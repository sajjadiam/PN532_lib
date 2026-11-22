/**
 * @file test_pn532_multi.c
 * @brief Deterministic scheduler test with a fake asynchronous Core.
 *
 * Link with pn532_multi.c, pn532_cmd.c, pn532_tag.c, without pn532.c.
 */
#include "pn532_multi.h"

#include <stdio.h>
#include <string.h>

static unsigned int checks;
static unsigned int failures;
#define CHECK(expr) do { ++checks; if (!(expr)) { ++failures; \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); } } while (0)

typedef struct {
    pn532_t core;
    uint8_t command[12];
    uint16_t command_length;
    pn532_complete_fn callback;
    void *user;
    pn532_result_t result;
    pn532_status_t transport_status;
    const uint8_t *reply;
    uint16_t reply_length;
    unsigned int submissions;
    unsigned int process_calls;
    bool pending;
    bool ready;
    bool locked;
} fake_core_t;

typedef struct {
    pn532_multi_event_t events[12];
    unsigned int count;
} observer_t;

pn532_status_t pn532_submit(pn532_t *core,
                            const pn532_command_t *command)
{
    fake_core_t *fake = (fake_core_t *)core;
    if (fake->pending) return PN532_STATUS_BUSY;
    if (fake->locked) return PN532_STATUS_IO_ERROR;
    if (command == NULL || command->length > sizeof(fake->command)) {
        return PN532_STATUS_INVALID_ARGUMENT;
    }
    memcpy(fake->command, command->data, command->length);
    fake->command_length = command->length;
    fake->callback = command->on_complete;
    fake->user = command->user;
    fake->pending = true;
    ++fake->submissions;
    return PN532_STATUS_OK;
}

void pn532_process(pn532_t *core)
{
    fake_core_t *fake = (fake_core_t *)core;
    ++fake->process_calls;
    if (fake->pending && fake->ready) {
        const pn532_completion_t event = {
            fake->result,
            fake->transport_status,
            fake->reply,
            fake->reply_length
        };
        fake->pending = false;
        fake->ready = false;
        fake->callback(fake->user, &event);
    }
}

static void observed(void *context, const pn532_multi_event_t *event)
{
    observer_t *observer = (observer_t *)context;
    CHECK(observer->count < 12U);
    if (observer->count < 12U) observer->events[observer->count++] = *event;
}

static void completed(fake_core_t *fake, pn532_result_t result,
                      const uint8_t *reply, uint16_t length)
{
    fake->result = result;
    fake->transport_status = PN532_STATUS_OK;
    fake->reply = reply;
    fake->reply_length = length;
    fake->ready = true;
}

static void test_round_robin(void)
{
    fake_core_t first = {0};
    fake_core_t second = {0};
    pn532_multi_t manager = {0};
    observer_t observer = {0};
    pn532_multi_reader_t readers[] = {
        {&first.core, 3U, false, false},
        {&second.core, 9U, false, false}
    };
    const pn532_multi_config_t config = {
        readers, 2U, 2U, 0U, 100U, observed, &observer
    };
    const uint8_t two_tags[] = {
        0x02U,
        0x01U, 0x04U, 0x00U, 0x08U, 0x04U,
        0x11U, 0x22U, 0x33U, 0x44U,
        0x02U, 0x04U, 0x00U, 0x08U, 0x04U,
        0xAAU, 0xBBU, 0xCCU, 0xDDU
    };
    const uint8_t no_tags[] = {0U};

    CHECK(pn532_multi_init(&manager, &config) == PN532_STATUS_OK);
    pn532_multi_process(&manager);
    CHECK(first.pending && !second.pending);
    CHECK(first.command_length == 5U);
    CHECK(first.command[0] == 0x32U && first.command[1] == 0x05U);
    CHECK(first.command[2] == 0xFFU && first.command[3] == 0x01U);
    CHECK(first.command[4] == 0U);
    completed(&first, PN532_RESULT_OK, NULL, 0U);
    pn532_multi_process(&manager);
    CHECK(readers[0].configured);
    pn532_multi_process(&manager);
    CHECK(first.command_length == 3U);
    CHECK(first.command[0] == 0x4AU && first.command[1] == 2U);
    completed(&first, PN532_RESULT_OK, two_tags, sizeof(two_tags));
    pn532_multi_process(&manager);
    CHECK(observer.count == 2U);
    CHECK(observer.events[0].type == PN532_MULTI_TAG);
    CHECK(observer.events[0].reader_id == 3U);
    CHECK(observer.events[0].tag.uid[0] == 0x11U);
    CHECK(observer.events[1].tag.uid[3] == 0xDDU);

    pn532_multi_process(&manager);
    CHECK(second.command[0] == 0x32U);
    completed(&second, PN532_RESULT_OK, NULL, 0U);
    pn532_multi_process(&manager);
    pn532_multi_process(&manager);
    CHECK(second.command[0] == 0x4AU);
    completed(&second, PN532_RESULT_OK, no_tags, sizeof(no_tags));
    pn532_multi_process(&manager);
    CHECK(observer.count == 2U);
    pn532_multi_process(&manager);
    CHECK(first.command[0] == 0x4AU);
    CHECK(first.submissions == 3U);
    CHECK(second.submissions == 2U);
}

