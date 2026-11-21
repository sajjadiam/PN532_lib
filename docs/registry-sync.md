# Credential registry and distributed synchronization (initial contract)

## Scope and authoritative data

- **Server:** authoritative credential database, access policies, dataset generation, monotonically increasing revision and complete snapshots. A 100,000-credential target is an **application/database sizing goal**, not a PN532 or MCU capacity guarantee.
- **MCU:** independent cache backend, durable revision cursor, outbound observation-event queue, reader scheduler and network adapter. Cache size is **backend-configured**; its available capacity does not affect server cardinality.
- **Reader:** independent PN532 Core + HSU instance. Read/identify tags only; an observed UID is not cryptographic proof of a card's identity.

No NFC card data is written or emulated by this registry module.

## Implemented now

The public \`pn532_registry.h\` interface exposes:
- Stable server-assigned \`credential_id\` (not computed from a card UID).
- \`dataset_id\` to invalidate cursors after server-side rebuilds or generation changes.
- Ordered, strictly contiguous \`revision\` values and \`UPSERT\`/\`REMOVE\` changes.
- A caller-supplied \`apply_atomic(context, delta)\` function. A storage adapter must persist **the record change and the new revision together**, returning success only after a durable commit.
- Duplicate revision suppression and gap/generation detection; gaps request a full snapshot.
- Explicit BUSY/storage-error results with no cursor advancement.
- No heap allocation, networking SDK or fixed maximum number of credentials in this layer.

## Not implemented yet

- Database/Flash/FRAM storage driver or durable snapshot installation.
- Application authorization, credential lookup and UID-to-account conflict handling.
- Card-reader polling, tag parsing, local time-slicing and multi-reader scheduler.
- Server transport (MQTT/HTTPS), wire serialization, signature/authentication, reconnect policy and retries.
- Outbound observation-event queue, monotonic event IDs, server de-duplication and timestamps.
- Access policy freshness bounds and offline fail-open/fail-closed decision policy.

The registry is a **delta-validation and commit coordinator**, not a complete replicated database. Its test storage has only four slots by design; that is a test constraint, not a library limit.

## Synchronization rules

1. Install a **complete, authenticated server snapshot** using the backend's own atomic mechanism; persist dataset and revision in the same transaction.
2. Initialize the cursor using that persisted \`dataset_id\` and \`revision\`.
3. Apply exactly the next revision for the same dataset. Treat older revisions as already applied, and version gaps or dataset changes as requiring snapshot repair.
4. For a successful callback, acknowledge the delta to the server **after** storage commit. For BUSY, retry without advancing. On storage error, do not acknowledge.
5. Never serialize the in-memory C structs directly; use a defined portable network encoding and authenticate the server message before calling this API.
6. When the stream approaches \`UINT64_MAX\`, start a new dataset generation via a snapshot. Revisions do not wrap.
7. Each reader's PN532 protocol recovery is independent. A failed reader must not corrupt the MCU-wide cursor or other readers.

## Security and identity caveats

The initial \`pn532_registry_uid_t\` models ISO/IEC 14443-A NFCID1 of 4, 7 or 10 bytes only. Some tags may use randomized or changeable identifiers. Other card technologies require an explicit technology-tagged key extension. An application must never authorize sensitive access solely from the UID or assume UID uniqueness without server-side policy.

For high assurance, use an authenticated card protocol and server-issued credentials. Protect server-to-MCU snapshots and deltas against tampering/replay. Transport QoS alone does not provide application exactly-once effects: the durable revision cursor supplies ordered, idempotent replay behavior only **within one dataset stream**.

## Existing hardware references

- NXP PN532 User Manual: https://www.nxp.com/docs/en/user-guide/141520.pdf
- NXP PN532/C1 Datasheet: https://www.nxp.com/docs/en/nxp/data-sheets/PN532_C1.pdf

These references concern PN532 tag and HSU behavior; the registry revision protocol above is an **application design contract**, not a feature claimed for PN532 silicon.
