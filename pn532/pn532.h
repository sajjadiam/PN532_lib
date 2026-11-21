/**
 * @file pn532.h
 * @brief Cooperative, nonblocking PN532 command Core public API.
 *
 * @details Provides one outstanding command per PN532 instance.
 * The Core owns command sequencing, ACK/NACK verification, response
 * matching and command deadlines. The transport owns UART byte I/O.
 *
 * No heap allocation, RTOS dependency or hardware-specific API is used.
 * Recovery is declared here; its cooperative implementation is pending.
 */
#ifndef PN532_H
#define PN532_H

#include "pn532_board.h"
#include "pn532_frame.h"
#include "pn532_transport.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Caller-allocated command Core instance. */
typedef struct pn532 pn532_t;

/** @brief Terminal result of one accepted command. */
typedef enum {
    PN532_RESULT_OK = 0,            /**< Valid matching response received. */
    PN532_RESULT_TX_TIMEOUT,        /**< UART TX did not finish in time. */
    PN532_RESULT_ACK_TIMEOUT,       /**< No valid ACK before its deadline. */
    PN532_RESULT_RESPONSE_TIMEOUT,  /**< No matching response in time. */
    PN532_RESULT_NACK,              /**< PN532 rejected the frame with NACK. */
    PN532_RESULT_DEVICE_ERROR,      /**< PN532 sent an application-error frame. */
    PN532_RESULT_FRAME_ERROR,       /**< Invalid received PN532 frame. */
    PN532_RESULT_UNEXPECTED_RESPONSE, /**< Unexpected response code or TFI. */
    PN532_RESULT_TRANSPORT_ERROR,   /**< Lower transport reported an error. */
    PN532_RESULT_CANCELLED          /**< Request cancelled by the host. */
} pn532_result_t;

/**
 * @brief Command completion delivered in task/main-loop context.
 *
 * @details On successful command completion, response points to bytes
 * AFTER the response code (PD0); response_length excludes PD0. If no
 * additional bytes exist, the pointer is NULL and the length is zero.
 * Recovery completion always has NULL response and zero length.
 *
 * The response points into caller-provided RX storage; it is valid until
 * the next accepted command or until the Core is reinitialized. Applications
 * needing longer retention must copy it. The completion descriptor itself
 * is valid only for the duration of the callback.
 *
 * transport_status is PN532_STATUS_OK when no lower-layer error occurred.
 * On transport failures it contains the underlying failure reason.
 */
typedef struct {
    pn532_result_t result;       /**< Final Core-level outcome. */
    pn532_status_t transport_status; /**< Lower-level error or OK. */
    const uint8_t *response;     /**< Response data after PD0 on success. */
    uint16_t response_length;    /**< Response data byte count. */
} pn532_completion_t;

/**
 * @brief Invoked exactly once for each accepted command or recovery request.
 *
 * @note Always invoked by pn532_process(), never from an ISR,
 * pn532_submit() or pn532_cancel(). Do not call Core APIs for the same
 * instance recursively from this callback.
 */
typedef void (*pn532_complete_fn)(
    void *user,
    const pn532_completion_t *completion
);

/**
 * @brief One PN532 command request.
 *
 * @details data starts at PD0 (command code), followed by any arguments.
 * Core synchronously encodes these bytes into its TX storage before
 * pn532_submit() returns OK. The original data need not remain valid
 * after a successful submit; the encoded TX storage must remain intact
 * until the transport confirms termination of TX.
 *
 * response_timeout_ms starts when a valid PN532 ACK has been received.
 */
typedef struct {
    const uint8_t *data;          /**< PD0 and command arguments. */
    uint16_t length;              /**< PD byte count, from 1 to 264. */
    uint32_t response_timeout_ms; /**< Response deadline after valid ACK. */
    pn532_complete_fn on_complete; /**< Required terminal callback. */
    void *user;                   /**< Opaque callback context. */
} pn532_command_t;

