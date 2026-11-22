# PN532 multi-reader scheduler (MCU-local, first iteration)

## Scope

This module schedules multiple independent PN532 Core instances on **one MCU**. Each instance owns its UART/HSU adapter, Core buffers, and transport state. It emits copied Type-A card observations tagged with a reader ID, without heap allocation or a network SDK.

This layer does **not** perform server synchronization. A future network adapter must add MCU identity, event identifiers, timestamps, storage and acknowledgements.

## Scheduling

Call `pn532_multi_init(manager, config)` once, then `pn532_multi_process(manager)` regularly from task/main-loop context. Each call starts at most one command or advances one Core step. The manager keeps one command in flight across all its readers and rotates after each discovery response.

Before a reader's first poll, the manager sends `RFConfiguration` (PD0 `0x32`, CfgItem `0x05`, parameters `0xFF, 0x01, max_passive_retries`). In NXP UM0701-02 §7.3.1, a passive activation retry value of `0xFF` means infinite search; `0x00` means one attempt. Infinite retry is rejected. The manager next sends `InListPassiveTarget` (`0x4A`, ISO14443-A at 106 kbit/s), with up to two targets.

A successful no-tag response generates no event. Each detected target produces a copied `PN532_MULTI_TAG` event. Repeated polling of a stationary tag creates repeated observations; presence debouncing and server event deduplication are application-level concerns.

`response_timeout_ms` applies to configuration and discovery. It must cover RF retries and actual hardware latency; Core TX and ACK deadlines remain separate.

## Fault isolation and recovery

If configuration, discovery, or tag parsing fails, only the affected reader is suspended. One `PN532_MULTI_ERROR` event is emitted and the scheduler continues servicing the remaining readers. No uncertain application command is retried automatically.

The application can perform `pn532_recover()` on that Core as necessary and subsequently call `pn532_multi_resume()`. Resume repeats RFConfiguration. Calling resume while the Core is recovery-locked causes the subsequent submission to fail and suspend the reader again.

Event callbacks run in task context, directly inside `pn532_multi_process()`; do not recursively invoke the manager or its Core operations. Event data remains valid only during the callback, and must be copied or queued for later use.

## RF caveat

**Serialized polling commands do not guarantee RF field isolation.** PN532 RF field state can persist beyond the command. NXP documents `RFConfiguration` CfgItem `0x01` for RF field control. A hardware-validated RF arbiter, with field settling times and antenna-spacing measurements, is needed before using this scheduler to time-multiplex physically adjacent reader antennas. For sufficiently separated antennas, a separate parallel scheduler mode can be added.

## Example

```c
static pn532_multi_t manager;
static pn532_multi_reader_t readers[] = {
    {.core = &reader_a_core, .id = 1U},
    {.core = &reader_b_core, .id = 2U}
};

static const pn532_multi_config_t multi_config = {
    .readers = readers,
    .reader_count = 2U,
    .max_targets = 2U,
    .max_passive_retries = 0U,
    .response_timeout_ms = 200U,
    .on_event = application_card_event,
    .context = NULL
};

(void)pn532_multi_init(&manager, &multi_config);
for (;;) {
    pn532_multi_process(&manager);
    application_network_process();
}
```

The example presumes independently initialized Core instances and an application-defined event callback. The timeout is illustrative, not validated on hardware.

## Reference

- NXP PN532 User Manual UM0701-02: https://www.nxp.com/docs/en/user-guide/141520.pdf — §7.3.1 RFConfiguration and §7.3.5 InListPassiveTarget.
