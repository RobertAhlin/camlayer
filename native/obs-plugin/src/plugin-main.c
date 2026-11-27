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
#define SETTING_FEATHER_WIDTH "edge_feather_width"
#define SETTING_EDGE_DETAIL "edge_detail"
#define SETTING_CLEANUP "edge_cleanup"
#define SETTING_STABILITY "edge_stability"
#define SETTING_FOLLOW_SPEED "edge_follow_speed"
#define SETTING_DIAGNOSTIC "diagnostic_alpha"
#define SETTING_AI_ENGINE "ai_engine"
#define SETTING_RUNTIME_TEST "enable_runtime_test"
#define SETTING_LIVE_AI "enable_live_ai"
#define SETTING_RVM_BLACK "rvm_alpha_black"
#define SETTING_RVM_WHITE "rvm_alpha_white"
#define SETTING_RVM_CLEANUP "rvm_cleanup"
#define SETTING_RVM_STABILITY "rvm_stability"
#define SETTING_RVM_FOLLOW "rvm_follow_speed"
// New keys prevent importing the failed SDF settings into Gaussian v3.
#define SETTING_RVM_FEATHER "rvm_gauss_v3_softness"
#define SETTING_RVM_FEATHER_WIDTH "rvm_gauss_v3_width"
#define SETTING_RVM_INNER "rvm_gauss_v3_inner_share"
#define SETTING_RVM_EDGE_HANDLING "rvm_edge_handling_enabled"
#define SETTING_RVM_RESOLUTION "rvm_mask_resolution"
#define CAMLAYER_MASK_SIDE 256
#define CAMLAYER_MASK_MAX_SIDE 1024
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
    double feather_width;
    double edge_detail;
    double cleanup;
    double stability;
    double follow_speed;
    double rvm_black, rvm_white, rvm_cleanup, rvm_stability, rvm_follow;
    double rvm_feather, rvm_feather_width, rvm_inner_share;
    bool rvm_edge_handling;
    uint32_t mask_side;
    uint32_t allocated_side;
    bool diagnostic;
    enum camlayer_ai_engine engine;
    camlayer_ai_session *ai;
    bool runtime_test;
    bool live_ai;
    camlayer_worker *worker;
    enum camlayer_ai_engine worker_engine;
    gs_texrender_t *capture;
    gs_stagesurf_t *staging;
    gs_texture_t *mask_texture;
    gs_eparam_t *mask_parameter;
    gs_eparam_t *mask_enabled_parameter;
    gs_eparam_t *threshold_parameter;
    gs_eparam_t *softness_parameter;
    gs_eparam_t *feather_parameter;
    gs_eparam_t *detail_parameter;
    gs_eparam_t *rvm_mode_parameter;
    bool staging_ready;
    bool mask_ready;
    uint64_t last_capture;
    uint64_t staged_timestamp;
    uint32_t captured_width;
    uint32_t captured_height;
    float result_mask[CAMLAYER_MASK_MAX_SIDE * CAMLAYER_MASK_MAX_SIDE];
    uint8_t mask_bytes[CAMLAYER_MASK_MAX_SIDE * CAMLAYER_MASK_MAX_SIDE];
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
    filter->feather_width = obs_data_get_double(settings, SETTING_FEATHER_WIDTH);
    filter->edge_detail = obs_data_get_double(settings, SETTING_EDGE_DETAIL);
    filter->cleanup = obs_data_get_double(settings, SETTING_CLEANUP);
    filter->stability = obs_data_get_double(settings, SETTING_STABILITY);
    filter->follow_speed = obs_data_get_double(settings, SETTING_FOLLOW_SPEED);
    filter->rvm_black = obs_data_get_double(settings, SETTING_RVM_BLACK);
    filter->rvm_white = obs_data_get_double(settings, SETTING_RVM_WHITE);
    filter->rvm_cleanup = obs_data_get_double(settings, SETTING_RVM_CLEANUP);
    filter->rvm_stability = obs_data_get_double(settings, SETTING_RVM_STABILITY);
    filter->rvm_follow = obs_data_get_double(settings, SETTING_RVM_FOLLOW);
    filter->rvm_feather = obs_data_get_double(settings, SETTING_RVM_FEATHER);
    filter->rvm_feather_width = obs_data_get_double(settings, SETTING_RVM_FEATHER_WIDTH);
    filter->rvm_inner_share = obs_data_get_double(settings, SETTING_RVM_INNER);
    filter->rvm_edge_handling = obs_data_get_bool(settings, SETTING_RVM_EDGE_HANDLING);
    const int configured_side = (int)obs_data_get_int(settings, SETTING_RVM_RESOLUTION);
    const uint32_t selected_side = configured_side == 384 || configured_side == 512 ||
        configured_side == 768 || configured_side == 1024
        ? (uint32_t)configured_side : CAMLAYER_MASK_SIDE;
    filter->diagnostic = obs_data_get_bool(settings, SETTING_DIAGNOSTIC);
    const enum camlayer_ai_engine selected =
        (enum camlayer_ai_engine)obs_data_get_int(settings, SETTING_AI_ENGINE);
    const enum camlayer_ai_engine next_engine =
        selected == CAMLAYER_AI_RVM ? CAMLAYER_AI_RVM : CAMLAYER_AI_MEDIAPIPE;
    if (filter->engine != next_engine) {
        blog(LOG_INFO, "[CamLayer] AI engine selection: %s",
             camlayer_engine_name(next_engine));
    }
    const uint32_t next_side = next_engine == CAMLAYER_AI_RVM
        ? selected_side : CAMLAYER_MASK_SIDE;
    const bool size_changed = filter->mask_side && filter->mask_side != next_side;
    filter->mask_side = next_side;
    filter->engine = next_engine;
    if (filter->worker)
        camlayer_worker_set_rvm_feather(filter->worker,
            (float)filter->rvm_black, (float)filter->rvm_white,
            filter->rvm_edge_handling ? (float)filter->rvm_feather : 0.0f,
            (float)filter->rvm_feather_width,
            filter->rvm_edge_handling ? (float)filter->rvm_inner_share : 0.5f);
    if (filter->worker)
        camlayer_worker_set_refinement(filter->worker,
            next_engine == CAMLAYER_AI_RVM ? 0.50f : (float)filter->threshold,
            next_engine == CAMLAYER_AI_RVM ? 0.0f : (float)filter->softness,
            next_engine == CAMLAYER_AI_RVM ? (filter->rvm_edge_handling ? (float)filter->rvm_cleanup : 0.0f) : (float)filter->cleanup,
            next_engine == CAMLAYER_AI_RVM ? (filter->rvm_edge_handling ? (float)filter->rvm_stability : 0.0f) : (float)filter->stability,
            next_engine == CAMLAYER_AI_RVM ? (filter->rvm_edge_handling ? (float)filter->rvm_follow : 1.0f) : (float)filter->follow_speed);
    const bool wanted_test = obs_data_get_bool(settings, SETTING_RUNTIME_TEST);
    if (filter->ai && (!wanted_test || next_engine != CAMLAYER_AI_MEDIAPIPE)) {
        camlayer_ai_close(filter->ai);
        filter->ai = NULL;
    }
    filter->runtime_test = wanted_test;
    const bool live_ai = obs_data_get_bool(settings, SETTING_LIVE_AI);
    if (filter->worker && (!live_ai || size_changed || next_engine != filter->worker_engine ||
                           filter->live_ai != live_ai)) {
        camlayer_worker_destroy(filter->worker);
        filter->worker = NULL;
        filter->mask_ready = false;
        filter->staging_ready = false;
    }
    filter->live_ai = live_ai;
    if (live_ai && !filter->worker) {
        const bool use_rvm = next_engine == CAMLAYER_AI_RVM;
        char *model_path = obs_module_file(use_rvm
            ? "models/rvm_mobilenetv3_fp32.onnx"
            : "models/selfie_segmentation.onnx");
        filter->worker = camlayer_worker_create(use_rvm
            ? CAMLAYER_BACKEND_RVM : CAMLAYER_BACKEND_MEDIAPIPE, model_path,
            filter->mask_side);
        if (filter->worker) {
            filter->worker_engine = next_engine;
            blog(LOG_INFO, "[CamLayer] Live %s inference worker starting",
                 camlayer_engine_name(next_engine));
        }
        else
            blog(LOG_WARNING, "[CamLayer] Could not create live inference worker");
        if (filter->worker)
            camlayer_worker_set_rvm_feather(filter->worker,
                (float)filter->rvm_black, (float)filter->rvm_white,
                filter->rvm_edge_handling ? (float)filter->rvm_feather : 0.0f,
                (float)filter->rvm_feather_width,
                filter->rvm_edge_handling ? (float)filter->rvm_inner_share : 0.5f);
        if (filter->worker)
            camlayer_worker_set_refinement(filter->worker,
                                           next_engine == CAMLAYER_AI_RVM ? 0.50f : (float)filter->threshold,
                                           next_engine == CAMLAYER_AI_RVM ? 0.0f : (float)filter->softness,
                                           next_engine == CAMLAYER_AI_RVM ? (filter->rvm_edge_handling ? (float)filter->rvm_cleanup : 0.0f) : (float)filter->cleanup,
                                           next_engine == CAMLAYER_AI_RVM ? (filter->rvm_edge_handling ? (float)filter->rvm_stability : 0.0f) : (float)filter->stability,
                                           next_engine == CAMLAYER_AI_RVM ? (filter->rvm_edge_handling ? (float)filter->rvm_follow : 1.0f) : (float)filter->follow_speed);
        bfree(model_path);
    }
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
        filter->feather_parameter =
            gs_effect_get_param_by_name(filter->alpha_effect, "edge_feather_width");
        filter->detail_parameter =
            gs_effect_get_param_by_name(filter->alpha_effect, "edge_detail");
        filter->rvm_mode_parameter = gs_effect_get_param_by_name(filter->alpha_effect, "rvm_mode");

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
    obs_data_set_default_double(settings, SETTING_FEATHER_WIDTH, 1.5);
    obs_data_set_default_double(settings, SETTING_EDGE_DETAIL, 0.0);
    obs_data_set_default_double(settings, SETTING_CLEANUP, 0.0);
    obs_data_set_default_double(settings, SETTING_STABILITY, 1.0);
    obs_data_set_default_double(settings, SETTING_FOLLOW_SPEED, 1.0);
    obs_data_set_default_double(settings, SETTING_RVM_BLACK, 0.0);
    obs_data_set_default_double(settings, SETTING_RVM_WHITE, 1.0);
    obs_data_set_default_double(settings, SETTING_RVM_CLEANUP, 0.0);
    obs_data_set_default_double(settings, SETTING_RVM_STABILITY, 0.0);
    obs_data_set_default_double(settings, SETTING_RVM_FOLLOW, 1.0);
    obs_data_set_default_double(settings, SETTING_RVM_FEATHER, 0.0);
    obs_data_set_default_double(settings, SETTING_RVM_FEATHER_WIDTH, 4.0);
    obs_data_set_default_double(settings, SETTING_RVM_INNER, 0.50);
    obs_data_set_default_bool(settings, SETTING_RVM_EDGE_HANDLING, false);
    obs_data_set_default_int(settings, SETTING_RVM_RESOLUTION, 256);
    obs_data_set_default_bool(settings, SETTING_DIAGNOSTIC, false);
    obs_data_set_default_int(settings, SETTING_AI_ENGINE, CAMLAYER_AI_MEDIAPIPE);
    obs_data_set_default_bool(settings, SETTING_RUNTIME_TEST, false);
    obs_data_set_default_bool(settings, SETTING_LIVE_AI, false);

}