/**
 * @brief Nonblocking protocol recovery request.
 *
 * @details Recovery uses a host ACK frame to stop PN532 command processing,
 * waits for physical UART TC, purges stale RX bytes through the transport,
 * then validates synchronization using GetFirmwareVersion (0x02).
 * No previously accepted application command is automatically retried.
 *
 * response_timeout_ms is the verification command's response deadline,
 * starting after its valid ACK. TX and ACK deadlines are configured in
 * pn532_config_t. All nonzero timeouts must be below 2^31 ms.
 *
 * Recovery requires the transport's flush_rx operation. Hardware reset
 * and optional board RST/IRQ signaling are not part of this initial path.
 */
typedef struct {
    uint32_t response_timeout_ms; /**< Verification response deadline. */
    pn532_complete_fn on_complete; /**< Required terminal callback. */
    void *user;                    /**< Opaque callback context. */
} pn532_recovery_request_t;

/**
 * @brief Caller-provided contiguous frame storage.
 *
 * @details No buffers are allocated inside Core. TX storage holds one
 * encoded outbound frame; RX storage holds one complete inbound frame.
 * Both buffers must remain valid for the device lifetime.
 * A buffer of PN532_FRAME_MAX_SIZE supports maximum-size frames.
 * Smaller buffers may reject commands or responses as too large.
 */
typedef struct {
    uint8_t *data;     /**< Caller-owned writable memory. */
    uint16_t capacity; /**< Size in bytes. */
} pn532_storage_t;

/**
 * @brief Configuration of one command Core instance.
 *
 * @details transport operations and the clock callback are mandatory.
 * board may be NULL; individual RST and IRQ callbacks may also be NULL.
 * Optional GPIO functions do not replace UART RX and are not needed for
 * ordinary HSU communication.
 *
 * The 32-bit clock must be monotonic modulo 2^32; nonzero timeouts must
 * be below 2^31 ms. The Core copies the configuration on init, but all
 * referenced storage, transport state and board state must remain valid.
 */
typedef struct {
    pn532_transport_t transport;    /**< Async frame transport. */
    const pn532_board_io_t *board;  /**< Optional GPIO adapter. */
    uint32_t (*now_ms)(void *context); /**< Nonblocking time source. */
    void *clock_context;            /**< Clock callback context. */
    pn532_storage_t tx_storage;     /**< Encoded command buffer. */
    pn532_storage_t rx_storage;     /**< ACK/response frame buffer. */
    uint32_t tx_timeout_ms;         /**< Deadline for physical TX completion. */
    uint32_t ack_timeout_ms;        /**< Deadline starting after UART TC. */
} pn532_config_t;

/**
 * @brief Caller-allocated, single-owner command instance.
 *
 * @warning Fields are implementation-owned despite the public definition,
 * which permits static/stack allocation without malloc. Do not modify
 * them directly or access the same instance from an ISR or two tasks.
 */
struct pn532 {
    pn532_config_t config;              /**< Saved configuration. */
    pn532_tx_frame_t tx;                /**< Active encoded TX frame. */
    pn532_rx_frame_t rx;                /**< Active received raw frame. */
    pn532_complete_fn on_complete;      /**< Callback of accepted command. */
    void *user;                         /**< Callback context. */
    uint32_t response_timeout_ms;       /**< Active response deadline. */
    uint32_t phase_started_ms;          /**< Start of ACK/response phase. */
    pn532_result_t terminal_result;     /**< Deferred completion result. */
    pn532_status_t terminal_transport_status; /**< Deferred transport error. */
    uint8_t command_code;               /**< Submitted PD0. */
    uint8_t state;                      /**< Private state-machine encoding. */
    bool cancel_pending;                /**< Host cancellation requested. */
    bool recovery_required;             /**< Protocol synchronization lost. */
};

/**
 * @brief Initialize an idle Core instance without accessing hardware.
 *
 * @param[out] device Caller-allocated instance.
 * @param[in] config Complete configuration.
 * @retval PN532_STATUS_OK Initialized and idle.
 * @retval PN532_STATUS_INVALID_ARGUMENT Invalid configuration.
 *
 * @pre No transport TX or RX operation may be active for this instance.
 * @note This function does not reset or wake the PN532 hardware.
 */
