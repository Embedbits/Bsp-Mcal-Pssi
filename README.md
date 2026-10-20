# PSSI Peripheral Driver

This module provides an abstraction layer for the **parallel synchronous slave interface** (`PSSI`) of STM32 MCUs: a synchronous
8-bit or 16-bit parallel port, driven by an external clock, that **receives or transmits a continuous high-speed data stream** (the
data bus, the clock `PDCK`, and the optional data-enable `DE` and ready `RDY` handshake signals), with the 32-byte FIFO and the DMA. It
is a low-level MCAL module: it owns the PSSI hardware, the application and the other modules never touch the registers.

The public interface (`Pssi_Port.h`, `Pssi_Types.h`) is **identical for all supported families**. The implementation
for a family lives in its own branch (`Dev/<Family>` for development, `Releases/<Family>` for the released versions), so
a module or an application written against the interface does not change when the MCU family changes.

This README is the landing page and the **specification of the interface** (master). The family branches contain their
own README with everything that is specific to the family: the instances of the device, their settings and the status of
the implementation.

> **Status: design.** No implementation exists yet, the interface below is a proposal. Stories and branches follow.

---

## Intent and purpose

- Some data sources and sinks do not speak a serial protocol: a **parallel ADC or DAC**, an FPGA, a CPLD, a camera in a raw parallel mode,
  a radar front end, a logic analyser front end. They put a word on the bus at every edge of a clock they own. The PSSI is the slave side of such
  a link: the MCU samples (or drives) the bus **at the rate of the external clock**, with no CPU involvement, and the DMA moves the words to
  (from) the memory.
- The handshake signals make the transfer **flow controlled**: `DE` tells the receiver that the data are valid (the sender frames the
  burst), `RDY` tells the sender that the receiver can take (or the sender can accept) the data. The module configures their use and polarity
  (`PSSI_HANDSHAKE_NONE`, `_DE`, `_RDY`, `_DE_RDY`).
- The interface is **half duplex**: a transfer is either an input or an output; the direction is set for a transfer and changes between
  the transfers.
- The module hides the differences between the families: the bus width, the FIFO size, the pins shared with the camera interface and
  the DMA request differ, the interface stays the same.

What the module is **not**: it does not generate a clock (the PSSI is a slave of the clock on the pin), it does not decode the
protocol of the attached device and it does not run together with the DCMI (the two share the pins).

---

## Concept

| Item | Meaning |
|---|---|
| **Direction** | `PSSI_DIRECTION_RECEIVE` or `PSSI_DIRECTION_TRANSMIT`. |
| **Width** | `PSSI_DATA_WIDTH_8` or `PSSI_DATA_WIDTH_16`. |
| **Clock** | The edge of `PDCK` on which the data are sampled / driven. |
| **Handshake** | The use and the polarity of `DE` and `RDY` (the data enable is also used as the start / stop of the burst). |
| **Transfer** | A buffer and its size; the DMA moves the words, the module reports the end and the errors. |

Rules:

1. `Pssi_Init` configures the interface, the pins, the DMA and the interrupt and leaves the interface **idle**. The direction, the width,
   the clock edge and the handshake are changed only while the interface is idle.
2. `Pssi_Set_RxStart( buffer, size )` / `Pssi_Set_TxStart( buffer, size )` arm the interface and the DMA; the transfer ends when the number of
   the words was moved (or the burst was ended by `DE` when it is configured so), and the complete callback reports the number of the words
   transferred. The same request and the same callbacks are used in the DMA and the interrupt mode.
3. An **overrun** (the receive FIFO is full and the DMA is late) and an **underrun** (the transmit FIFO is empty) are reported by the error callback;
   the module stops the transfer and brings the interface to the idle state.
4. `Pssi_Set_Abort` stops a transfer and flushes the FIFO.
5. The size and the alignment of the buffer are checked against the width (an even number of bytes for the 16-bit bus); a violation is rejected.
6. All parameters are checked before a register is written.

---

## Supported families

Every family gets its own story, its own tasks and its own branches `Dev/<Family>` and `Releases/<Family>` cut from this
master.

