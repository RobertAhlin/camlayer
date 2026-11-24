/*
 * CamLayer OBS filter: experimental native MediaPipe with GPU alpha rendering.
 *
 * The optional ellipse remains a diagnostic and is not person segmentation.
 * Rendering via OBS's graphics filter path avoids unsafe raw-frame lifetime
 * assumptions and handles the camera source's upstream pixel format in OBS.
 */
#include <obs-module.h>
#include <util/bmem.h>
#include "ai-engine.h"
#include "ai-worker.h"
#include <util/platform.h>
#include <graphics/graphics.h>
#include <graphics/vec4.h>
#include <math.h>
#include <string.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("camlayer-obs", "en-US")

#define CAMLAYER_FILTER_ID "camlayer_person_cutout"
#define SETTING_THRESHOLD "mask_threshold"
#define SETTING_SOFTNESS "edge_softness"
#define SETTING_CLEANUP "edge_cleanup"
#define SETTING_STABILITY "edge_stability"
#define SETTING_FOLLOW_SPEED "edge_follow_speed"
#define SETTING_DIAGNOSTIC "diagnostic_alpha"
#define SETTING_AI_ENGINE "ai_engine"
#define SETTING_RUNTIME_TEST "enable_runtime_test"
#define SETTING_LIVE_AI "enable_live_ai"
#define SETTING_ADVANCED "use_advanced_settings"
#define SETTING_COLOR_ENABLE "color_refinement"
#define SETTING_COLOR_STRENGTH "color_strength"
#define SETTING_BG_COLOR "background_color"
#define SETTING_HAIR_COLOR "hair_color"
#define SETTING_SKIN_COLOR "skin_color"
#define SETTING_CLOTH_COLOR "clothing_color"
#define SETTING_BG_TOL "background_tolerance"
#define SETTING_HAIR_TOL "hair_tolerance"
#define SETTING_SKIN_TOL "skin_tolerance"
#define SETTING_CLOTH_TOL "clothing_tolerance"
#define ADVANCED_GROUP "advanced_color_group"
#define CAMLAYER_MASK_SIDE 256
/* Capture rate follows the control: 10 to 30 requested frames per second. */

/* MediaPipe provides experimental live inference; RVM remains unavailable. */
enum camlayer_ai_engine {
    CAMLAYER_AI_MEDIAPIPE = 0,
    CAMLAYER_AI_RVM = 1,
};

static const char *camlayer_engine_name(enum camlayer_ai_engine engine)
{
    return engine == CAMLAYER_AI_RVM ? "RVM" : "MediaPipe";
}

struct camlayer_filter {
    obs_source_t *source;
    gs_effect_t *alpha_effect;
    gs_eparam_t *ellipse_parameter;
    double threshold;
    double softness;
    double cleanup;
    double stability;
    double follow_speed;
    bool advanced;
    bool diagnostic;
    enum camlayer_ai_engine engine;
    camlayer_ai_session *ai;
    bool runtime_test;
    bool live_ai;
    camlayer_worker *worker;
    gs_texrender_t *capture;
    gs_stagesurf_t *staging;
    gs_texture_t *mask_texture;
    gs_eparam_t *mask_parameter;
    gs_eparam_t *mask_enabled_parameter;
    gs_eparam_t *threshold_parameter;
    gs_eparam_t *softness_parameter;
    bool staging_ready;
    bool mask_ready;
    uint64_t last_capture;
    uint64_t staged_timestamp;
    uint32_t captured_width;
    uint32_t captured_height;
    float result_mask[CAMLAYER_MASK_SIDE * CAMLAYER_MASK_SIDE];
    uint8_t mask_bytes[CAMLAYER_MASK_SIDE * CAMLAYER_MASK_SIDE];
};

static const char *camlayer_get_name(void *unused)
{
    UNUSED_PARAMETER(unused);
    return obs_module_text("CamLayer.FilterName");
}