pn532_status_t pn532_init(pn532_t *device, const pn532_config_t *config);

/**
 * @brief Accept one command without blocking on I/O.
 *
 * @details Validates and encodes the request into configured TX storage.
 * On OK, exactly one completion will later be delivered by process().
 * On any other return status, no completion callback is scheduled.
 *
 * @param[in,out] device Initialized instance.
 * @param[in] command Command including PD0 and arguments.
 * @retval PN532_STATUS_OK Command accepted, not yet completed.
 * @retval PN532_STATUS_BUSY Another command is still active.
 * @retval PN532_STATUS_BUFFER_TOO_SMALL Encoded frame will not fit.
 * @retval PN532_STATUS_INVALID_ARGUMENT Invalid command/configuration.
 * @retval PN532_STATUS_IO_ERROR Device requires protocol recovery.
 *
 * @note No application callback is made from this function.
 * @note A new command must not be submitted from within the completion
 * callback; submit after pn532_process() returns.
 */
pn532_status_t pn532_submit(
    pn532_t *device,
    const pn532_command_t *command
);

/**
 * @brief Advance one bounded cooperative step of command processing.
 *
 * @details Advances the active command or recovery operation without
 * waiting for UART input, DMA or elapsed deadlines.
 * Must be called regularly even when no IRQ is generated. RX processing
 * remains bounded by the HSU adapter's configured byte budget.
 *
 * A successful TX completion means physical UART TC, not DMA TC.
 * The ACK deadline begins after TX completion; the response deadline
 * begins after successful ACK validation.
 *
 * @param[in,out] device Initialized instance.
 * @note Call from one task/main-loop context, never from an ISR.
 * At most one completion callback is issued per invocation.
 */
void pn532_process(pn532_t *device);

/**
 * @brief Start asynchronous PN532 protocol recovery or explicit link check.
 *
 * @details Accepted only when no command or recovery operation is active.
 * Uses the PN532 host ACK cancellation frame, waits until UART transmission
 * is physically complete, flushes the local RX queue while transport is
 * idle, then validates the link with GetFirmwareVersion (0x02).
 *
 * The device is considered synchronized only after a valid matching
 * verification response. Failure keeps recovery_required asserted and
 * pn532_submit() blocked. Recovery success does not prove a previously
 * interrupted command had no application-level side effects.
 *
 * A successful acceptance schedules exactly one terminal callback,
 * delivered from pn532_process() in task context. The callback uses the
 * standard pn532_completion_t result/status fields, with response NULL
 * and response_length zero. Reentrant calls for the same instance from
 * inside the callback are not supported.
 *
 * @param[in,out] device Initialized, idle PN532 instance.
 * @param[in] request Recovery callback and verification response timeout.
 * @retval PN532_STATUS_OK Accepted; completion will be asynchronous.
 * @retval PN532_STATUS_BUSY Another operation is active.
 * @retval PN532_STATUS_INVALID_ARGUMENT Invalid instance or request.
 * @retval PN532_STATUS_UNSUPPORTED Transport lacks flush_rx.
 *
 * @note Does not call board reset_set() and does not retry application
 * commands. The transport must not have a pending TX buffer when idle.
 * @note Declared API; implementation will follow in pn532.c.
 */
pn532_status_t pn532_recover(
    pn532_t *device,
    const pn532_recovery_request_t *request
);

/**
 * @brief Request nonblocking cancellation of the active command.
 *
 * @details Cancellation may require additional calls to process()
 * before the transport stops referencing TX storage. A completion
 * callback with PN532_RESULT_CANCELLED is delivered once cancellation
 * becomes terminal. Cancellation cannot guarantee that PN532 did not
 * execute the command already sent.
 *
 * @param[in,out] device Initialized instance.
 * @retval PN532_STATUS_OK Cancellation request accepted, or already idle.
 * @retval PN532_STATUS_INVALID_ARGUMENT Null/uninitialized instance.
 *
 * @note Does not invoke the completion callback synchronously.
 */
pn532_status_t pn532_cancel(pn532_t *device);

#ifdef __cplusplus
}
#endif

#endif /* PN532_H */
