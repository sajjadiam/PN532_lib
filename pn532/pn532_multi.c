/**
 * @file pn532_multi.c
 * @brief Bounded MCU-local PN532 reader round-robin controller.
 */
#include "pn532_multi.h"

#include <stddef.h>
#include <string.h>

#define MULTI_IDLE 0U
#define MULTI_CONFIGURE 1U
#define MULTI_DISCOVER 2U

/** @brief Notify one application event without retaining its storage. */
static void multi_notify(pn532_multi_t *manager,
                         const pn532_multi_event_t *event)
{
    manager->config.on_event(manager->config.context, event);
}

/** @brief Report a reader failure and prevent further automatic polling. */
static void multi_fail(pn532_multi_t *manager, pn532_result_t result,
                       pn532_status_t transport_status)
{
    pn532_multi_event_t event = {0};
    pn532_multi_reader_t *reader =
        &manager->config.readers[manager->active_index];

    reader->suspended = true;
    manager->next_index = (uint8_t)((manager->active_index + 1U) %
                                     manager->config.reader_count);
    event.type = PN532_MULTI_ERROR;
    event.reader_id = reader->id;
    event.result = result;
    event.transport_status = transport_status;
    multi_notify(manager, &event);
}

/** @brief Called from a Core completion, never from ISR. */
static void multi_completed(void *context, const pn532_completion_t *completion)
{
    pn532_multi_t *manager = (pn532_multi_t *)context;
    pn532_multi_reader_t *reader =
        &manager->config.readers[manager->active_index];
    const uint8_t previous_phase = manager->phase;
    pn532_tag_type_a_list_t parsed;
    uint8_t i;

    manager->phase = MULTI_IDLE;
    if (completion->result != PN532_RESULT_OK) {
        multi_fail(manager, completion->result,
                   completion->transport_status);
        return;
    }
    if (previous_phase == MULTI_CONFIGURE) {
        reader->configured = true;
        manager->next_index = manager->active_index;
        return;
    }
    if (previous_phase != MULTI_DISCOVER ||
        pn532_tag_parse_type_a(completion->response,
                               completion->response_length,
                               &parsed) != PN532_TAG_OK) {
        multi_fail(manager, PN532_RESULT_UNEXPECTED_RESPONSE,
                   PN532_STATUS_OK);
        return;
    }

    manager->next_index = (uint8_t)((manager->active_index + 1U) %
                                     manager->config.reader_count);
    for (i = 0U; i < parsed.count; ++i) {
        pn532_multi_event_t event = {0};
        event.type = PN532_MULTI_TAG;
        event.reader_id = reader->id;
        event.result = PN532_RESULT_OK;
        event.transport_status = PN532_STATUS_OK;
        event.tag = parsed.targets[i];
        multi_notify(manager, &event);
    }
}

pn532_status_t pn532_multi_init(pn532_multi_t *manager,
                                const pn532_multi_config_t *config)
{
    uint8_t seen[32] = {0};
    uint8_t i;

    if (manager == NULL || config == NULL || config->readers == NULL ||
        config->reader_count == 0U || config->max_targets == 0U ||
        config->max_targets > 2U || config->max_passive_retries == 0xFFU ||
        config->response_timeout_ms == 0U ||
        config->response_timeout_ms > UINT32_C(0x7FFFFFFF) ||
        config->on_event == NULL) {
        return PN532_STATUS_INVALID_ARGUMENT;
    }
    for (i = 0U; i < config->reader_count; ++i) {
        const uint8_t id = config->readers[i].id;
        const uint8_t index = (uint8_t)(id >> 3U);
        const uint8_t flag = (uint8_t)(1U << (id & 7U));
        uint8_t j;
        if (config->readers[i].core == NULL ||
            (seen[index] & flag) != 0U) {
            return PN532_STATUS_INVALID_ARGUMENT;
        }
        /* Distinct IDs must never alias the same Core transport state. */
        for (j = 0U; j < i; ++j) {
            if (config->readers[j].core == config->readers[i].core) {
                return PN532_STATUS_INVALID_ARGUMENT;
            }
        }
        seen[index] = (uint8_t)(seen[index] | flag);
    }

    memset(manager, 0, sizeof(*manager));
    manager->config = *config;
    for (i = 0U; i < config->reader_count; ++i) {
        config->readers[i].configured = false;
        config->readers[i].suspended = false;
    }
    manager->initialized = true;
    return PN532_STATUS_OK;
}

/** @brief One nonblocking RFConfiguration command with finite retries. */
static pn532_status_t multi_configure_reader(pn532_multi_t *manager,
                                             pn532_multi_reader_t *reader)
{
    const uint8_t data[] = {
        0x32U, 0x05U, 0xFFU, 0x01U,
        manager->config.max_passive_retries
    };
    const pn532_command_t command = {
        data,
        sizeof(data),
        manager->config.response_timeout_ms,
        multi_completed,
        manager
    };
    return pn532_submit(reader->core, &command);
}

static pn532_status_t multi_poll_reader(pn532_multi_t *manager,
                                        pn532_multi_reader_t *reader)
{
    const pn532_cmd_type_a_request_t request = {
        manager->config.max_targets,
        manager->config.response_timeout_ms,
        multi_completed,
        manager
    };
    return pn532_cmd_list_type_a(reader->core, &request);
}

void pn532_multi_process(pn532_multi_t *manager)
{
    uint8_t index;
    pn532_multi_reader_t *reader;
    pn532_status_t status;

    if (manager == NULL || !manager->initialized) return;
    if (manager->phase != MULTI_IDLE) {
        pn532_process(manager->config.readers[manager->active_index].core);
        return;
    }

    /* One scheduling decision per call, regardless of reader count. */
    index = manager->next_index;
    manager->next_index = (uint8_t)(
        ((unsigned int)index + 1U) % manager->config.reader_count);
    reader = &manager->config.readers[index];
    if (reader->suspended) return;

    manager->active_index = index;
    status = reader->configured
        ? multi_poll_reader(manager, reader)
        : multi_configure_reader(manager, reader);
    if (status == PN532_STATUS_OK) {
        manager->phase = reader->configured
            ? MULTI_DISCOVER : MULTI_CONFIGURE;
    } else if (status != PN532_STATUS_BUSY) {
        multi_fail(manager, PN532_RESULT_TRANSPORT_ERROR, status);
    }
}

pn532_status_t pn532_multi_resume(pn532_multi_t *manager,
                                  uint8_t reader_id)
{
    uint8_t i;

    if (manager == NULL || !manager->initialized) {
        return PN532_STATUS_INVALID_ARGUMENT;
    }
    for (i = 0U; i < manager->config.reader_count; ++i) {
        pn532_multi_reader_t *reader = &manager->config.readers[i];
        if (reader->id != reader_id) continue;
        if (manager->phase != MULTI_IDLE &&
            manager->active_index == i) {
            return PN532_STATUS_BUSY;
        }
        reader->configured = false;
        reader->suspended = false;
        manager->next_index = i;
        return PN532_STATUS_OK;
    }
    return PN532_STATUS_INVALID_ARGUMENT;
}
