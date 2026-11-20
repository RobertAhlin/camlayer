/*
 * CamLayer native OBS filter: alpha-compositing diagnostic milestone.
 *
 * The optional ellipse test is NOT an AI person mask. It validates conversion
 * from camera pixel formats to BGRA and transmission of a genuine alpha
 * channel through the OBS async video-filter pipeline.
 */
#include <obs-module.h>
#include <util/bmem.h>
#include <media-io/video-scaler.h>
#include <math.h>
#include <string.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("camlayer-obs", "en-US")

#define CAMLAYER_FILTER_ID "camlayer_person_cutout"
#define SETTING_THRESHOLD "mask_threshold"
#define SETTING_SOFTNESS "edge_softness"
#define SETTING_CLEANUP "edge_cleanup"
#define SETTING_STABILITY "edge_stability"
#define SETTING_DIAGNOSTIC "diagnostic_alpha"

struct camlayer_filter {
    obs_source_t *context;
    double threshold;
    double softness;
    double cleanup;
    double stability;
    bool diagnostic;

    struct obs_source_frame *output_frame;
    video_scaler_t *scaler;
    enum video_format previous_format;
    uint32_t previous_width;
    uint32_t previous_height;
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
}

static void camlayer_reset_buffers(struct camlayer_filter *filter)
{
    if (filter->scaler) {
        video_scaler_destroy(filter->scaler);
        filter->scaler = NULL;
    }
    obs_source_frame_destroy(filter->output_frame);
    filter->output_frame = NULL;
    filter->previous_width = 0;
    filter->previous_height = 0;
    filter->previous_format = VIDEO_FORMAT_NONE;
}

static void *camlayer_create(obs_data_t *settings, obs_source_t *source)
{
    struct camlayer_filter *filter = bzalloc(sizeof(*filter));
    filter->context = source;
    camlayer_update(filter, settings);
    blog(LOG_INFO, "[CamLayer] Native filter instance created");
    return filter;
}

static void camlayer_destroy(void *data)
{
    struct camlayer_filter *filter = data;
    if (!filter)
        return;

    camlayer_reset_buffers(filter);
    bfree(filter);
}

static void camlayer_defaults(obs_data_t *settings)
{
    obs_data_set_default_double(settings, SETTING_THRESHOLD, 0.50);
    obs_data_set_default_double(settings, SETTING_SOFTNESS, 0.0);
    obs_data_set_default_double(settings, SETTING_CLEANUP, 0.0);
    obs_data_set_default_double(settings, SETTING_STABILITY, 1.0);
    obs_data_set_default_bool(settings, SETTING_DIAGNOSTIC, false);
}

