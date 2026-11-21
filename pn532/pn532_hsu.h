/**
 * @file pn532_hsu.h
 * @brief Nonblocking PN532 HSU adapter using a platform UART event backend.
 *
 * @details ISR and DMA are entirely platform-owned. This adapter is
 * cooperative, performs bounded work and never waits for UART events.
 */
#ifndef PN532_HSU_H
#define PN532_HSU_H

#include "pn532_transport.h"
#include "pn532_frame.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Snapshot returned by the nonblocking UART RX backend.
 *
 * @note count is the number of bytes copied to the supplied destination.
 *       When status is non-OK, count must be zero.
 */
typedef struct {
    pn532_status_t status; /**< OK or I/O error / overrun. */
    uint16_t count;          /**< Bytes copied; never greater than capacity. */
} pn532_hsu_rx_result_t;

/**
 * @brief Platform UART callbacks. All callbacks must return immediately.
 *
 * @details tx_start accepts caller-owned data without copying it; the
 * data must remain valid through physical UART transmission complete.
 * tx_poll returns DONE only after hardware TC (not DMA transfer complete).
 * tx_abort merely *requests* cancellation; tx_poll must eventually report
 * a terminal state before the source buffer may be released.
 *
 * rx_read drains only bytes already present (typically in an IRQ-driven
 * ring buffer). It may return zero on an empty queue. It must never
 * wait for new bytes. rx_flush discards queued stale RX bytes and clears
 * latched UART overrun/error. It must also return without waiting.
 *
 * now_ms is a free-running wrapping monotonic uint32_t millisecond clock.
 */
typedef struct {
    pn532_status_t (*tx_start)(void *context, const uint8_t *data, uint16_t length);
    pn532_async_result_t (*tx_poll)(void *context);
    pn532_status_t (*tx_abort)(void *context);
    pn532_hsu_rx_result_t (*rx_read)(void *context, uint8_t *data, uint16_t capacity);
    pn532_status_t (*rx_flush)(void *context);
    uint32_t (*now_ms)(void *context);
} pn532_hsu_io_t;

/**
 * @brief Caller-owned, independent asynchronous HSU transport instance.
 *
 * @warning Contents are private to the owner task. Do not mutate during
 * an active operation or from an ISR.
 */
typedef struct {
    const pn532_hsu_io_t *io; /**< Platform callback table. */
    void *context;                  /**< Platform context. */
    pn532_tx_frame_t tx;            /**< Active TX descriptor, shallow copy. */
    pn532_rx_frame_t *rx;           /**< Active caller-owned RX descriptor. */
    uint32_t tx_started;            /**< TX acceptance timestamp. */
    uint32_t rx_started;            /**< RX acceptance timestamp. */
    uint16_t rx_used;                 /**< Received prefix size. */
    uint16_t rx_expected;             /**< Expected complete frame size. */
    uint16_t rx_budget;               /**< Maximum RX bytes processed per poll. */
    uint8_t sync_a;                 /**< UART sync history. */
    uint8_t sync_b;                 /**< UART sync history. */
    bool sync_found;                /**< Start marker already found. */
    bool tx_abort_pending;          /**< Abort requested, wait for TC/stop. */
    pn532_status_t tx_abort_reason; /**< Pending error or cancellation code. */
    pn532_async_result_t tx_result; /**< Latest transmit state. */
    pn532_async_result_t rx_result; /**< Latest receive state. */
} pn532_hsu_t;

/**
 * @brief Initialize the HSU adapter without starting hardware operations.
 *
 * @param[out] device Caller-owned instance.
 * @param[in] io Complete nonblocking platform callback table.
 * @param[in] context Passed to all platform callbacks.
 * @param[in] rx_budget Number of UART RX bytes processed per poll, 1..275.
 * @retval PN532_STATUS_OK Initialized.
 * @retval PN532_STATUS_INVALID_ARGUMENT Missing callback or invalid budget.
 */
pn532_status_t pn532_hsu_init(pn532_hsu_t *device, const pn532_hsu_io_t *io, void *context, uint16_t rx_budget);

/** @brief Async transport operations; bind context to a device instance. */
extern const pn532_transport_ops_t pn532_hsu_transport_ops;

#ifdef __cplusplus
}
#endif

#endif /* PN532_HSU_H */
