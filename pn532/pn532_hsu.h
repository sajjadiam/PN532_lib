/**
 * @file pn532_hsu.h
 * @brief Synchronous, portable PN532 high-speed UART (HSU) adapter.
 *
 * @details
 * Adapts a platform's exact-length UART read/write functions to the
 * frame-oriented PN532 transport interface. The UART interface uses raw
 * canonical PN532 frames (NXP UM0701-02, section 6.2.3).
 *
 * The platform owns the UART instance and the monotonically incrementing
 * millisecond counter. No dynamic allocation or global device state is used.
 * This module does not automatically wake a sleeping PN532, reset hardware,
 * retry commands, or check frame DCS. Explicit wake-up is available when
 * a platform delay callback is provided.
 */
#ifndef PN532_HSU_H
#define PN532_HSU_H

#include "pn532_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Platform UART write operation descriptor. */
typedef struct {
    const uint8_t *data; /**< Bytes to transmit. */
    size_t length;       /**< Exact requested byte count. */
    uint32_t timeout_ms; /**< Maximum time allowed for this operation. */
} pn532_hsu_tx_t;

/** @brief Platform UART read operation descriptor. */
typedef struct {
    uint8_t *data;       /**< Destination for received bytes. */
    size_t length;       /**< Exact requested byte count. */
    uint32_t timeout_ms; /**< Maximum time allowed for this operation. */
} pn532_hsu_rx_t;

/**
 * @brief Platform-specific blocking UART operations.
 *
 * @details
 * read_exact and write_exact must transfer exactly request->length bytes
 * before returning PN532_STATUS_OK. On partial transfer or timeout, return
 * PN532_STATUS_TIMEOUT or PN532_STATUS_IO_ERROR instead of success.
 * The requested time limit must be respected even if no data is available.
 * Implementations must not read ahead of the requested byte count.
 * now_ms is a monotonic free-running 32-bit millisecond counter; overflow
 * is supported. All individual operation timeouts are less than 2^31 ms.
 *
 * @warning After a partial UART read failure, remaining bytes may still
 *          be in the hardware RX FIFO. The next read attempts to locate
 *          the next frame marker, but false synchronization inside a
 *          corrupted payload remains possible. A platform may need to
 *          reset/flush RX explicitly when recovering severe link errors.
 */
typedef struct {
    pn532_status_t (*read_exact)(void *context, pn532_hsu_rx_t *request);
    pn532_status_t (*write_exact)(void *context, const pn532_hsu_tx_t *request);
    uint32_t (*now_ms)(void *context);
    void (*delay_ms)(void *context, uint32_t delay_ms); /**< Optional wake delay. */
} pn532_hsu_io_t;

/** @brief An independent, caller-owned HSU transport instance. */
typedef struct {
    const pn532_hsu_io_t *io; /**< Platform operations (must outlive instance). */
    void *context;            /**< Context passed to platform operations. */
} pn532_hsu_t;

/**
 * @brief Initialize the adapter instance without accessing hardware.
 * @param[out] hsu Caller-owned instance.
 * @param[in] io Valid platform I/O table.
 * @param[in] context Opaque platform context; may be NULL.
 * @retval PN532_STATUS_OK All mandatory callbacks were supplied.
 * @retval PN532_STATUS_INVALID_ARGUMENT Null instance or missing callback.
 */
pn532_status_t pn532_hsu_init(
    pn532_hsu_t *hsu,
    const pn532_hsu_io_t *io,
    void *context
);

/** @brief Explicit HSU wake-up parameters, without implicit delays. */
typedef struct {
    uint32_t tx_timeout_ms; /**< Timeout for the 0x55 dummy byte. */
    uint32_t settle_ms;     /**< Oscillator/firmware wake-up delay. */
} pn532_hsu_wakeup_t;

/**
 * @brief Wake an HSU-enabled PN532 after a permitted power-down mode.
 *
 * @details Writes the 0x55 dummy byte and waits settle_ms, as specified
 * by UM0701-02 section 7.2.11. Call before an ordinary command if the
 * PN532 may be sleeping and UART is an enabled wake-up source.
 * The delay is platform- and board-dependent; it is not hard-coded.
 *
 * @param[in] hsu Initialized HSU instance.
 * @param[in] wake Timeout and wake-up delay descriptor.
 * @retval PN532_STATUS_OK Wake sequence transmitted and delay completed.
 * @retval PN532_STATUS_INVALID_ARGUMENT Invalid input or missing delay.
 * @retval PN532_STATUS_TIMEOUT UART transmission timed out.
 * @retval PN532_STATUS_IO_ERROR UART transmission failed.
 * @note This operation does not confirm that the PN532 is awake. The next
 *       command/ACK exchange must establish that.
 */
pn532_status_t pn532_hsu_wakeup(
    pn532_hsu_t *hsu,
    const pn532_hsu_wakeup_t *wake
);

/**
 * @brief The transport operations table shared by HSU instances.
 *
 * @details Bind using { &pn532_hsu_transport_ops, &hsu_instance }.
 * The same HSU instance must not be accessed concurrently.
 */
extern const pn532_transport_ops_t pn532_hsu_transport_ops;

#ifdef __cplusplus
}
#endif

#endif /* PN532_HSU_H */
