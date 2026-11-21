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

#ifdef __cplusplus
}
#endif

#endif /* PN532_CMD_H */