static void test_reader_failure_and_resume(void)
{
    fake_core_t first = {0};
    fake_core_t second = {0};
    pn532_multi_t manager = {0};
    observer_t observer = {0};
    pn532_multi_reader_t readers[] = {
        {&first.core, 1U, false, false},
        {&second.core, 2U, false, false}
    };
    const pn532_multi_config_t config = {
        readers, 2U, 1U, 2U, 100U, observed, &observer
    };
    const uint8_t invalid[] = {3U};

    CHECK(pn532_multi_init(&manager, &config) == PN532_STATUS_OK);
    pn532_multi_process(&manager);
    CHECK(pn532_multi_resume(&manager, 1U) == PN532_STATUS_BUSY);
    completed(&first, PN532_RESULT_OK, NULL, 0U);
    pn532_multi_process(&manager);
    pn532_multi_process(&manager);
    completed(&first, PN532_RESULT_OK, invalid, sizeof(invalid));
    pn532_multi_process(&manager);
    CHECK(readers[0].suspended);
    CHECK(observer.count == 1U);
    CHECK(observer.events[0].type == PN532_MULTI_ERROR);
    CHECK(observer.events[0].reader_id == 1U);
    CHECK(observer.events[0].result == PN532_RESULT_UNEXPECTED_RESPONSE);

    pn532_multi_process(&manager);
    CHECK(second.pending);
    CHECK(second.command[0] == 0x32U);
    completed(&second, PN532_RESULT_OK, NULL, 0U);
    pn532_multi_process(&manager);
    pn532_multi_process(&manager);
    completed(&second, PN532_RESULT_ACK_TIMEOUT, NULL, 0U);
    pn532_multi_process(&manager);
    CHECK(readers[1].suspended);
    CHECK(observer.count == 2U);
    CHECK(observer.events[1].reader_id == 2U);
    CHECK(observer.events[1].result == PN532_RESULT_ACK_TIMEOUT);

    first.locked = true;
    CHECK(pn532_multi_resume(&manager, 1U) == PN532_STATUS_OK);
    pn532_multi_process(&manager);
    CHECK(readers[0].suspended);
    CHECK(observer.count == 3U);
    CHECK(observer.events[2].transport_status == PN532_STATUS_IO_ERROR);
    first.locked = false;
    CHECK(pn532_multi_resume(&manager, 1U) == PN532_STATUS_OK);
    pn532_multi_process(&manager);
    CHECK(first.pending && first.command[0] == 0x32U);
    CHECK(pn532_multi_resume(&manager, 42U) == PN532_STATUS_INVALID_ARGUMENT);
}

static void test_arguments(void)
{
    fake_core_t first = {0};
    fake_core_t second = {0};
    observer_t observer = {0};
    pn532_multi_t manager = {0};
    pn532_multi_reader_t readers[] = {
        {&first.core, 7U, false, false},
        {&second.core, 7U, false, false}
    };
    pn532_multi_config_t config = {
        readers, 2U, 1U, 0U, 100U, observed, &observer
    };

    CHECK(pn532_multi_init(NULL, &config) == PN532_STATUS_INVALID_ARGUMENT);
    CHECK(pn532_multi_init(&manager, NULL) == PN532_STATUS_INVALID_ARGUMENT);
    CHECK(pn532_multi_init(&manager, &config) == PN532_STATUS_INVALID_ARGUMENT);
    readers[1].id = 8U;
    readers[1].core = &first.core;
    CHECK(pn532_multi_init(&manager, &config) == PN532_STATUS_INVALID_ARGUMENT);
    readers[1].core = &second.core;
    config.max_targets = 3U;
    CHECK(pn532_multi_init(&manager, &config) == PN532_STATUS_INVALID_ARGUMENT);
    config.max_targets = 1U;
    config.max_passive_retries = 0xFFU;
    CHECK(pn532_multi_init(&manager, &config) == PN532_STATUS_INVALID_ARGUMENT);
    config.max_passive_retries = 0U;
    config.response_timeout_ms = 0U;
    CHECK(pn532_multi_init(&manager, &config) == PN532_STATUS_INVALID_ARGUMENT);
    config.response_timeout_ms = 100U;
    CHECK(pn532_multi_init(&manager, &config) == PN532_STATUS_OK);
}

int main(void)
{
    test_round_robin();
    test_reader_failure_and_resume();
    test_arguments();
    printf("PN532 multi: %u checks, %u failures\n", checks, failures);
    return failures == 0U ? 0 : 1;
}
