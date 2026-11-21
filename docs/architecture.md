# PN532 architecture: Async HSU contract (draft v0.1)

## Scope
Portable C11, instance-based and heap-free. Only HSU/UART is in scope for v0.1; I2C and SPI are future work. The frame encoder/decoder and receive-length planner are pure CPU functions and remain unchanged. The repository contains only one HSU transport implementation, in pn532_hsu.h/.c, and one transport contract, in pn532_transport.h.

## Nonblocking invariants
- No hardware callback may sleep, spin, wait for DMA/IRQ completion, or invoke an application callback.
- TX start returns immediately: OK means **accepted**, not physically transmitted. Caller-owned TX bytes remain unchanged until physical UART TC is reported.
- RX read returns only bytes immediately available, at most its requested capacity; RX must be armed **before** TX begins. No read-ahead or waiting for future bytes.
- Time source is a monotonic wrapping uint32_t millisecond counter. Deadline comparisons use unsigned elapsed subtraction and timeouts below 2^31 ms.
- Cooperative processing has an explicit per-call RX byte budget; there is no unlimited loop draining a full queue.
- ISR/driver only publishes data and events through an appropriately synchronized adapter. Completion callbacks run in task/main-loop context.
- No simultaneous operations per instance; no implicit retries or command queues.

## Frame boundaries and recovery
- Frame layer encodes/decodes, including LCS/DCS/TFI; no hardware I/O.
- RX planner returns the required number of bytes to complete a frame header or body.
- HSU transport frames bytes; Core handles ACK/NACK, response matching and command deadlines.
- RX overrun, malformed headers, incomplete frames and transport errors are reported explicitly. A flush/resynchronization must be performed before reusing corrupted receive state.
- A timeout or cancel does **not** prove the PN532 did not execute a command. Do not automatically retry commands.
- TX timeout begins when transfer is accepted; ACK timeout starts after physical TX complete; response timeout begins on valid ACK.
- Cancellation is a request. DMA buffer ownership persists until the platform confirms TX stopped or completed.

## Buffer ownership and concurrency
- Requests retain caller-owned buffers (without copying payloads) until completion or confirmed abort.
- Instance state is task-owned, and is not implicitly thread safe. ISR-facing platform queues require proper synchronization rather than volatile alone.
- A later Core will guarantee exactly one terminal notification per accepted request. No user callbacks from ISR.

## Optional board GPIO contract
- RST is optional: a board callback reset_set(context, asserted) performs a **nonblocking** logical reset assertion/release. Polarity is the board adapter's responsibility. A state machine handles reset hold and startup time. With no callback, hardware reset reports UNSUPPORTED.
- Wiring is unverified until the module schematic is supplied: RST may map to PN532 RSTPD_N (active low), and must **not** be confused with RSTOUT_N (output).
- IRQ is optional: an EXTI/event latch may notify the task about device activity. It is **not** UART RX IRQ and **not** required for ordinary HSU. Some modes produce short pulses; avoid unprotected slow GPIO polling.
- Any combination (neither / RST / IRQ / both) must work. Optional H_REQ handshaking and advanced low-power modes are deferred.

## API and implementation sequence
1. Formalize transport and board API and tests (implemented).
2. Implement HSU nonblocking adapter and UART simulator tests (initial version; hardware validation pending).
3. Implement asynchronous Core request/ACK/response state machine.
4. Hardware-in-the-loop GetFirmwareVersion verification.
5. Consider I2C/SPI after HSU stabilization.

## References
- NXP PN532/C1 Datasheet: https://www.nxp.com/docs/en/nxp/data-sheets/PN532_C1.pdf
- NXP PN532 User Manual UM0701-02: https://www.nxp.com/docs/en/user-guide/141520.pdf
