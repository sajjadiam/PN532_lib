/**
 * @file pn532_multi.h
 * @brief Cooperative round-robin PN532 Type-A reader scheduler.
 *
 * @details Single MCU, multiple independently configured Core instances.
 * No heap, no blocking hardware operations and no server SDK dependency.
 * This scheduler serializes commands; it does NOT guarantee RF-field
 * isolation. Overlapping antennas need separate RF field arbitration.
 */
#ifndef PN532_MULTI_H
#define PN532_MULTI_H

#include "pn532_cmd.h"
#include "pn532_tag.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    pn532_t *core;             /**< Dedicated Core instance for this reader. */
    uint8_t id;                /**< Unique identifier within this MCU. */
    bool configured;           /**< Private manager flag. */
    bool suspended;            /**< Private manager flag on failure. */
} pn532_multi_reader_t;

typedef enum {
    PN532_MULTI_TAG = 0,
    PN532_MULTI_ERROR
} pn532_multi_event_type_t;

/** @brief Valid only during the application callback. */
typedef struct {
    pn532_multi_event_type_t type;
    uint8_t reader_id;
    pn532_result_t result;            /**< Core result; meaningful for errors. */
    pn532_status_t transport_status;  /**< Underlying transport status. */
    pn532_tag_type_a_t tag;          /**< Only meaningful for TAG events. */
} pn532_multi_event_t;

typedef void (*pn532_multi_event_fn)(
    void *context,
    const pn532_multi_event_t *event
);

/**
 * @brief Caller-supplied configuration for one MCU's reader array.
 *
 * @details max_passive_retries must be 0..254 (0 means one attempt;
 * 255 means infinite retry and is rejected). Prior to first polling each
 * reader is configured by RFConfiguration CfgItem 0x05. All readers must
 * already have separate initialized Core/HSU/UART instances.
 */
typedef struct {
    pn532_multi_reader_t *readers;
    uint8_t reader_count;
    uint8_t max_targets;           /**< 1 or 2 per polling command. */
    uint8_t max_passive_retries;   /**< 0..254; finite retry count. */
    uint32_t response_timeout_ms; /**< Applies to config and polling. */
    pn532_multi_event_fn on_event;
    void *context;
} pn532_multi_config_t;

/** @brief Caller-owned manager; contents implementation-owned. */
typedef struct {
    pn532_multi_config_t config;
    uint8_t next_index;
    uint8_t active_index;
    uint8_t phase;
    bool initialized;
} pn532_multi_t;

/**
 * @brief Validate and bind a reader set; all readers initially enabled.
 * @return OK or INVALID_ARGUMENT; does not perform hardware I/O.
 */
pn532_status_t pn532_multi_init(
    pn532_multi_t *manager,
    const pn532_multi_config_t *config
);

/**
 * @brief Advance at most one Core state or start at most one command.
 *
 * @details Exactly one reader is active in the scheduler at once.
 * Discovered tags generate one event per tag (UID copied). A malformed
 * response or command failure suspends only that reader, reporting one
 * ERROR event. No-tag responses produce no event.
 *
 * The caller calls this periodically in task context, never an ISR.
 * Do not re-enter this manager or its Core APIs from on_event.
 * No automatic recovery or retry of a failed application command occurs.
 */
void pn532_multi_process(pn532_multi_t *manager);

/**
 * @brief Re-enable a suspended reader after external Core recovery.
 *
 * @details RFConfiguration is repeated before the next scan.
 * If the Core is still recovery-locked, the next command submission will
 * suspend the reader again. Cannot resume a reader currently in flight.
 */
pn532_status_t pn532_multi_resume(
    pn532_multi_t *manager,
    uint8_t reader_id
);

#ifdef __cplusplus
}
#endif

#endif /* PN532_MULTI_H */
