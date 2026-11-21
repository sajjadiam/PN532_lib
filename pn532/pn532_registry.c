/**
 * @file pn532_registry.c
 * @brief Bounded, backend-neutral application of versioned credential deltas.
 */
#include "pn532_registry.h"

#include <stddef.h>
#include <stdint.h>

static bool valid_record(const pn532_registry_record_t *record)
{
    return record->credential_id != UINT32_C(0) &&
           (record->uid.length == 4U ||
            record->uid.length == 7U ||
            record->uid.length == 10U);
}

pn532_registry_result_t pn532_registry_init(
    pn532_registry_t *registry,
    const pn532_registry_config_t *config)
{
    if (registry == NULL || config == NULL ||
        config->dataset_id == UINT32_C(0) ||
        config->apply_atomic == NULL) {
        return PN532_REGISTRY_INVALID_ARGUMENT;
    }

    registry->dataset_id = config->dataset_id;
    registry->revision = config->persisted_revision;
    registry->apply_atomic = config->apply_atomic;
    registry->storage_context = config->storage_context;
    return PN532_REGISTRY_OK;
}

pn532_registry_result_t pn532_registry_apply_delta(
    pn532_registry_t *registry,
    const pn532_registry_delta_t *delta)
{
    pn532_registry_result_t status;

    if (registry == NULL || registry->apply_atomic == NULL ||
        registry->dataset_id == UINT32_C(0) || delta == NULL ||
        delta->dataset_id == UINT32_C(0) ||
        delta->revision == UINT32_C(0) ||
        (delta->operation != PN532_REGISTRY_UPSERT &&
         delta->operation != PN532_REGISTRY_REMOVE) ||
        delta->record.credential_id == UINT32_C(0) ||
        (delta->operation == PN532_REGISTRY_UPSERT &&
         !valid_record(&delta->record))) {
        return PN532_REGISTRY_INVALID_ARGUMENT;
    }

    if (delta->dataset_id != registry->dataset_id) {
        return PN532_REGISTRY_SNAPSHOT_REQUIRED;
    }
    if (delta->revision <= registry->revision) {
        return PN532_REGISTRY_ALREADY_APPLIED;
    }
    if (registry->revision == UINT32_MAX ||
        delta->revision - registry->revision != UINT32_C(1)) {
        return PN532_REGISTRY_SNAPSHOT_REQUIRED;
    }

    status = registry->apply_atomic(registry->storage_context, delta);
    if (status == PN532_REGISTRY_OK) {
        registry->revision = delta->revision;
        return PN532_REGISTRY_OK;
    }
    return status == PN532_REGISTRY_BUSY
        ? PN532_REGISTRY_BUSY
        : PN532_REGISTRY_STORAGE_ERROR;
}