static void camlayer_update(void *data, obs_data_t *settings)
{
    struct camlayer_filter *filter = data;
    if (!filter)
        return;

    filter->threshold = obs_data_get_double(settings, SETTING_THRESHOLD);
    filter->softness = obs_data_get_double(settings, SETTING_SOFTNESS);
    filter->cleanup = obs_data_get_double(settings, SETTING_CLEANUP);
    filter->stability = obs_data_get_double(settings, SETTING_STABILITY);
    filter->follow_speed = obs_data_get_double(settings, SETTING_FOLLOW_SPEED);
    filter->advanced = obs_data_get_bool(settings, SETTING_ADVANCED);
    camlayer_worker_set_refinement(filter->worker,
                                   (float)filter->threshold, (float)filter->softness,
                                   (float)filter->cleanup, (float)filter->stability,
                                   (float)filter->follow_speed);
    filter->diagnostic = obs_data_get_bool(settings, SETTING_DIAGNOSTIC);
    const enum camlayer_ai_engine selected =
        (enum camlayer_ai_engine)obs_data_get_int(settings, SETTING_AI_ENGINE);
    const enum camlayer_ai_engine next_engine =
        selected == CAMLAYER_AI_RVM ? CAMLAYER_AI_RVM : CAMLAYER_AI_MEDIAPIPE;
    if (filter->engine != next_engine) {
        blog(LOG_INFO, "[CamLayer] Selected future AI backend: %s (not active)",
             camlayer_engine_name(next_engine));
    }
    filter->engine = next_engine;
    const bool wanted_test = obs_data_get_bool(settings, SETTING_RUNTIME_TEST);
    if (filter->ai && (!wanted_test || next_engine != CAMLAYER_AI_MEDIAPIPE)) {
        camlayer_ai_close(filter->ai);
        filter->ai = NULL;
    }
    filter->runtime_test = wanted_test;
    const bool live_ai = obs_data_get_bool(settings, SETTING_LIVE_AI);
    if (filter->worker && (!live_ai || next_engine != CAMLAYER_AI_MEDIAPIPE ||
                           filter->live_ai != live_ai)) {
        camlayer_worker_destroy(filter->worker);
        filter->worker = NULL;
        filter->mask_ready = false;
        filter->staging_ready = false;
    }
    filter->live_ai = live_ai;
    if (live_ai && next_engine == CAMLAYER_AI_MEDIAPIPE && !filter->worker) {
        char *model_path = obs_module_file("models/selfie_segmentation.onnx");
        filter->worker = camlayer_worker_create(CAMLAYER_BACKEND_MEDIAPIPE, model_path);
        if (filter->worker)
            blog(LOG_INFO, "[CamLayer] Live MediaPipe inference worker started (experimental)");
        else
            blog(LOG_WARNING, "[CamLayer] Could not create live inference worker");
        if (filter->worker)
            camlayer_worker_set_refinement(filter->worker,
                                           (float)filter->threshold, (float)filter->softness,
                                           (float)filter->cleanup, (float)filter->stability,
                                           (float)filter->follow_speed);
        bfree(model_path);
    }
    camlayer_color_config color = {0};
    color.enabled = filter->advanced &&
                    obs_data_get_bool(settings, SETTING_COLOR_ENABLE);
    color.strength = (float)obs_data_get_double(settings, SETTING_COLOR_STRENGTH);
    color.rgb[CAMLAYER_COLOR_BACKGROUND] =
        (uint32_t)obs_data_get_int(settings, SETTING_BG_COLOR);
    color.rgb[CAMLAYER_COLOR_HAIR] =
        (uint32_t)obs_data_get_int(settings, SETTING_HAIR_COLOR);
    color.rgb[CAMLAYER_COLOR_SKIN] =
        (uint32_t)obs_data_get_int(settings, SETTING_SKIN_COLOR);
    color.rgb[CAMLAYER_COLOR_CLOTHING] =
        (uint32_t)obs_data_get_int(settings, SETTING_CLOTH_COLOR);
    color.tolerance[CAMLAYER_COLOR_BACKGROUND] =
        (float)obs_data_get_double(settings, SETTING_BG_TOL);
    color.tolerance[CAMLAYER_COLOR_HAIR] =
        (float)obs_data_get_double(settings, SETTING_HAIR_TOL);
    color.tolerance[CAMLAYER_COLOR_SKIN] =
        (float)obs_data_get_double(settings, SETTING_SKIN_TOL);
    color.tolerance[CAMLAYER_COLOR_CLOTHING] =
        (float)obs_data_get_double(settings, SETTING_CLOTH_TOL);
    camlayer_worker_set_color_config(filter->worker, &color);
    if (wanted_test && next_engine == CAMLAYER_AI_MEDIAPIPE && !filter->ai) {
        char *path = obs_module_file("models/selfie_segmentation.onnx");
        char error[512] = {0};
        filter->ai = camlayer_ai_open(path, error, sizeof(error));
        if (filter->ai) {
            blog(LOG_INFO, "[CamLayer] Native MediaPipe ONNX session loaded; live video integration pending");
        } else {
            blog(LOG_WARNING, "[CamLayer] Native MediaPipe ONNX model not available: %s", error);
        }
        bfree(path);
    }
}

