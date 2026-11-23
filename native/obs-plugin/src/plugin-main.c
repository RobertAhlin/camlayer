/*
 * CamLayer OBS filter: GPU alpha diagnostic, BEFORE native AI integration.
 *
 * The optional ellipse is a test mask, NOT person segmentation.
 * Rendering via OBS's graphics filter path avoids unsafe raw-frame lifetime
 * assumptions and handles the camera source's upstream pixel format in OBS.
 */
#include <obs-module.h>
#include <util/bmem.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("camlayer-obs", "en-US")

#define CAMLAYER_FILTER_ID "camlayer_person_cutout"
#define SETTING_THRESHOLD "mask_threshold"
#define SETTING_SOFTNESS "edge_softness"
#define SETTING_CLEANUP "edge_cleanup"
#define SETTING_STABILITY "edge_stability"
#define SETTING_DIAGNOSTIC "diagnostic_alpha"
#define SETTING_AI_ENGINE "ai_engine"

/* Engine selection is persisted now. Live inference will follow in a later build. */
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
    bool diagnostic;
    enum camlayer_ai_engine engine;
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
}

static void camlayer_destroy(void *data)
{
    struct camlayer_filter *filter = data;
    if (!filter)
        return;

    obs_enter_graphics();
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
    if (filter->alpha_effect)
        filter->ellipse_parameter =
            gs_effect_get_param_by_name(filter->alpha_effect, "ellipse_enabled");
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
    obs_data_set_default_bool(settings, SETTING_DIAGNOSTIC, false);
    obs_data_set_default_int(settings, SETTING_AI_ENGINE, CAMLAYER_AI_MEDIAPIPE);
}

static obs_properties_t *camlayer_properties(void *unused)
{
    UNUSED_PARAMETER(unused);
    obs_properties_t *props = obs_properties_create();

    obs_property_t *engine = obs_properties_add_list(
        props, SETTING_AI_ENGINE, obs_module_text("CamLayer.AIEngine"),
        OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(engine, obs_module_text("CamLayer.EngineMediaPipe"),
                              CAMLAYER_AI_MEDIAPIPE);
    obs_property_list_add_int(engine, obs_module_text("CamLayer.EngineRVM"),
                              CAMLAYER_AI_RVM);
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
    obs_properties_add_text(props, "bootstrap_info",
                            obs_module_text("CamLayer.BootstrapNotice"),
                            OBS_TEXT_INFO);
    return props;
}

static void camlayer_render(void *data, gs_effect_t *unused)
{
    UNUSED_PARAMETER(unused);
    struct camlayer_filter *filter = data;
    if (!filter || !filter->alpha_effect)
        return;

    if (!obs_source_process_filter_begin(filter->source, GS_RGBA,
                                          OBS_ALLOW_DIRECT_RENDERING))
        return;

    /* Both selections intentionally use the same safe pass-through/ellipse
     * renderer until capture, ONNX inference, and mask upload are implemented.
     * Never claim that switching the selector activates either model.
     */
    gs_effect_set_bool(filter->ellipse_parameter, filter->diagnostic);
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
    return "CamLayer OBS filter, GPU alpha diagnostic (AI not implemented)";
}