| Family | Branch | Interface | Status |
|---|---|---|---|
| STM32H5 | `Dev/STM32H5` | `PSSI` on all lines except STM32H503: 8- or 16-bit data, the 8-word (32-byte) FIFO, `PDCK`, `DE` and `RDY`, DMA; the pins are shared with the `DCMI` | planned (first) |
| STM32U5 | `Dev/STM32U5` | `PSSI` on the lines that have it | planned |
| STM32H7 | `Dev/STM32H7` | `PSSI` on the lines that have it | planned |
| STM32U3 | `Dev/STM32U3` | device dependent | planned |
| STM32F4, STM32F7, STM32G4, STM32L4 | `Dev/STM32F4` ... | no PSSI on the device - the module is a stub that returns `PSSI_REQUEST_ERROR` | planned |

A function without a meaning in a family stays in the interface, returns `PSSI_REQUEST_ERROR` and suppresses the unused
parameters (for example the 16-bit width on an interface with 8 data lines).

---

## Public interface in short (proposal)

| Group | Functions |
|---|---|
| Module management | `Pssi_Get_ModuleVersion`, `Pssi_Init`, `Pssi_Deinit`, `Pssi_Task`, `Pssi_Get_DefaultConfig` |
| Transfers | `Pssi_Set_RxStart`, `Pssi_Set_TxStart`, `Pssi_Set_Abort`, `Pssi_Get_XferState`, `Pssi_Get_XferCount`, `Pssi_Get_XferError` |
| Settings | `Pssi_Set_DataWidth` / `Get`, `Pssi_Set_ClockEdge` / `Get`, `Pssi_Set_Handshake` / `Get` |
| Interrupts | `Pssi_Set_IrqPriority` / `Get`, the callbacks are a part of the configuration |
| Pins | `Pssi_InitClockGpio`, `Pssi_InitDataGpio`, `Pssi_InitDeGpio`, `Pssi_InitRdyGpio` |

```c
pssi_ModuleVersion_t Pssi_Get_ModuleVersion  ( void );

pssi_RequestState_t  Pssi_Init               ( const pssi_Config_t * const pssiConfig );
pssi_RequestState_t  Pssi_Deinit             ( void );
void                 Pssi_Task               ( void );
pssi_RequestState_t  Pssi_Get_DefaultConfig  ( pssi_Config_t * const pssiConfig );

pssi_RequestState_t  Pssi_Set_RxStart        ( pssi_Data_t * const buffer, pssi_DataCnt_t byteCnt );
pssi_RequestState_t  Pssi_Set_TxStart        ( const pssi_Data_t * const buffer, pssi_DataCnt_t byteCnt );
pssi_RequestState_t  Pssi_Set_Abort          ( void );
pssi_RequestState_t  Pssi_Get_XferState      ( pssi_XferState_t * const xferState );
pssi_RequestState_t  Pssi_Get_XferCount      ( pssi_DataCnt_t * const transferredCnt );
```

Every function returns `PSSI_REQUEST_OK` / `PSSI_REQUEST_ERROR` and checks its parameters and the state of the module
before it touches a register.

| Type | Meaning |
|---|---|
| `pssi_Config_t` | Data width, clock edge, handshake mode and polarities, transfer mode (`PSSI_XFER_MODE_ISR` / `DMA`), DMA channels, pins, callbacks, interrupt priority |
| `pssi_XferState_t` | `PSSI_XFER_IDLE`, `PSSI_XFER_RECEIVING`, `PSSI_XFER_TRANSMITTING`, `PSSI_XFER_ERROR` |
| `pssi_XferError_t` | Bit mask: overrun, underrun, DMA error |
| Callbacks | `XferCompleteCallback( byteCnt )`, `ErrorCallback( errorMask )` |

---

## Usage

### Receiving a burst from a parallel ADC with the data enable

```c
#include "Pssi_Port.h"

static pssi_Data_t samples[ 4096u ];

static void App_BurstDone( pssi_DataCnt_t byteCnt ) { /* process byteCnt bytes of the samples */ }

pssi_Config_t pssiConfig;

(void)Pssi_Get_DefaultConfig( &pssiConfig );

pssiConfig.DataWidth         = PSSI_DATA_WIDTH_16;
pssiConfig.ClockEdge         = PSSI_CLOCK_EDGE_RISING;
pssiConfig.Handshake         = PSSI_HANDSHAKE_DE;           /* the ADC frames the burst with the data enable */
pssiConfig.XferMode          = PSSI_XFER_MODE_DMA;
pssiConfig.XferCompleteCallback = App_BurstDone;

(void)Pssi_Init( &pssiConfig );
(void)Pssi_Set_RxStart( samples, sizeof( samples ) );
```

---

## Behaviour and limits

