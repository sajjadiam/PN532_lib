/**
 * @file test_pn532_registry.c
 * @brief Deterministic tests of the server-authoritative delta cursor.
 */
#include "pn532_registry.h"

#include <stdio.h>
#include <string.h>

static unsigned int checks;
static unsigned int failures;

#define CHECK(expr) do {                                             \
    ++checks;                                                        \
    if (!(expr)) {                                                   \
        ++failures;                                                  \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); \
    }                                                                \
} while (0)

#define CACHE_SLOTS 4U

typedef struct {
    pn532_registry_record_t records[CACHE_SLOTS];
    uint64_t persisted_dataset;
    uint64_t persisted_revision;
    pn532_registry_result_t forced_status;
    unsigned int commits;
    unsigned int calls;
} memory_store_t;

/** @brief Fake atomic transaction: stage all modifications before commit. */
static pn532_registry_result_t store_atomic(
    void *context,
    const pn532_registry_delta_t *delta)
{
    memory_store_t *store = (memory_store_t *)context;
    pn532_registry_record_t staged[CACHE_SLOTS];
    unsigned int i;
    unsigned int slot = CACHE_SLOTS;

    ++store->calls;
    if (store->forced_status != PN532_REGISTRY_OK) {
        return store->forced_status;
    }
    memcpy(staged, store->records, sizeof(staged));

    for (i = 0U; i < CACHE_SLOTS; ++i) {
        if (staged[i].credential_id == delta->record.credential_id) {
            slot = i;
            break;
        }
    }
    if (delta->operation == PN532_REGISTRY_REMOVE) {
        if (slot != CACHE_SLOTS) {
            memset(&staged[slot], 0, sizeof(staged[slot]));
        }
    } else {
        if (slot == CACHE_SLOTS) {
            for (i = 0U; i < CACHE_SLOTS; ++i) {
                if (staged[i].credential_id == UINT64_C(0)) {
                    slot = i;
                    break;
                }
            }
        }
        if (slot == CACHE_SLOTS) {
            return PN532_REGISTRY_STORAGE_ERROR;
        }
        staged[slot] = delta->record;
    }

    /* The simulated commit boundary updates records and revision together. */
    memcpy(store->records, staged, sizeof(staged));
    store->persisted_dataset = delta->dataset_id;
    store->persisted_revision = delta->revision;
    ++store->commits;
    return PN532_REGISTRY_OK;
}

static pn532_registry_delta_t delta_for(uint64_t revision,
                                        pn532_registry_operation_t operation)
{
    pn532_registry_delta_t delta;
    memset(&delta, 0, sizeof(delta));
    delta.dataset_id = UINT64_C(12);
    delta.revision = revision;
    delta.operation = operation;
    delta.record.credential_id = UINT64_C(1001);
    delta.record.policy_id = UINT64_C(81);
    delta.record.uid.length = 7U;
    delta.record.uid.bytes[0] = 0x04U;
    delta.record.uid.bytes[1] = 0x11U;
    delta.record.uid.bytes[2] = 0xA8U;
    delta.record.enabled = true;
    return delta;
}

static void test_apply_and_deduplicate(void)
{
    memory_store_t storage = {0};
    pn532_registry_t registry = {0};
    pn532_registry_delta_t delta = delta_for(UINT64_C(1), PN532_REGISTRY_UPSERT);

    CHECK(pn532_registry_init(&registry, UINT64_C(12), UINT64_C(0),
                              store_atomic, &storage) == PN532_REGISTRY_OK);
    CHECK(pn532_registry_apply_delta(&registry, &delta) == PN532_REGISTRY_OK);
    CHECK(storage.commits == 1U);
    CHECK(storage.persisted_revision == UINT64_C(1));
    CHECK(registry.revision == UINT64_C(1));
    CHECK(storage.records[0].credential_id == UINT64_C(1001));
    CHECK(pn532_registry_apply_delta(&registry, &delta) ==
          PN532_REGISTRY_ALREADY_APPLIED);
    CHECK(storage.calls == 1U);

    delta.revision = UINT64_C(2);
    delta.operation = PN532_REGISTRY_REMOVE;
    CHECK(pn532_registry_apply_delta(&registry, &delta) == PN532_REGISTRY_OK);
    CHECK(storage.records[0].credential_id == UINT64_C(0));
    CHECK(storage.persisted_revision == UINT64_C(2));
}

static void test_gaps_and_dataset_changes(void)
{
    memory_store_t storage = {0};
    pn532_registry_t registry = {0};
    pn532_registry_delta_t delta = delta_for(UINT64_C(7), PN532_REGISTRY_UPSERT);

    CHECK(pn532_registry_init(&registry, UINT64_C(12), UINT64_C(5),
                              store_atomic, &storage) == PN532_REGISTRY_OK);
    CHECK(pn532_registry_apply_delta(&registry, &delta) ==
          PN532_REGISTRY_SNAPSHOT_REQUIRED);
    CHECK(storage.calls == 0U);
    delta.revision = UINT64_C(6);
    delta.dataset_id = UINT64_C(13);
    CHECK(pn532_registry_apply_delta(&registry, &delta) ==
          PN532_REGISTRY_SNAPSHOT_REQUIRED);
    CHECK(storage.calls == 0U);
    delta.dataset_id = UINT64_C(12);
    CHECK(pn532_registry_apply_delta(&registry, &delta) == PN532_REGISTRY_OK);
    CHECK(registry.revision == UINT64_C(6));
}

