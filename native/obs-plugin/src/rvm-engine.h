// Native ONNX Robust Video Matting MobileNetV3 backend.
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct camlayer_rvm_session camlayer_rvm_session;
// Official RVM MobileNetV3 FP32 ONNX model. Supports 256, 384 and 512 square
// input/output masks. Recurrent states belong exclusively to this session.
camlayer_rvm_session *camlayer_rvm_open(const char *model_path,
                                        char *error, size_t error_capacity);
int camlayer_rvm_segment_bgra(camlayer_rvm_session *session,
                              const uint8_t *bgra, uint32_t stride,
                              float *alpha, size_t capacity, uint32_t side);
void camlayer_rvm_reset(camlayer_rvm_session *session);
void camlayer_rvm_close(camlayer_rvm_session *session);
#ifdef __cplusplus
}
#endif