static bool camlayer_engine_modified(obs_properties_t *props, obs_property_t *property,
                                     obs_data_t *settings)
{
    UNUSED_PARAMETER(property);
    const bool rvm = obs_data_get_int(settings, SETTING_AI_ENGINE) == CAMLAYER_AI_RVM;
    const char *media[] = {SETTING_THRESHOLD, SETTING_SOFTNESS, SETTING_FEATHER_WIDTH,
                           SETTING_EDGE_DETAIL, SETTING_CLEANUP, SETTING_STABILITY,
                           SETTING_FOLLOW_SPEED};
    const bool edge_enabled = obs_data_get_bool(settings, SETTING_RVM_EDGE_HANDLING);
    const char *rvm_advanced[] = {SETTING_RVM_CLEANUP, SETTING_RVM_STABILITY,
                                  SETTING_RVM_FOLLOW, SETTING_RVM_FEATHER,
                                  SETTING_RVM_FEATHER_WIDTH, SETTING_RVM_INNER};
    for (size_t i = 0; i < sizeof(media) / sizeof(media[0]); ++i)
        obs_property_set_visible(obs_properties_get(props, media[i]), !rvm);
    obs_property_set_visible(obs_properties_get(props, SETTING_RVM_BLACK), rvm);
    obs_property_set_visible(obs_properties_get(props, SETTING_RVM_WHITE), rvm);
    obs_property_set_visible(obs_properties_get(props, SETTING_RVM_EDGE_HANDLING), rvm);
    obs_property_set_visible(obs_properties_get(props, SETTING_RVM_RESOLUTION), rvm);
    for (size_t i = 0; i < sizeof(rvm_advanced) / sizeof(rvm_advanced[0]); ++i)
        obs_property_set_visible(obs_properties_get(props, rvm_advanced[i]),
                                 rvm && edge_enabled);
    return true;
}

