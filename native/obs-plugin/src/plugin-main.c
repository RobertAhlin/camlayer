/*
 * CamLayer native OBS filter bootstrap.
 *
 * This version intentionally passes video through unchanged. A future milestone
 * will integrate the segmentation runtime and generate an alpha channel.
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

struct camlayer_filter {
    obs_source_t *context;
    double threshold;
    double softness;
    double cleanup;
    double stability;
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
}

static void *camlayer_create(obs_data_t *settings, obs_source_t *source)
{
    struct camlayer_filter *filter = bzalloc(sizeof(*filter));
    filter->context = source;
    camlayer_update(filter, settings);
    blog(LOG_INFO, "[CamLayer] Native pass-through filter instance created");
    return filter;
}

static void camlayer_destroy(void *data)
{
    bfree(data);
}

static void camlayer_defaults(obs_data_t *settings)
{
    obs_data_set_default_double(settings, SETTING_THRESHOLD, 0.50);
    obs_data_set_default_double(settings, SETTING_SOFTNESS, 0.0);
    obs_data_set_default_double(settings, SETTING_CLEANUP, 0.0);
    obs_data_set_default_double(settings, SETTING_STABILITY, 1.0);
}

static obs_properties_t *camlayer_properties(void *unused)
{
    UNUSED_PARAMETER(unused);
    obs_properties_t *props = obs_properties_create();

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

static struct obs_source_frame *camlayer_filter_video(void *data,
                                                       struct obs_source_frame *frame)
{
    UNUSED_PARAMETER(data);

    /* Return the unmodified input frame. Camera formats may include NV12,
     * YUY2, and BGRA. We must not assume that there is an alpha channel.
     */
    return frame;
}

static struct obs_source_info camlayer_filter_info = {
    .id = CAMLAYER_FILTER_ID,
    .type = OBS_SOURCE_TYPE_FILTER,
    .output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_ASYNC,
    .get_name = camlayer_get_name,
    .create = camlayer_create,
    .destroy = camlayer_destroy,
    .update = camlayer_update,
    .get_defaults = camlayer_defaults,
    .get_properties = camlayer_properties,
    .filter_video = camlayer_filter_video,
};

bool obs_module_load(void)
{
    obs_register_source(&camlayer_filter_info);
    blog(LOG_INFO, "[CamLayer] Native OBS filter registered, no AI yet");
    return true;
}

const char *obs_module_description(void)
{
    return "CamLayer native person segmentation filter (bootstrap only)";
}
