#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct camlayer_worker camlayer_worker;
enum camlayer_backend {
    CAMLAYER_BACKEND_MEDIAPIPE = 0,
    CAMLAYER_BACKEND_RVM = 1
};
enum camlayer_worker_status {
    CAMLAYER_WORKER_STARTING = 0,
    CAMLAYER_WORKER_READY = 1,
    CAMLAYER_WORKER_FAILED = 2,
    CAMLAYER_WORKER_UNAVAILABLE = 3
};

/* The background thread owns model init, inference, and teardown.
 * Engine selection is immutable during a worker lifetime; destroy/create
 * a worker to switch, resetting any model-specific temporal state.
 */
camlayer_worker *camlayer_worker_create(int backend, const char *model_path);
void camlayer_worker_destroy(camlayer_worker *worker);

/* Copies and reduces a BGRA image to 256x256 in the caller, then replaces
 * any pending frame with the newest image. This never runs model inference
 * in the caller; returns 0 on invalid input or unavailable worker.
 */
int camlayer_worker_submit(camlayer_worker *worker, const uint8_t *bgra,
                           uint32_t width, uint32_t height, uint32_t stride,
                           uint64_t source_timestamp);

/* Returns the newest completed mask only once, 0 if no newer result.
 * Caller owns the destination buffer (256*256 float entries).
 */
int camlayer_worker_take_mask(camlayer_worker *worker, float *dst,
                              size_t capacity, uint64_t *source_timestamp);
/* Thread-safe live settings. Independent per camera/filter instance. */
void camlayer_worker_set_refinement(camlayer_worker *worker, float cleanup,
                                    float stability, float follow_speed);
/* Clears temporal state after camera disconnect or geometry change. */
void camlayer_worker_reset_refinement(camlayer_worker *worker);
int camlayer_worker_get_status(camlayer_worker *worker);
#ifdef __cplusplus
}
#endif
