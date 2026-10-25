# GNU Radio GFSK Transmitter (Pico Config)

This is the first implementation of the GNU radio and is the most basic implementation showing how the continuous transmitter works with four modes being alternating, continuous 0s, PRBS(Psuedo Random Binary Sequence) and random. The other files in the project are the files I created in the GNU Radio GUI which for this implementation are two output being the Waterfall GUI and the Gain/Frequency GUI. The one variable the GNU radio shows off rf gain as the amount of configurability this project gives us with the radios.

This is a PlatformIO* project using the Arduino-Pico core (earlephilhower), which bundles
the Raspberry Pi Pico SDK.

The work is split across three pieces:

| Piece | Job | Why it does this job |
|---|---|---|
| **RFM69 radio** | Generates the RF carrier, does the GFSK modulation, and **sets the bit clock** (DCLK). | The radio's crystal sets the exact bitrate. We just follow its clock. |
| **PIO state machine** | Watches DCLK and puts the next bit on DATA at the right moment, every bit. | Bit timing has to be exact. PIO is hardware, so it never misses an edge, even when the CPU is busy printing to USB. |
| **CPU (`loop()`)** | Makes data in 32-bit chunks, keeps the PIO FIFO full, and handles serial commands. | Making data and talking to a human isn't timing-critical. It only has to stay ahead of an 8-word buffer. |

The SPI bus is only used to configure the radio and read status. No data goes over SPI.

## Pico2 to RFM69HCW Pinout

GPIO 16-19 are a specific pin group for SPI communication
|RASPBERRY PICO|RFM69HCW|
|---|---|
|GPIO 16|RX|
|GPIO 17|CSn|
|GPIO 18|SCK|
|GPIO 19|TX|
|GPIO 20|Reset(active high)|
|GPIO 21|DATA|
|GPIO 22|DCLK|
Also make sure if you are using the PA and for just safety always put the PA on the chip.


## Some Other Important Parameters

| Parameter | Default | Meaning |
|---|---|---|
| **Bitrate (BR)** | 50 kbps | Bits per second. Bit period = 20 µs. |
| **Fdev** | 25 kHz | How far each tone sits from the centre. |
| **Modulation index h** | 1.0 | `h = 2·Fdev / BR`. Tone spacing (2·Fdev = 50 kHz) divided by bitrate. |


## Small explanation of GFSK

How FSK encoding works in simplest idea

- bit **1** → `Fc + Fdev`
- bit **0** → `Fc − Fdev`

Difference between FSK and GFSK. Plain FSK jumps instantly between the two frequencies. Sharp jumps spread energy over a wide
bandwidth (spectral splatter). Gaussian FSK runs the bit stream through a Gaussian low-pass filter *before* the modulator, so the frequency glides between the two values. Nothing is actually pure FSK because you need to move across your band to change from a 0 to 1 but this adds the Guassian filter and is an extremely known and simple way of transmitting a radio signal.

The filter is described by **BT**, the bandwidth-time product (filter 3 dB bandwidth × bit
period):

| BT | Effect |
|---|---|
| 1.0 | Light smoothing, close to plain FSK, wider spectrum, least ISI |
| 0.5 (the one we use) | Good balance (Bluetooth uses this) |
| 0.3 | Narrow spectrum (GSM's GMSK uses this), more inter-symbol interference (ISI) |

Smaller BT means a narrower spectrum, but each bit "smears" into its neighbours, which makes
the receiver's job harder.
The three important parameters for a GFSK signal

| Parameter | Default | Meaning |
|---|---|---|
| Bitrate (BR) | 50 kbps | kilo bits per second |
| Fdev | 25 kHz | How far each tone sits from the centre. |
| Modulation index h | 1.0 | `h = 2·Fdev / BR`. Tone spacing (2·Fdev = 50 kHz) divided by bitrate. |

- h = 0.5 is MSK (minimum shift keying), the tightest spacing where the two tones stay
  orthogonal.
- h = 1 (the default) puts the tones further apart. That makes it very easy to see
  two clear peaks on a waterfall with the `a` pattern, and easy to demodulate.
- The SX1231 datasheet requires `0.5 ≤ h ≤ 10` and `Fdev + BR/2 ≤ 500 kHz`. The firmware
  warns over serial if you break either rule.

Sampling on the SDR for GFSK follows Carson's rule: `2·(Fdev + BR/2) = 2·(25k + 25k) = 100 kHz`.
That's why the GNU Radio low-pass filter is set to about 60 kHz one-sided (±50 kHz plus
some margin).



## Project Tree

```
rfm69_gfsk_tx/
├── platformio.ini          build config (board, platform, framework)
├── README.md               this file
├── .gitignore              ignores .pio/ build output and editor folders
├── include/
│   ├── config.h            pins + RF settings  <- the file you edit
│   ├── rfm69.h             driver API, register addresses, constants
│   └── dclk_tx.pio.h       PIO program, generated from src/dclk_tx.pio (the only copy)
├── src/
│   ├── main.cpp            setup()/loop(), serial commands, pattern generator, PIO feeding
│   ├── rfm69.cpp           RFM69 SPI driver (reset, register read/write, configure, modes)
│   └── dclk_tx.pio         PIO assembly source (reference; not compiled directly)
├── .vscode/                PlatformIO-generated editor/debug config (gitignored)
└── .pio/                   build output, incl. firmware.uf2 (gitignored)
```

