/**
 * @file pn532_transport.h
 * @brief Portable nonblocking PN532 frame transport interface.
 *
 * @details Defines the single asynchronous transport contract for PN532.
 * All callbacks are bounded and must return without waiting for hardware.
 * Each device instance has its own transport context and caller-owned buffers.
 */
#ifndef PN532_TRANSPORT_H
#define PN532_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Shared operation and transport error codes. */
typedef enum {
    PN532_STATUS_OK = 0,           /**< Accepted or completed successfully. */
    PN532_STATUS_INVALID_ARGUMENT, /**< Invalid pointer or descriptor. */
    PN532_STATUS_TIMEOUT,          /**< Deadline expired. */
    PN532_STATUS_IO_ERROR,         /**< Hardware communication failure. */
    PN532_STATUS_BUFFER_TOO_SMALL, /**< Receive buffer cannot hold the frame. */
    PN532_STATUS_FRAME_ERROR,      /**< Malformed frame or framing lost. */
    PN532_STATUS_BUSY,             /**< Operation already active. */
    PN532_STATUS_CANCELLED,        /**< Accepted operation cancelled. */
    PN532_STATUS_UNSUPPORTED,      /**< Optional feature unavailable. */
    PN532_STATUS_OVERRUN           /**< UART RX bytes were lost. */
} pn532_status_t;

/**
 * @brief Frame transmission request.
 *
 * @note The caller owns data and must not change or release it until
 * poll_tx reports a terminal state, including after abort requests.
 */
typedef struct {
    const uint8_t *data; /**< Complete canonical PN532 frame. */
    uint16_t length;       /**< Number of bytes. */
    uint32_t timeout_ms; /**< TX deadline relative to acceptance. */
} pn532_tx_frame_t;

/**
 * @brief Frame receive request and caller-owned destination.
 *
 * @note The descriptor and data must remain valid until poll_rx
 * reports a terminal state. length is set only on successful completion.
 */
typedef struct {
    uint8_t *data;       /**< Complete canonical PN532 frame destination. */
    uint16_t capacity;     /**< Destination capacity. */
    uint16_t length;       /**< Received length; output. */
    uint32_t timeout_ms; /**< RX deadline relative to acceptance. */
} pn532_rx_frame_t;

/** @brief Progress of one asynchronous operation. */
typedef enum {
    PN532_ASYNC_IDLE = 0, /**< No operation accepted. */
    PN532_ASYNC_BUSY,     /**< Accepted, still in progress. */
    PN532_ASYNC_DONE,     /**< Finished successfully. */
    PN532_ASYNC_FAILED    /**< Terminal error or cancellation. */
} pn532_async_state_t;

/** @brief State and status observed from task/main-loop context. */
typedef struct {
    pn532_async_state_t state; /**< Current operation state. */
    pn532_status_t status;     /**< Failure reason, otherwise OK. */
} pn532_async_result_t;

/**
 * @brief Nonblocking frame transport operations for one PN532 instance.
 *
 * @details Each instance supports at most one active TX and one active RX.
 * The command Core must arm RX before starting TX to avoid losing an ACK.
 *
 * start_tx returns OK when the transport accepts a request; this does
 * NOT mean that the final UART bit has been transmitted. poll_tx returns
 * DONE only after hardware transmission is complete (UART TC).
 *
 * start_rx receives exactly one canonical frame. poll_rx consumes a
 * bounded amount of data per invocation and never waits for new bytes.
 * The transport must preserve already-queued bytes between two frames,
 * including a back-to-back ACK and response.
 *
 * abort requests cancellation without waiting; caller-owned TX memory
 * remains in use until poll_tx reaches a terminal state. Aborting does
 * not guarantee that the PN532 has not executed the command.
 *
 * flush_rx is a nonblocking, local receive-queue purge for recovery.
 * It discards buffered UART bytes, clears receive framing/error state,
 * and leaves TX untouched. Call only when TX and RX are both terminal
 * (no accepted operation is still busy); otherwise it returns BUSY.
 * On failure, receive synchronization is not guaranteed restored.
 * It must NOT be used between a valid ACK and its response, since the
 * response may already be queued. A successful flush does not cancel
 * PN532 execution or prove application-level synchronization.
 *
 * These operations do not validate ACK/response command sequencing.
 * Core handles that protocol and command-specific deadlines.
 *
 * @warning Platform callbacks must be nonblocking. No callback is
 * invoked from the PN532 library's ISR; ISR synchronization is the
 * responsibility of the platform UART implementation.
 */
typedef struct {
    pn532_status_t (*start_tx)(void *context, const pn532_tx_frame_t *frame);
    pn532_async_result_t (*poll_tx)(void *context);
    pn532_status_t (*start_rx)(void *context, pn532_rx_frame_t *frame);
    pn532_async_result_t (*poll_rx)(void *context);
    pn532_status_t (*abort)(void *context);
    pn532_status_t (*flush_rx)(void *context); /**< Idle-only RX purge for recovery. */
} pn532_transport_ops_t;

/** @brief Transport binding for one independent PN532 instance. */
typedef struct {
    const pn532_transport_ops_t *ops; /**< Frame transport operations. */
    void *context;                    /**< Instance-specific transport state. */
} pn532_transport_t;

#ifdef __cplusplus
}
#endif

#endif /* PN532_TRANSPORT_H */
