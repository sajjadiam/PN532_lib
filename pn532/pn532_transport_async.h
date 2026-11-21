/**
 * @file pn532_transport_async.h
 * @brief Portable, cooperative nonblocking PN532 frame transport contract.
 *
 * @details This is a migration API. Existing synchronous transport remains
 * available until asynchronous HSU and its tests replace it.
 *
 * No function in this interface may wait for UART bytes, transmission,
 * hardware reset, DMA, ISR completion, or timeouts. State advancement
 * happens when poll functions are called from a task/main loop.
 */
#ifndef PN532_TRANSPORT_ASYNC_H
#define PN532_TRANSPORT_ASYNC_H

#include "pn532_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Asynchronous operation progress. */
typedef enum {
    PN532_ASYNC_IDLE = 0, /**< No operation is active. */
    PN532_ASYNC_BUSY,     /**< Operation accepted, not yet complete. */
    PN532_ASYNC_DONE,     /**< Operation completed successfully. */
    PN532_ASYNC_FAILED   /**< Operation ended with an error. */
} pn532_async_state_t;

/** @brief Atomic task-context snapshot of an asynchronous operation. */
typedef struct {
    pn532_async_state_t state; /**< Current operation state. */
    pn532_status_t status;     /**< Error on FAILED; OK otherwise. */
} pn532_async_result_t;

/**
 * @brief Nonblocking frame transport operations.
 *
 * @details Each instance supports at most one TX and one RX operation.
 * The Core serializes command transactions; adapters may reject conflicts
 * with PN532_STATUS_BUSY. The Core must arm RX before starting TX.
 *
 * start_tx returns OK on acceptance only; the caller's TX data must
 * remain unchanged until poll_tx returns DONE or FAILED and the
 * underlying UART has stopped referencing it.
 *
 * start_rx accepts a caller-owned buffer to be filled with exactly one
 * canonical PN532 frame (including header and postamble). The adapter
 * sets frame->length only on DONE. On FAILED it sets length to zero.
 *
 * poll_tx / poll_rx must return immediately without blocking. Each
 * poll_rx invocation may consume at most the adapter's configured
 * RX byte budget. Completion is observed only in task context.
 *
 * abort is nonblocking. A successful return means the cancellation
 * request was accepted, NOT that DMA has stopped or that PN532 did
 * not execute the command. Buffer ownership remains until the
 * corresponding poll reports a terminal result.
 *
 * There is no implicit retry. Timeout does not imply no device action.
 * Error recovery and RX flushing belong to the adapter/platform.
 */
typedef struct {
    pn532_status_t (*start_tx)(
        void *context, const pn532_tx_frame_t *frame);
    pn532_async_result_t (*poll_tx)(void *context);
    pn532_status_t (*start_rx)(
        void *context, pn532_rx_frame_t *frame);
    pn532_async_result_t (*poll_rx)(void *context);
    pn532_status_t (*abort)(void *context);
} pn532_async_transport_ops_t;

/** @brief Transport vtable and per-device state binding. */
typedef struct {
    const pn532_async_transport_ops_t *ops; /**< Nonblocking operations. */
    void *context;                           /**< Instance-specific state. */
} pn532_async_transport_t;

#ifdef __cplusplus
}
#endif

#endif /* PN532_TRANSPORT_ASYNC_H */