static void camlayer_destroy(void *data)
{
    struct camlayer_filter *filter = data;
    if (!filter)
        return;

    camlayer_worker_destroy(filter->worker);
    camlayer_ai_close(filter->ai);
    obs_enter_graphics();
    gs_texture_destroy(filter->mask_texture);
    gs_stagesurface_destroy(filter->staging);
    gs_texrender_destroy(filter->capture);
    gs_effect_destroy(filter->alpha_effect);
    obs_leave_graphics();
    bfree(filter);
}

static void *camlayer_create(obs_data_t *settings, obs_source_t *source)
{
    struct camlayer_filter *filter = bzalloc(sizeof(*filter));
    filter->source = source;

    char *effect_path = obs_module_file("ellipse_alpha.effect");
    if (!effect_path) {
        blog(LOG_ERROR, "[CamLayer] Alpha effect file not found");
        camlayer_destroy(filter);
        return NULL;
    }

    obs_enter_graphics();
    filter->alpha_effect = gs_effect_create_from_file(effect_path, NULL);
    if (filter->alpha_effect) {
        filter->ellipse_parameter =
            gs_effect_get_param_by_name(filter->alpha_effect, "ellipse_enabled");
        filter->mask_parameter =
            gs_effect_get_param_by_name(filter->alpha_effect, "ai_mask");
        filter->mask_enabled_parameter =
            gs_effect_get_param_by_name(filter->alpha_effect, "mask_enabled");
        filter->threshold_parameter =
            gs_effect_get_param_by_name(filter->alpha_effect, "mask_threshold");
        filter->softness_parameter =
            gs_effect_get_param_by_name(filter->alpha_effect, "edge_softness");
    }
    obs_leave_graphics();

    bfree(effect_path);

    if (!filter->alpha_effect || !filter->ellipse_parameter) {
        blog(LOG_ERROR, "[CamLayer] Could not load alpha diagnostic shader");
        camlayer_destroy(filter);
        return NULL;
    }

    camlayer_update(filter, settings);
    blog(LOG_INFO, "[CamLayer] GPU alpha diagnostic filter created");
    return filter;
}

static void camlayer_defaults(obs_data_t *settings)
{
    obs_data_set_default_double(settings, SETTING_THRESHOLD, 0.50);
    obs_data_set_default_double(settings, SETTING_SOFTNESS, 0.0);
    obs_data_set_default_double(settings, SETTING_CLEANUP, 0.0);
    obs_data_set_default_double(settings, SETTING_STABILITY, 1.0);
    obs_data_set_default_double(settings, SETTING_FOLLOW_SPEED, 1.0);
    obs_data_set_default_bool(settings, SETTING_DIAGNOSTIC, false);
    obs_data_set_default_int(settings, SETTING_AI_ENGINE, CAMLAYER_AI_MEDIAPIPE);
    obs_data_set_default_bool(settings, SETTING_RUNTIME_TEST, false);
    obs_data_set_default_bool(settings, SETTING_LIVE_AI, false);
    obs_data_set_default_bool(settings, SETTING_ADVANCED, false);
    obs_data_set_default_bool(settings, SETTING_COLOR_ENABLE, false);
    obs_data_set_default_double(settings, SETTING_COLOR_STRENGTH, 35.0);
    obs_data_set_default_int(settings, SETTING_BG_COLOR, 0x808080);
    obs_data_set_default_int(settings, SETTING_HAIR_COLOR, 0x77736D);
    obs_data_set_default_int(settings, SETTING_SKIN_COLOR, 0xC69A80);
    obs_data_set_default_int(settings, SETTING_CLOTH_COLOR, 0x494B49);
    obs_data_set_default_double(settings, SETTING_BG_TOL, 25.0);
    obs_data_set_default_double(settings, SETTING_HAIR_TOL, 25.0);
    obs_data_set_default_double(settings, SETTING_SKIN_TOL, 20.0);
    obs_data_set_default_double(settings, SETTING_CLOTH_TOL, 30.0);
}

