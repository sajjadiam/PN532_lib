# PN532 Library

A portable, modular Embedded C library for the **NXP PN532 NFC controller**.

## Overview

PN532_lib aims to provide a hardware-independent interface for PN532-based embedded systems. The design separates physical I/O, PN532 frame encoding, and future command/NFC APIs.

### Design goals

- **Portability:** Pure C11 core, without dependence on any MCU vendor SDK or RTOS.
- **Modularity:** Separate frame encoding, receive planning, transport, and command-processing layers.
- **Nonblocking:** An event-driven HSU interface is the target for use in larger real-time systems.
- **Reliability:** Explicit errors, deadlines, frame validation, and controlled recovery.
- **Ownership:** Independent device instances, caller-owned buffers, and no dynamic allocation.

## Support status

| Feature | Status |
|---|---|
| Normal / extended frames, ACK / NACK, parsing | Implemented and host-tested |
| Receive-length planning | Implemented and host-tested |
| HSU/UART nonblocking transport | Implemented; host-tested, hardware validation pending |
| Command-level asynchronous Core and protocol Recovery | Implemented; host-tested |
| Nonblocking ISO14443-A discovery and target parsing | Implemented; host-tested, hardware validation pending |
| Credential registry delta coordinator | Implemented; storage and network adapters pending |
| MCU-local cooperative multi-reader scheduling | Implemented; host-tested, hardware validation pending |
| Server synchronization and RF field arbitration | Architecture defined; implementation pending |
| Optional RST / IRQ board integration | Contract specified; implementation pending |
| I2C and SPI | Deferred to future releases |

The command Core, ISO14443-A discovery/parsing and versioned registry delta contract are host-tested, but real hardware behavior, card-memory operations, durable storage, and server synchronization still require implementation/validation. Do not treat this as a complete NFC access-control system.

## Architecture and contracts

See [Async HSU architecture](docs/architecture.md) for transport semantics and [Registry synchronization contract](docs/registry-sync.md) for server-authoritative credential revisions, cache boundaries, and limitations.

See [MCU multi-reader scheduler](docs/multi-reader.md) for reader scheduling, failure isolation and RF field caveats.

## Testing

Use CMake and CTest to build and run the host-side frame, transport, Core, Core+HSU integration and registry tests. Hardware behavior remains to be verified on a real PN532 module.