static void test_retry_after_failed_commit(void)
{
    memory_store_t storage = {0};
    pn532_registry_t registry = {0};
    pn532_registry_delta_t delta = delta_for(UINT64_C(1), PN532_REGISTRY_UPSERT);

    CHECK(pn532_registry_init(&registry, UINT64_C(12), UINT64_C(0),
                              store_atomic, &storage) == PN532_REGISTRY_OK);
    storage.forced_status = PN532_REGISTRY_BUSY;
    CHECK(pn532_registry_apply_delta(&registry, &delta) ==
          PN532_REGISTRY_BUSY);
    CHECK(registry.revision == UINT64_C(0));
    CHECK(storage.commits == 0U);

    storage.forced_status = PN532_REGISTRY_STORAGE_ERROR;
    CHECK(pn532_registry_apply_delta(&registry, &delta) ==
          PN532_REGISTRY_STORAGE_ERROR);
    CHECK(registry.revision == UINT64_C(0));

    storage.forced_status = PN532_REGISTRY_OK;
    CHECK(pn532_registry_apply_delta(&registry, &delta) == PN532_REGISTRY_OK);
    CHECK(registry.revision == UINT64_C(1));
    CHECK(storage.commits == 1U);
}

static void test_validation_and_overflow(void)
{
    memory_store_t storage = {0};
    pn532_registry_t registry = {0};
    pn532_registry_delta_t delta = delta_for(UINT64_C(1), PN532_REGISTRY_UPSERT);

    CHECK(pn532_registry_init(NULL, UINT64_C(12), UINT64_C(0),
                              store_atomic, &storage) ==
          PN532_REGISTRY_INVALID_ARGUMENT);
    CHECK(pn532_registry_init(&registry, UINT64_C(0), UINT64_C(0),
                              store_atomic, &storage) ==
          PN532_REGISTRY_INVALID_ARGUMENT);
    CHECK(pn532_registry_init(&registry, UINT64_C(12), UINT64_C(0),
                              NULL, &storage) ==
          PN532_REGISTRY_INVALID_ARGUMENT);
    CHECK(pn532_registry_init(&registry, UINT64_C(12), UINT64_C(0),
                              store_atomic, &storage) == PN532_REGISTRY_OK);
    CHECK(pn532_registry_apply_delta(&registry, NULL) ==
          PN532_REGISTRY_INVALID_ARGUMENT);

    delta.record.uid.length = 8U;
    CHECK(pn532_registry_apply_delta(&registry, &delta) ==
          PN532_REGISTRY_INVALID_ARGUMENT);
    delta.record.uid.length = 10U;
    delta.record.credential_id = UINT64_C(0);
    CHECK(pn532_registry_apply_delta(&registry, &delta) ==
          PN532_REGISTRY_INVALID_ARGUMENT);
    delta.record.credential_id = UINT64_C(1001);
    delta.revision = UINT64_C(0);
    CHECK(pn532_registry_apply_delta(&registry, &delta) ==
          PN532_REGISTRY_INVALID_ARGUMENT);
    delta.revision = UINT64_C(1);
    delta.operation = (pn532_registry_operation_t)99;
    CHECK(pn532_registry_apply_delta(&registry, &delta) ==
          PN532_REGISTRY_INVALID_ARGUMENT);

    delta = delta_for(UINT64_MAX, PN532_REGISTRY_UPSERT);
    CHECK(pn532_registry_apply_delta(&registry, &delta) ==
          PN532_REGISTRY_SNAPSHOT_REQUIRED);
    CHECK(storage.calls == 0U);
    CHECK(pn532_registry_init(&registry, UINT64_C(12), UINT64_MAX,
                              store_atomic, &storage) == PN532_REGISTRY_OK);
    CHECK(pn532_registry_apply_delta(&registry, &delta) ==
          PN532_REGISTRY_ALREADY_APPLIED);
    CHECK(storage.calls == 0U);
}

static void test_cache_capacity_is_backend_policy(void)
{
    memory_store_t storage = {0};
    pn532_registry_t registry = {0};
    pn532_registry_delta_t delta = delta_for(UINT64_C(1), PN532_REGISTRY_UPSERT);
    uint64_t id;

    CHECK(pn532_registry_init(&registry, UINT64_C(12), UINT64_C(0),
                              store_atomic, &storage) == PN532_REGISTRY_OK);
    for (id = UINT64_C(1001); id < UINT64_C(1005); ++id) {
        delta.record.credential_id = id;
        CHECK(pn532_registry_apply_delta(&registry, &delta) ==
              PN532_REGISTRY_OK);
        ++delta.revision;
    }
    CHECK(registry.revision == UINT64_C(4));
    delta.record.credential_id = UINT64_C(1005);
    CHECK(pn532_registry_apply_delta(&registry, &delta) ==
          PN532_REGISTRY_STORAGE_ERROR);
    CHECK(registry.revision == UINT64_C(4));
    CHECK(storage.persisted_revision == UINT64_C(4));
}

int main(void)
{
    test_apply_and_deduplicate();
    test_gaps_and_dataset_changes();
    test_retry_after_failed_commit();
    test_validation_and_overflow();
    test_cache_capacity_is_backend_policy();
    printf("PN532 registry: %u checks, %u failures\n", checks, failures);
    return failures == 0U ? 0 : 1;
}