static obs_properties_t *camlayer_properties(void *unused)
{
    UNUSED_PARAMETER(unused);
    obs_properties_t *props = obs_properties_create();

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

static bool camlayer_prepare_buffers(struct camlayer_filter *filter,
                                    const struct obs_source_frame *frame)
{
    if (filter->output_frame && filter->previous_format == frame->format &&
        filter->previous_width == frame->width &&
        filter->previous_height == frame->height) {
        return true;
    }

    camlayer_reset_buffers(filter);

    if (frame->width == 0 || frame->height == 0 || frame->width > 8192 ||
        frame->height > 8192) {
        return false;
    }

    filter->output_frame =
        obs_source_frame_create(VIDEO_FORMAT_BGRA, frame->width, frame->height);
    if (!filter->output_frame || !filter->output_frame->data[0]) {
        camlayer_reset_buffers(filter);
        return false;
    }

    if (frame->format != VIDEO_FORMAT_BGRA) {
        struct video_scale_info input = {
            .format = frame->format,
            .width = frame->width,
            .height = frame->height,
            .colorspace = VIDEO_CS_DEFAULT,
            .range = frame->full_range ? VIDEO_RANGE_FULL : VIDEO_RANGE_PARTIAL
        };
        struct video_scale_info output = {
            .format = VIDEO_FORMAT_BGRA,
            .width = frame->width,
            .height = frame->height,
            .colorspace = VIDEO_CS_DEFAULT,
            .range = VIDEO_RANGE_FULL
        };
        int result = video_scaler_create(&filter->scaler, &output, &input,
                                         VIDEO_SCALE_FAST_BILINEAR);
        if (result != VIDEO_SCALER_SUCCESS) {
            blog(LOG_WARNING, "[CamLayer] Cannot convert video format %d",
                 (int)frame->format);
            camlayer_reset_buffers(filter);
            return false;
        }
    }

    filter->previous_format = frame->format;
    filter->previous_width = frame->width;
    filter->previous_height = frame->height;
    blog(LOG_INFO, "[CamLayer] BGRA diagnostic frame %ux%u, input format %d",
         frame->width, frame->height, (int)frame->format);
    return true;
}

static bool camlayer_copy_to_bgra(struct camlayer_filter *filter,
                                 const struct obs_source_frame *input)
{
    struct obs_source_frame *output = filter->output_frame;
    if (input->format == VIDEO_FORMAT_BGRA) {
        if (!input->data[0] || input->linesize[0] < input->width * 4 ||
            output->linesize[0] < input->width * 4) {
            return false;
        }
        for (uint32_t y = 0; y < input->height; ++y) {
            memcpy(output->data[0] + y * output->linesize[0],
                   input->data[0] + y * input->linesize[0],
                   (size_t)input->width * 4);
        }
        return true;
    }

    if (!filter->scaler)
        return false;

    return video_scaler_scale(filter->scaler, output->data, output->linesize,
                              (const uint8_t *const *)input->data, input->linesize);
}

/* Temporary geometry. Later the real model's confidence mask replaces this. */
static void camlayer_diagnostic_alpha(struct obs_source_frame *frame)
{
    const float inv_rx = 1.0f / (0.34f * (float)frame->width);
    const float inv_ry = 1.0f / (0.48f * (float)frame->height);
    const float center_x = 0.5f * (float)frame->width;
    const float center_y = 0.54f * (float)frame->height;

    for (uint32_t y = 0; y < frame->height; ++y) {
        uint8_t *row = frame->data[0] + (size_t)y * frame->linesize[0];
        const float dy = ((float)y - center_y) * inv_ry;
        for (uint32_t x = 0; x < frame->width; ++x) {
            const float dx = ((float)x - center_x) * inv_rx;
            const float distance = sqrtf(dx * dx + dy * dy);
            float alpha = (1.02f - distance) / 0.04f;
            if (alpha < 0.0f) alpha = 0.0f;
            if (alpha > 1.0f) alpha = 1.0f;
            /* Diagnostic input camera is opaque. Preserve original RGB. */
            row[(size_t)x * 4 + 3] = (uint8_t)(alpha * 255.0f + 0.5f);
        }
    }
}

static struct obs_source_frame *camlayer_filter_video(void *data,
                                                       struct obs_source_frame *frame)
{
    struct camlayer_filter *filter = data;
    if (!filter || !frame || !filter->diagnostic)
        return frame;

    if (!camlayer_prepare_buffers(filter, frame))
        return frame;

    if (!camlayer_copy_to_bgra(filter, frame))
        return frame;

    struct obs_source_frame *output = filter->output_frame;
    output->timestamp = frame->timestamp;
    output->flip = frame->flip;
    output->full_range = true;
    output->trc = frame->trc;
    output->max_luminance = frame->max_luminance;
    camlayer_diagnostic_alpha(output);
    return output;
}

static struct obs_source_info camlayer_filter_info = {
    .id = CAMLAYER_FILTER_ID,
    .type = OBS_SOURCE_TYPE_FILTER,
    .output_flags = OBS_SOURCE_ASYNC_VIDEO,
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
    blog(LOG_INFO, "[CamLayer] Native alpha diagnostic filter registered");
    return true;
}

const char *obs_module_description(void)
{
    return "CamLayer OBS person segmentation (alpha diagnostic, no AI yet)";
}