static bool camlayer_advanced_modified(obs_properties_t *props,
                                       obs_property_t *property,
                                       obs_data_t *settings)
{
    UNUSED_PARAMETER(property);
    obs_property_t *group = obs_properties_get(props, ADVANCED_GROUP);
    if (!group) return false;
    obs_property_set_visible(group, obs_data_get_bool(settings, SETTING_ADVANCED));
    return true;
}

static obs_properties_t *camlayer_properties(void *data)
{
    obs_properties_t *props = obs_properties_create();

    obs_property_t *engine = obs_properties_add_list(
        props, SETTING_AI_ENGINE, obs_module_text("CamLayer.AIEngine"),
        OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(engine, obs_module_text("CamLayer.EngineMediaPipe"),
                              CAMLAYER_AI_MEDIAPIPE);
    obs_property_list_add_int(engine, obs_module_text("CamLayer.EngineRVM"),
                              CAMLAYER_AI_RVM);
    obs_properties_add_bool(props, SETTING_RUNTIME_TEST,
                            obs_module_text("CamLayer.RuntimeTest"));
    obs_properties_add_bool(props, SETTING_LIVE_AI,
                            obs_module_text("CamLayer.LiveAI"));
    obs_properties_add_text(props, "engine_info",
                            obs_module_text("CamLayer.EngineNotice"), OBS_TEXT_INFO);

    obs_properties_add_bool(props, SETTING_DIAGNOSTIC,
                            obs_module_text("CamLayer.DiagnosticAlpha"));
    obs_properties_add_float_slider(props, SETTING_THRESHOLD,
                                    obs_module_text("CamLayer.Threshold"), 0.15, 0.85, 0.01);
    obs_properties_add_float_slider(props, SETTING_SOFTNESS,
                                    obs_module_text("CamLayer.Softness"), -2.0, 2.0, 0.01);
    obs_properties_add_float_slider(props, SETTING_CLEANUP,
                                    obs_module_text("CamLayer.Cleanup"), 0.0, 1.0, 0.05);
    obs_properties_add_float_slider(props, SETTING_STABILITY,
                                    obs_module_text("CamLayer.Stability"), 0.0, 2.0, 0.05);
    obs_properties_add_float_slider(props, SETTING_FOLLOW_SPEED,
                                    obs_module_text("CamLayer.FollowSpeed"), 0.0, 5.0, 0.05);
    obs_properties_add_text(props, "bootstrap_info",
                            obs_module_text("CamLayer.BootstrapNotice"),
                            OBS_TEXT_INFO);

    obs_property_t *advanced_toggle = obs_properties_add_bool(
        props, SETTING_ADVANCED, obs_module_text("CamLayer.UseAdvanced"));
    obs_property_set_modified_callback(advanced_toggle, camlayer_advanced_modified);
    obs_properties_t *advanced_props = obs_properties_create();
    obs_properties_add_bool(advanced_props, SETTING_COLOR_ENABLE,
                            obs_module_text("CamLayer.ColorEnable"));
    obs_properties_add_text(advanced_props, "advanced_color_info",
                            obs_module_text("CamLayer.ColorNotice"), OBS_TEXT_INFO);
    obs_properties_add_color(advanced_props, SETTING_BG_COLOR,
                             obs_module_text("CamLayer.BgColor"));
    obs_properties_add_float_slider(advanced_props, SETTING_BG_TOL,
                                    obs_module_text("CamLayer.BgTolerance"),
                                    0.0, 100.0, 1.0);
    obs_properties_add_color(advanced_props, SETTING_HAIR_COLOR,
                             obs_module_text("CamLayer.HairColor"));
    obs_properties_add_float_slider(advanced_props, SETTING_HAIR_TOL,
                                    obs_module_text("CamLayer.HairTolerance"),
                                    0.0, 100.0, 1.0);
    obs_properties_add_color(advanced_props, SETTING_SKIN_COLOR,
                             obs_module_text("CamLayer.SkinColor"));
    obs_properties_add_float_slider(advanced_props, SETTING_SKIN_TOL,
                                    obs_module_text("CamLayer.SkinTolerance"),
                                    0.0, 100.0, 1.0);
    obs_properties_add_color(advanced_props, SETTING_CLOTH_COLOR,
                             obs_module_text("CamLayer.ClothingColor"));
    obs_properties_add_float_slider(advanced_props, SETTING_CLOTH_TOL,
                                    obs_module_text("CamLayer.ClothingTolerance"),
                                    0.0, 100.0, 1.0);
    obs_properties_add_float_slider(advanced_props, SETTING_COLOR_STRENGTH,
                                    obs_module_text("CamLayer.ColorStrength"),
                                    0.0, 100.0, 1.0);
    obs_property_t *advanced_group = obs_properties_add_group(
        props, ADVANCED_GROUP, obs_module_text("CamLayer.ColorGroup"),
        OBS_GROUP_NORMAL, advanced_props);
    const struct camlayer_filter *filter = data;
    obs_property_set_visible(advanced_group, filter && filter->advanced);
    return props;
}

/* Called exclusively from OBS's graphics render callback.
 * Capture into a small staging surface rather than reading full HD frames.
 * Map the previously staged frame before staging the next one.
 */
static void camlayer_sample_camera(struct camlayer_filter *filter)
{
    obs_source_t *target = obs_filter_get_target(filter->source);
    if (!target || !filter->worker ||
        camlayer_worker_get_status(filter->worker) != CAMLAYER_WORKER_READY) {
        filter->mask_ready = false;
        return;
    }

    const uint32_t width = obs_source_get_width(target);
    const uint32_t height = obs_source_get_height(target);
    if (!width || !height) {
        filter->mask_ready = false;
        filter->staging_ready = false;
        filter->staged_timestamp = 0;
        camlayer_worker_reset_refinement(filter->worker);
        return;
    }
    if (width != filter->captured_width || height != filter->captured_height) {
        filter->captured_width = width;
        filter->captured_height = height;
        filter->staging_ready = false;
        filter->mask_ready = false;
        filter->staged_timestamp = 0;
        camlayer_worker_reset_refinement(filter->worker);
    }
    if (!filter->capture)
        filter->capture = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
    if (!filter->staging)
        filter->staging = gs_stagesurface_create(CAMLAYER_MASK_SIDE,
                                                 CAMLAYER_MASK_SIDE, GS_BGRA);
    if (!filter->capture || !filter->staging)
        return;

    const uint64_t now = os_gettime_ns();
    /* Follow speed 0 -> 10 Hz, 1 -> 20 Hz, 2 -> 30 Hz, 5 -> 60 Hz.
     * Actual mask FPS may remain below this rate on slower CPUs.
     */
    const double follow = fmax(0.0, fmin(5.0, filter->follow_speed));
    const uint64_t capture_interval = (uint64_t)(100000000.0 / (1.0 + follow));
    if (now - filter->last_capture < capture_interval)
        return;
    filter->last_capture = now;

    if (filter->staging_ready) {
        uint8_t *pixels = NULL;
        uint32_t stride = 0;
        if (gs_stagesurface_map(filter->staging, &pixels, &stride)) {
            camlayer_worker_submit(filter->worker, pixels, CAMLAYER_MASK_SIDE,
                                    CAMLAYER_MASK_SIDE, stride,
                                    filter->staged_timestamp);
            gs_stagesurface_unmap(filter->staging);
        }
        filter->staging_ready = false;
    }

    gs_texrender_reset(filter->capture);
    gs_blend_state_push();
    gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
    if (gs_texrender_begin(filter->capture, CAMLAYER_MASK_SIDE,
                            CAMLAYER_MASK_SIDE)) {
        struct vec4 transparent = {0};
        gs_clear(GS_CLEAR_COLOR, &transparent, 0.0f, 0);
        gs_ortho(0.0f, (float)width, 0.0f, (float)height, -100.0f, 100.0f);
        obs_source_video_render(target);
        gs_texrender_end(filter->capture);
        gs_texture_t *texture = gs_texrender_get_texture(filter->capture);
        if (texture) {
            gs_stage_texture(filter->staging, texture);
            filter->staging_ready = true;
            filter->staged_timestamp = now;
        }
    }
    gs_blend_state_pop();
}

static void camlayer_publish_mask(struct camlayer_filter *filter)
{
    if (!filter->worker || !filter->live_ai || filter->diagnostic)
        return;
    uint64_t timestamp = 0;
    if (!camlayer_worker_take_mask(filter->worker, filter->result_mask,
                                   CAMLAYER_MASK_SIDE * CAMLAYER_MASK_SIDE,
                                   &timestamp))
        return;
    for (size_t i = 0; i < CAMLAYER_MASK_SIDE * CAMLAYER_MASK_SIDE; i++) {
        float v = filter->result_mask[i];
        if (!isfinite(v)) v = 0.0f;
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        filter->mask_bytes[i] = (uint8_t)(v * 255.0f + 0.5f);
    }
    if (!filter->mask_texture) {
        filter->mask_texture = gs_texture_create(
            CAMLAYER_MASK_SIDE, CAMLAYER_MASK_SIDE, GS_R8, 1, NULL, GS_DYNAMIC);
    }
    if (filter->mask_texture) {
        gs_texture_set_image(filter->mask_texture, filter->mask_bytes,
                             CAMLAYER_MASK_SIDE, false);
        if (!filter->mask_ready)
            blog(LOG_INFO, "[CamLayer] First live MediaPipe alpha mask uploaded to GPU");
        filter->mask_ready = true;
    }
    UNUSED_PARAMETER(timestamp);
}

static void camlayer_render(void *data, gs_effect_t *unused)
{
    UNUSED_PARAMETER(unused);
    struct camlayer_filter *filter = data;
    if (!filter || !filter->alpha_effect)
        return;

    if (filter->live_ai && !filter->diagnostic) {
        camlayer_sample_camera(filter);
        camlayer_publish_mask(filter);
    }

    if (!obs_source_process_filter_begin(filter->source, GS_RGBA,
                                          OBS_NO_DIRECT_RENDERING))
        return;

    /* The diagnostic ellipse always overrides the experimental live mask.
     * RVM remains unavailable until recurrent inference is implemented.
     */
    gs_effect_set_bool(filter->ellipse_parameter, filter->diagnostic);
    const bool apply_mask = filter->live_ai && !filter->diagnostic &&
                            filter->engine == CAMLAYER_AI_MEDIAPIPE &&
                            filter->mask_ready && filter->mask_texture;
    if (filter->mask_enabled_parameter)
        gs_effect_set_bool(filter->mask_enabled_parameter, apply_mask);
    if (filter->threshold_parameter)
        gs_effect_set_float(filter->threshold_parameter, (float)filter->threshold);
    if (filter->softness_parameter)
        gs_effect_set_float(filter->softness_parameter, (float)filter->softness);
    if (filter->mask_parameter && apply_mask)
        gs_effect_set_texture(filter->mask_parameter, filter->mask_texture);
    obs_source_process_filter_end(filter->source, filter->alpha_effect, 0, 0);
}

static struct obs_source_info camlayer_filter_info = {
    .id = CAMLAYER_FILTER_ID,
    .type = OBS_SOURCE_TYPE_FILTER,
    .output_flags = OBS_SOURCE_VIDEO,
    .get_name = camlayer_get_name,
    .create = camlayer_create,
    .destroy = camlayer_destroy,
    .update = camlayer_update,
    .get_defaults = camlayer_defaults,
    .get_properties = camlayer_properties,
    .video_render = camlayer_render,
};

bool obs_module_load(void)
{
    obs_register_source(&camlayer_filter_info);
    blog(LOG_INFO, "[CamLayer] GPU alpha diagnostic filter registered");
    return true;
}

const char *obs_module_description(void)
{
    return "CamLayer OBS background removal (experimental MediaPipe live alpha)";
}
