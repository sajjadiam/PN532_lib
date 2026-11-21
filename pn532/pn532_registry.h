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
 * not be derived from the tag UID. The server assigns a compact 32-bit
 * credential key (not a truncated UID or a truncated database ID).
 * Policy interpretation is application-owned.
 */
typedef struct {
    uint32_t credential_id;          /**< Nonzero stable server key. */
    uint32_t policy_id;              /**< Application-defined policy reference. */
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
    uint32_t dataset_id;             /**< Nonzero server dataset generation. */
    uint32_t revision;               /**< Sequential revision within dataset. */
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
 * @brief Configuration from a durably committed server snapshot.
 *
 * @note These are 32-bit application IDs, unrelated to PN532 internal IDs.
 * The server must rotate dataset_id and atomically install a new snapshot
 * before revision overflows UINT32_MAX. Dataset generations must not be
 * reused for the same deployment. The event stream for card observations
 * is separate; reads do not increment revision.
 */
typedef struct {
    uint32_t dataset_id;             /**< Nonzero dataset generation. */
    uint32_t persisted_revision;     /**< Committed revision, possibly zero. */
    pn532_registry_apply_fn apply_atomic;
    void *storage_context;
} pn532_registry_config_t;

/**
 * @brief Caller-owned registry cursor and backend binding.
 *
 * @note No arrays, network handles or persistent buffers are stored here.
 */
typedef struct {
    uint32_t dataset_id;
    uint32_t revision;
    pn532_registry_apply_fn apply_atomic;
    void *storage_context;
} pn532_registry_t;

/**
 * @brief Bind a previously installed and verified backend snapshot.
 *
 * @details The caller installs a complete authenticated snapshot atomically
 * and supplies its committed dataset_id and revision in config. This
 * function does not install, fetch or verify a snapshot.
 *
 * @param[out] registry Caller-owned cursor.
 * @param[in] config Persisted cursor and atomic storage callback.
 */
pn532_registry_result_t pn532_registry_init(
    pn532_registry_t *registry,
    const pn532_registry_config_t *config
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
