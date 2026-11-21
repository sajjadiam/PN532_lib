/**
 * @file pn532_registry.h
 * @brief Server-authoritative credential registry and revisioned delta contract.
 *
 * @details No MCU-wide card limit and no library-owned storage. Application
 * backends decide cache size and persistence. This layer is independent of
 * PN532 transport, MCU hardware, network protocol and allocation strategy.
 */
#ifndef PN532_REGISTRY_H
#define PN532_REGISTRY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Maximum NFCID1 size for ISO/IEC 14443 Type A. */
#define PN532_REGISTRY_UID_MAX 10U

/** @brief Identity *observation*; UID alone is not authentication. */
typedef struct {
    uint8_t length;                     /**< Must be 4, 7 or 10. */
    uint8_t bytes[PN532_REGISTRY_UID_MAX];
} pn532_registry_uid_t;

/**
 * @brief One authoritative credential record obtained from the server.
 *
 * @note credential_id is the server-assigned stable database key and must
 * not be derived from the tag UID. Policy interpretation is application-owned.
 */
typedef struct {
    uint64_t credential_id;          /**< Nonzero stable server key. */
    uint64_t policy_id;              /**< Application-defined policy reference. */
    pn532_registry_uid_t uid;        /**< ISO14443-A observed identifier. */
    bool enabled;                    /**< Server-controlled credential state. */
} pn532_registry_record_t;

/** @brief One change in the server's single ordered dataset stream. */
typedef enum {
    PN532_REGISTRY_UPSERT = 1,
    PN532_REGISTRY_REMOVE = 2
} pn532_registry_operation_t;

/**
 * @brief Server-issued atomic update.
 *
 * @details REMOVE uses record.credential_id only; all other record fields
 * are ignored. UPSERT requires a valid Type-A UID and nonzero credential_id.
 */
typedef struct {
    uint64_t dataset_id;             /**< Nonzero server dataset generation. */
    uint64_t revision;               /**< Sequential revision within dataset. */
    pn532_registry_operation_t operation;
    pn532_registry_record_t record;
} pn532_registry_delta_t;

/** @brief Result of validating and applying a server delta. */
typedef enum {
    PN532_REGISTRY_OK = 0,
    PN532_REGISTRY_ALREADY_APPLIED,
    PN532_REGISTRY_SNAPSHOT_REQUIRED,
    PN532_REGISTRY_BUSY,
    PN532_REGISTRY_STORAGE_ERROR,
    PN532_REGISTRY_INVALID_ARGUMENT
} pn532_registry_result_t;

/**
 * @brief Atomically persist a delta *and its new revision*.
 *
 * @details Return OK only after both changes are durably committed as one
 * transaction. BUSY means nothing was applied; retry the same delta later.
 * For every non-OK result, the backend must leave its durable record set
 * and revision unchanged. Implementations may use a journal or dual-bank
 * snapshot. Do not expose this callback to ISR execution.
 *
 * A network acknowledgement must follow confirmed persistence, not merely
 * delivery of the MQTT/HTTP message.
 */
typedef pn532_registry_result_t (*pn532_registry_apply_fn)(
    void *context,
    const pn532_registry_delta_t *delta
);

/**
 * @brief Caller-owned registry cursor and storage binding.
 *
 * @note dataset_id/revision must match the backend's durable state at init.
 * No arrays, network handles, or persistent buffers are stored here.
 */
typedef struct {
    uint64_t dataset_id;
    uint64_t revision;
    pn532_registry_apply_fn apply_atomic;
    void *storage_context;
} pn532_registry_t;

/**
 * @brief Bind a previously initialized and verified backend snapshot.
 *
 * @details On a fresh MCU or dataset change, the application must first
 * install a complete server snapshot atomically and then initialize this
 * cursor with the snapshot's dataset_id and revision. This function does
 * NOT install, download or verify the snapshot itself.
 */
pn532_registry_result_t pn532_registry_init(
    pn532_registry_t *registry,
    uint64_t dataset_id,
    uint64_t persisted_revision,
    pn532_registry_apply_fn apply_atomic,
    void *storage_context
);

/**
 * @brief Validate, deduplicate and persist the next contiguous delta.
 *
 * @retval PN532_REGISTRY_OK Delta atomically persisted and cursor advanced.
 * @retval PN532_REGISTRY_ALREADY_APPLIED Revision is not newer.
 * @retval PN532_REGISTRY_SNAPSHOT_REQUIRED Dataset differs or revision gap.
 * @retval PN532_REGISTRY_BUSY Backend still processing; retry unchanged.
 * @retval PN532_REGISTRY_STORAGE_ERROR Backend rejected atomic commit.
 * @retval PN532_REGISTRY_INVALID_ARGUMENT Bad input or uninitialized state.
 *
 * @note Only one task may use this registry at a time.
 * @note Wire-level deserialization is intentionally outside this interface;
 * never transmit this C struct's raw memory representation.
 */
pn532_registry_result_t pn532_registry_apply_delta(
    pn532_registry_t *registry,
    const pn532_registry_delta_t *delta
);

#ifdef __cplusplus
}
#endif

#endif /* PN532_REGISTRY_H */
