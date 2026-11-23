#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* C ABI shared by the OBS C plugin and the ONNX C++ implementation.
 * A model must be initialized before inference. All functions are noexcept.
 */
typedef struct camlayer_ai_session camlayer_ai_session;
camlayer_ai_session *camlayer_ai_open(const char *model_path, char *error, size_t error_size);
void camlayer_ai_close(camlayer_ai_session *session);
/* Converts a packed BGRA frame to the model's 1x3x256x256 RGB tensor.
 * Writes 256x256 floating-point alpha values; not thread safe per session.
 * Returns 1 on success. Intended for a future background inference worker.
 */
int camlayer_ai_segment_bgra(camlayer_ai_session *session, const uint8_t *bgra,
                            uint32_t width, uint32_t height, uint32_t stride,
                            float *alpha, size_t alpha_capacity);
#ifdef __cplusplus
}
#endif