static obs_properties_t *camlayer_properties(void *data)
{
    UNUSED_PARAMETER(data);
    obs_properties_t *props = obs_properties_create();

    obs_property_t *engine = obs_properties_add_list(
        props, SETTING_AI_ENGINE, obs_module_text("CamLayer.AIEngine"),
        OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(engine, obs_module_text("CamLayer.EngineMediaPipe"),
                              CAMLAYER_AI_MEDIAPIPE);
    obs_property_list_add_int(engine, obs_module_text("CamLayer.EngineRVM"),
                              CAMLAYER_AI_RVM);
    obs_property_set_modified_callback(engine, camlayer_engine_modified);
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
    obs_properties_add_float_slider(props, SETTING_FEATHER_WIDTH,
                                    obs_module_text("CamLayer.FeatherWidth"), 0.0, 3.0, 0.05);
    obs_properties_add_float_slider(props, SETTING_EDGE_DETAIL,
                                    obs_module_text("CamLayer.EdgeDetail"),
                                    0.0, 2.0, 0.05);
    obs_properties_add_float_slider(props, SETTING_CLEANUP,
                                    obs_module_text("CamLayer.Cleanup"), 0.0, 1.0, 0.05);
    obs_properties_add_float_slider(props, SETTING_STABILITY,
                                    obs_module_text("CamLayer.Stability"), 0.0, 2.0, 0.05);
    obs_properties_add_float_slider(props, SETTING_FOLLOW_SPEED,
                                    obs_module_text("CamLayer.FollowSpeed"), 0.0, 5.0, 0.05);
    obs_property_t *resolution = obs_properties_add_list(
        props, SETTING_RVM_RESOLUTION, obs_module_text("CamLayer.RVMResolution"),
        OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(resolution, "256 x 256 (reference)", 256);
    obs_property_list_add_int(resolution, "384 x 384 (balanced)", 384);
    obs_property_list_add_int(resolution, "512 x 512 (high quality)", 512);
    obs_property_list_add_int(resolution, "768 x 768 (experimental)", 768);
    obs_property_list_add_int(resolution, "1024 x 1024 (very experimental / slow)", 1024);
    obs_properties_add_float_slider(props, SETTING_RVM_BLACK,
                                    obs_module_text("CamLayer.RVMBlack"), 0.0, 0.9, 0.01);
    obs_properties_add_float_slider(props, SETTING_RVM_WHITE,
                                    obs_module_text("CamLayer.RVMWhite"), 0.1, 1.0, 0.01);
    obs_property_t *edge_handling = obs_properties_add_bool(
        props, SETTING_RVM_EDGE_HANDLING, obs_module_text("CamLayer.RVMEdgeHandling"));
    obs_property_set_modified_callback(edge_handling, camlayer_engine_modified);
    obs_properties_add_float_slider(props, SETTING_RVM_CLEANUP,
                                    obs_module_text("CamLayer.RVMCleanup"), 0.0, 2.0, 0.05);
    obs_properties_add_float_slider(props, SETTING_RVM_STABILITY,
                                    obs_module_text("CamLayer.RVMStability"), 0.0, 4.0, 0.05);
    obs_properties_add_float_slider(props, SETTING_RVM_FOLLOW,
                                    obs_module_text("CamLayer.RVMFollow"), 0.0, 10.0, 0.05);
    obs_properties_add_float_slider(props, SETTING_RVM_FEATHER,
                                    obs_module_text("CamLayer.RVMFeather"), 0.0, 10.0, 0.05);
    obs_properties_add_float_slider(props, SETTING_RVM_FEATHER_WIDTH,
                                    obs_module_text("CamLayer.RVMFeatherWidth"), 0.0, 20.0, 0.25);
    obs_properties_add_float_slider(props, SETTING_RVM_INNER,
                                    obs_module_text("CamLayer.RVMInner"), 0.0, 1.0, 0.05);
    if (data) {
        struct camlayer_filter *filter = data;
        obs_data_t *current = obs_source_get_settings(filter->source);
        camlayer_engine_modified(props, engine, current);
        obs_data_release(current);
    } else {
        obs_data_t *defaults = obs_data_create();
        obs_data_set_int(defaults, SETTING_AI_ENGINE, CAMLAYER_AI_MEDIAPIPE);
        camlayer_engine_modified(props, engine, defaults);
        obs_data_release(defaults);
    }
    obs_properties_add_text(props, "bootstrap_info",
                            obs_module_text("CamLayer.BootstrapNotice"),
                            OBS_TEXT_INFO);

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
    if (filter->allocated_side != filter->mask_side) {
        gs_stagesurface_destroy(filter->staging);
        filter->staging = NULL;
        gs_texrender_destroy(filter->capture);
        filter->capture = NULL;
        gs_texture_destroy(filter->mask_texture);
        filter->mask_texture = NULL;
        filter->allocated_side = filter->mask_side;
        filter->staging_ready = false;
        filter->mask_ready = false;
    }
    if (!filter->staging)
        filter->staging = gs_stagesurface_create(filter->mask_side,
                                                 filter->mask_side, GS_BGRA);
    if (!filter->capture || !filter->staging)
        return;

    const uint64_t now = os_gettime_ns();
    /* Follow speed 0 -> 10 Hz, 1 -> 20 Hz, 2 -> 30 Hz, 5 -> 60 Hz.
     * Actual mask FPS may remain below this rate on slower CPUs.
     */
    const double follow = fmax(0.0, fmin(5.0,
        filter->engine == CAMLAYER_AI_RVM ?
        (filter->rvm_edge_handling ? filter->rvm_follow : 1.0) : filter->follow_speed));
    // Keep requests capped at 60 Hz even when RVM follow is set above 5.
    // The camera itself may provide fewer frames (e.g. 30 FPS).
    const uint64_t capture_interval = (uint64_t)(100000000.0 / (1.0 + follow));
    if (now - filter->last_capture < capture_interval)
        return;
    filter->last_capture = now;

    if (filter->staging_ready) {
        uint8_t *pixels = NULL;
        uint32_t stride = 0;
        if (gs_stagesurface_map(filter->staging, &pixels, &stride)) {
            camlayer_worker_submit(filter->worker, pixels, filter->mask_side,
                                    filter->mask_side, stride,
                                    filter->staged_timestamp);
            gs_stagesurface_unmap(filter->staging);
        }
        filter->staging_ready = false;
    }

    gs_texrender_reset(filter->capture);
    gs_blend_state_push();
    gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
    if (gs_texrender_begin(filter->capture, filter->mask_side,
                            filter->mask_side)) {
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
                                   (size_t)filter->mask_side * filter->mask_side,
                                   &timestamp))
        return;
    for (size_t i = 0; i < (size_t)filter->mask_side * filter->mask_side; i++) {
        float v = filter->result_mask[i];
        if (!isfinite(v)) v = 0.0f;
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        filter->mask_bytes[i] = (uint8_t)(v * 255.0f + 0.5f);
    }
    if (!filter->mask_texture) {
        filter->mask_texture = gs_texture_create(
            filter->mask_side, filter->mask_side, GS_R8, 1, NULL, GS_DYNAMIC);
    }
    if (filter->mask_texture) {
        gs_texture_set_image(filter->mask_texture, filter->mask_bytes,
                             filter->mask_side, false);
        if (!filter->mask_ready)
            blog(LOG_INFO, "[CamLayer] First live %s alpha mask uploaded to GPU", camlayer_engine_name(filter->engine));
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
                            filter->mask_ready && filter->mask_texture;
    if (filter->mask_enabled_parameter)
        gs_effect_set_bool(filter->mask_enabled_parameter, apply_mask);
    if (filter->rvm_mode_parameter)
        gs_effect_set_bool(filter->rvm_mode_parameter, filter->engine == CAMLAYER_AI_RVM);
    if (filter->threshold_parameter)
        gs_effect_set_float(filter->threshold_parameter, (float)filter->threshold);
    if (filter->softness_parameter)
        gs_effect_set_float(filter->softness_parameter, (float)filter->softness);
    if (filter->mask_parameter && apply_mask)
        gs_effect_set_texture(filter->mask_parameter, filter->mask_texture);
    if (filter->feather_parameter)
        gs_effect_set_float(filter->feather_parameter, (float)filter->feather_width);
    if (filter->detail_parameter)
        gs_effect_set_float(filter->detail_parameter, (float)filter->edge_detail);
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
