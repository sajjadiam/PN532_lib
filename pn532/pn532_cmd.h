/**
 * @file pn532_cmd.h
 * @brief Small, nonblocking PN532 command helpers.
 *
 * @details The command Core owns framing and callback delivery; helpers
 * only validate command parameters and submit protocol data bytes.
 */
#ifndef PN532_CMD_H
#define PN532_CMD_H

#include "pn532.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief ISO14443-A @106 kbps target discovery (UM0701-02 section 7.3.5). */
typedef struct {
    uint8_t max_targets;             /**< One or two targets per PN532 command. */
    uint32_t response_timeout_ms;    /**< Deadline after ACK, 1..INT32_MAX. */
    pn532_complete_fn on_complete;   /**< Mandatory Core completion callback. */
    void *user;                      /**< Callback context. */
} pn532_cmd_type_a_request_t;

/**
 * @brief Submit InListPassiveTarget (D4 4A MaxTg 00) asynchronously.
 *
 * @details Core copies the 3-byte command before this call returns.
 * Completion.response starts with NbTg (the D5 4B response code was
 * already removed by Core). Does not poll, interpret a tag, or block.
 *
 * @param[in,out] device Initialized Core instance.
 * @param[in] request Count, timeout, and terminal notification.
 * @return pn532_submit() status; no callback is queued on failure.
 */
pn532_status_t pn532_cmd_list_type_a(
    pn532_t *device,
    const pn532_cmd_type_a_request_t *request
);


/** @brief InDataExchange PD0 (D4 40), UM0701-02 section 7.3.8. */
#define PN532_CMD_IN_DATA_EXCHANGE 0x40U

/** @brief Maximum DataOut / DataIn bytes per exchange, excluding headers. */
#define PN532_CMD_EXCHANGE_MAX_DATA 262U

/**
 * @brief Submit a single, unchained InDataExchange with no scratch buffer.
 *
 * @details command->data must already contain:
 *   [0x40, Tg (0x01 or 0x02), DataOut (0..262 bytes)].
 * The Core copies this byte sequence into its configured TX frame storage
 * synchronously before returning. Caller can reuse command->data afterwards.
 *
 * Reuses pn532_command_t to avoid another request structure and 264-byte
 * temporary stack buffer. This initial helper does not initiate outbound
 * MI chaining; advanced chained exchanges may use pn532_submit() directly.
 * The selected Tg must have been activated (e.g. InListPassiveTarget).
 *
 * Completion.result == PN532_RESULT_OK means a valid host response frame,
 * NOT that the RF operation succeeded: always parse its Status byte.
 */
pn532_status_t pn532_cmd_in_data_exchange(
    pn532_t *device,
    const pn532_command_t *command
);

/**
 * @brief Zero-copy view of a D5 41 payload returned by the command Core.
 *
 * @note Status bits: 0..5 error, 6 More Information, 7 NAD present.
 * No automatic chain assembly is performed. Application must handle
 * More Information when exchanging with ISO-DEP / DEP targets.
 * data references Core's RX storage and has the same lifetime as the
 * pn532_completion_t response pointer (until the next accepted command).
 */
typedef struct {
    uint8_t error_code;
    bool more_information;
    bool nad_present;
    const uint8_t *data;
    uint16_t length;
} pn532_cmd_exchange_view_t;

typedef enum {
    PN532_CMD_EXCHANGE_OK = 0,         /**< No PN532 RF error. */
    PN532_CMD_EXCHANGE_RF_ERROR,       /**< PN532 status bits 0..5 nonzero. */
    PN532_CMD_EXCHANGE_MALFORMED,      /**< Missing status or excess bytes. */
    PN532_CMD_EXCHANGE_INVALID_ARGUMENT
} pn532_cmd_exchange_result_t;

/**
 * @brief Decode Status + DataIn after the Core has removed response PD0.
 *
 * @param[in] response completion.response from a successful D5 41 frame.
 * @param[in] length completion.response_length.
 * @param[out] view Output is cleared before any validation; on RF_ERROR
 *                  it still exposes error_code and Status flags.
 * @return OK, RF_ERROR, MALFORMED, or INVALID_ARGUMENT.
 *
 * @pre completion.result == PN532_RESULT_OK; never parse a failed Core
 *      completion or confuse PN532 RF status with a transport error.
 */
pn532_cmd_exchange_result_t pn532_cmd_parse_exchange(
    const uint8_t *response,
    uint16_t length,
    pn532_cmd_exchange_view_t *view
);

#ifdef __cplusplus
}
#endif

#endif /* PN532_CMD_H */