## src/dclk_tx.pio and the generated header

 The program

```
.program dclk_tx
.wrap_target
    wait 0 pin 0        ; DCLK low
    out pins, 1         ; present next bit
    wait 1 pin 0        ; radio samples it on this rising edge
.wrap
```

| Instr. | What it does |
|---|---|
| `wait 0 pin 0` | Stall until IN-pin 0 (= DCLK) is **low**. "pin 0" is relative to the IN base set in `sm_config_set_in_pins`, so it's GP21. |
| `out pins, 1` | Shift **1 bit** out of the OSR (Output Shift Register) onto the OUT pins (= DATA, GP22). |
| `wait 1 pin 0` | Stall until DCLK is **high**. The radio has sampled DATA by now. |
| `.wrap` | Jump back to `.wrap_target` **for free** (no instruction cycle used). |

The `% c-sdk { … %}` block: `dclk_tx_program_init()`

`pioasm` copies this C code into the generated header. It sets up the state machine:

| Call | Why |
|---|---|
| `dclk_tx_program_get_default_config(offset)` | Generated by pioasm. Sets the wrap addresses relative to where the program got loaded. |
| `sm_config_set_in_pins(&c, dclk_pin)` | IN base = DCLK, so `wait … pin 0` means GP21. |
| `sm_config_set_out_pins(&c, data_pin, 1)` | OUT base = DATA, 1 pin wide. |
| `sm_config_set_out_shift(&c, false, true, 32)` | `false` = shift **left** → MSB leaves first. `true` = **autopull**. `32` = pull threshold. So word `0x80000000` sends a `1` followed by 31 `0`s. |
| `sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX)` | Each SM has a 4-word TX FIFO and a 4-word RX FIFO. We never receive, so the RX FIFO gets joined to TX: **8 words = 256 bits = 5.12 ms** of buffer at 50 kbps. |
| `pio_gpio_init(pio, data_pin/dclk_pin)` | Route the pins to this PIO block. (For an input that's not strictly required to *read* it, but it's harmless and keeps things consistent.) |
| `gpio_pull_down(dclk_pin)` | If DIO1 isn't connected, DCLK reads a solid low and doesn't float and pick up noise. With no clock, nothing gets transmitted, and the bitrate report reads ~0, which makes the wiring fault obvious. |
| `pio_sm_set_consecutive_pindirs(..., true/false)` | DATA = output, DCLK = input, from the PIO's point of view. |
| `pio_sm_init` + `pio_sm_set_enabled` | Load the config, reset the PC to the start, and start running. |

`#include "hardware/clocks.h"` in that block isn't actually used. Many SDK examples include
it because they call `clock_get_hz()` to set a clock divider. This program doesn't need a
divider: it runs at full speed and is paced only by `wait`.

`dclk_tx.pio.h`: why it's checked in

PlatformIO doesn't run `pioasm` automatically for this core, so the assembled output is
committed. The `.pio` file is the source of truth for humans, and the `.h` file is what the
compiler sees. The header contains:
- `dclk_tx_program_instructions[]`: the 3 machine-code words (`0x2020`, `0x6001`, `0x20a0`)
- `dclk_tx_program`: a struct the SDK's `pio_add_program()` loads into instruction memory
- `dclk_tx_wrap_target` / `dclk_tx_wrap`: loop addresses (0 and 2)
- the copied `dclk_tx_program_init()` function

To regenerate it after editing the `.pio` (pioasm ships with PlatformIO):

```bash
~/.platformio/packages/tool-pioasm-rp2040-earlephilhower/pioasm -o c-sdk src/dclk_tx.pio include/dclk_tx.pio.h
```

Keep **only one** copy of this header, in `include/`. Don't put a copy in `src/`: `main.cpp`
includes it with quotes, and GCC searches the including file's own folder first, so a copy in
`src/` would silently override the one in `include/`. See [review item 1](#9-code-review).


## Big picture of configuration

```
                 USB serial (115200)
   PC terminal  <------------------->  +-------------------- Pico 2 (RP2350) ---------------------+
   (commands,                          |                                                         |
    status)                            |  CPU (Arduino loop)                                     |
                                       |   - picks a pattern (PN9 / random / 1010 / zeros)       |
                                       |   - generates 32-bit words                              |
                                       |   - keeps the PIO TX FIFO full --------+                |
                                       |   - configures the radio over SPI      |                |
                                       |                 |                      v                |
                                       |                 |          PIO state machine            |
                                       |                 |          "dclk_tx" (3 instructions)   |
                                       |                 |           waits for DCLK low,         |
                                       |                 |           puts 1 bit on DATA          |
                                       +-----------------|----------------|--------^-------------+
                                                SPI0     |           DATA |        | DCLK
                                          (config only)  v           GP22 v        | GP21
                                       +----------------------------- RFM69HCW --------------------+
                                       |  registers <- SPI      DIO2 (DATA in)   DIO1 (DCLK out)  |
                                       |  modulator: bit 1 -> Fc + Fdev, bit 0 -> Fc - Fdev        |
                                       |  Gaussian filter (BT 0.5) smooths the frequency steps     |
                                       +------------------------------------ RF out ---------------+
                                                                               |
                                                                           antenna / attenuator
                                                                               |
                                                                      SDR  ->  GNU Radio
                                                         (LPF -> quad demod -> symbol sync -> slicer -> BER)
```