- **Speed.** The interface samples at the external clock; the DMA, the FIFO and the memory must keep up, otherwise the overrun is reported. The
  maximum clock is in the datasheet.
- **Slave only.** The interface never drives the clock; the source must be running before the transfer starts, or the transfer waits.
- **Half duplex.** The direction changes between the transfers only; the module rejects a change in a running transfer.
- **Shared pins.** The camera interface and the PSSI share the pins; the product uses one of them at a time.
- **Not part of the first version:** the continuous circular reception, the transfer chaining, the sampling of the bus by an internal clock.

---

## Dependencies

- **Rcc** - peripheral clock and reset
- **Nvic** - interrupt
- **Gpio** - the clock, the data, the `DE` and `RDY` pins
- **Gpdma** - DMA transfers
- **RAL (Register Abstraction Layer)** - used internally to access the registers

All mandatory RCC, NVIC, GPIO and DMA configurations are handled internally.

---

## Testing

- **Unit tests** (host, Unity + CMock + RegMem): the configuration to register mapping, the buffer size and the alignment checks, the
  transfer flow (receive, transmit, end by `DE`, abort), the overrun and the underrun paths, the callbacks.
- **Integration tests** (target, probe-rs): a counter pattern driven by a timer and the GPIO as the external source (clock and data) received and
  compared, the data enable framing, the transmit loop to a second instance of the wiring.

---

## Interface conventions

The module follows the common rules of the MCAL layer, so that it behaves like the other modules:

- **Lists instead of free members.** Everything that the hardware restricts (the pins, the kernel clock sources, the DMA
  channels, the triggers and the other selectable sources) is a `typedef enum` list with exactly the items that are valid
  for the device line; a pin is an item `PSSI_<SIGNAL>_PIN_<INSTANCE>_P<port><pin>` and there is an `*_UNUSED` item. A choice
  that is not in the list cannot be written.
- **Own peripheral only.** The module calls only the registers / LL functions of its own peripheral. The clocks, the
  interrupts, the pins, the DMA and the EXTI lines go through the API of the Rcc, Nvic, Gpio, Gpdma and Exti modules; a
  missing function is added to that module, it is not bypassed.
- **Init verifies itself.** `Pssi_Init` checks the result at the end; when it is not as required, the module is
  deinitialized and an error is returned. The waiting for the hardware is a loop on the register state with a timeout,
  never a blind delay.
- **Transfer modes.** Where the module moves data, the POLL (served by `Pssi_Task`), ISR and DMA modes have the same
  configuration structure, the same request and the same callbacks (error, half transfer, transfer complete).
- **Flags and states are enums of the module** (`pssi_FunctionState_t`, `pssi_FlagState_t`), the limits and the constants are
  named, there are no magic numbers in the interface.
- **Unsupported functions stay.** A function without a meaning in a family keeps its signature, suppresses the unused
  parameters and returns `PSSI_REQUEST_ERROR`.
- **Documentation.** Every function and type has a Doxygen description, the main header has the `\defgroup`.

---

## Repository layout and branches

The master holds this README and the license. The family branches (`Dev/<Family>`, `Releases/<Family>`) hold:

| Path | Content |
|---|---|
| `README.md` | README of the family: the instances of the device, specifics, errata, status |
| `Pssi_Port.h`, `Pssi_Types.h` | Public interface (identical in all families) |
| `Pssi*.c`, `Pssi.h` | Implementation of the family |
| `CMakeLists.txt` | Library `Pssi_Lib` of the EmBi platform |
| `Tests/UnitTests` | Unit tests (Unity + CMock + RegMem) |
| `Tests/IntegrationTests` | Integration tests on the target (probe-rs) |

A change of the public interface is made in all families.

---

## CMake integration

```cmake
target_link_libraries( User_Lib PRIVATE Pssi_Lib )
```

1. Link `Pssi_Lib` to your CMake library.
2. Include `Pssi_Port.h` in your project.
3. Configure the module as needed for your hardware.

---

## License

This project is licensed under the **Creative Commons Attribution-NonCommercial 4.0 International (CC BY-NC 4.0)**.

You are free to use, modify, and share this work for **non-commercial purposes**, provided appropriate credit is given.

See [LICENSE.md](LICENSE.md) for full terms or visit [creativecommons.org/licenses/by-nc/4.0](https://creativecommons.org/licenses/by-nc/4.0/).

---

## Authors

- **Mr.Nobody** - [embedbits.com](https://embedbits.com)

Contributions are welcome! Please open a pull request.
