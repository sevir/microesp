# Pocket-Dongle-S3 pinout (MESP-US-0001)

Clone of the LilyGO T-Dongle-S3. ESP32-S3 (QFN56) rev v0.2, 8 MB embedded octal PSRAM, 16 MB quad flash, MAC `90:70:69:f6:62:dc`.
Spike: `hw/spikes/pinout/` (ESP-IDF v5.4). The same code also runs inside the TuyaOpen firmware of the `hw/spikes/tuya-usb/` spike (`pinout_probe` component), which is the one left flashed on the board.

## Status

| Function | GPIO (T-Dongle-S3 reference) | Status | Evidence |
|---|---|---|---|
| Flash 16 MB / PSRAM 8 MB octal | — | **Verified by software** | esptool `flash-id`; log `flash 16 MB, PSRAM 8192 KB` with `SPIRAM_MODE_OCT` |
| LCD SDA (MOSI) | **11** (not 3) | **Verified** (factory firmware, JTAG) | `GPIO_FUNC11_OUT_SEL=103` (FSPID) |
| LCD SCL | **10** (not 5) | **Verified** (JTAG) | `GPIO_FUNC10_OUT_SEL=101` (FSPICLK) |
| LCD CS | **12** (not 4) | **Verified** (JTAG) | software GPIO; at 0 during SPI transfers |
| LCD DC | **13** (not 2) | **Verified** (JTAG) | at 0 only while the command byte (0x2A/0x2B/0x36…) is sent |
| LCD RST | **14** (not 1) | **Verified** (JTAG) | 20–60 ms low pulse at boot and then always at 1 |
| Backlight | no GPIO | **Verified** (JTAG) | the factory firmware configures no other output (only GPIO 10–14) nor LEDC: fixed backlight |
| RGB LED | unknown (WS2812 on 40 is kept) | Unverified | the factory firmware routes neither RMT nor any other output: it does not use the LED |
| BOOT button | 0 | Partial: idle level = 1 (pull-up) read on every boot; a real press is missing | log `BUTTON GPIO0 raw level=1` |
| TF (SDMMC 4-bit) CLK12 CMD16 D0 14 D1 17 D2 21 D3 18 | | Cannot be verified without a card | `sdmmc_init_ocr ... 0x107` (timeout: no card or different pins) |

LCD note: an attempt was made to verify the wiring by reading `RDDID`/`RDDST`/`RDDMADCTL` from the ST7735 in 3-wire mode (bidirectional SDA, bit-bang) with the reference pins and with all 120 permutations (MOSI, SCLK, CS, DC) of GPIO 1–5, with pull-up and pull-down. In no case does the panel drive the line (reading identical to the pull): the module does not allow reading over SDA. The LCD wiring can only be confirmed by looking at the screen.

Observation: on two boots of the standalone spike GPIO0 read 0 continuously for ~40 s (button pressed by someone?). It has not been reproduced again; on the other boots it reads a stable 1.

## Reading the factory firmware over USB-JTAG (2026-10-04)

The factory image was flashed (`hw/factory-backup/`, Arduino-ESP32 2.0.13 + TFT_eSPI-like library, 160×80 GIF) and, with the demo running, registers were read with OpenOCD (`board/esp32s3-builtin.cfg`, no sudo via `sg dialout`):

- GPIO matrix (`GPIO_FUNCn_OUT_SEL_CFG`, 0x60004554+4n): only GPIO10 = 101 (FSPICLK) and GPIO11 = 103 (FSPID); the rest 0x100 (simple GPIO). `GPIO_ENABLE` = 0x7C00 (GPIO 10–14).
- Sampling of `GPIO_OUT` + `SPI2_W0` (300–1500 samples): CS=12 at 0 during sends, DC=13 at 0 during commands; 14 always at 1 after boot (at boot: 0 and then 1 → RST).
- Initialization trace with a *watchpoint* on `SPI2_W0` (0x60024098) from `reset halt`: TFT_eSPI ST7735S sequence (`01, 11, B1–B4, C0–C5, 20, 36 C8, 3A 05, 2A, 2B, E0, E1, 13, 29`), then `21` (INVON) and `36 A8` (MY|MV|BGR, rotation 1). Drawing window: CASET 1..160, RASET 26..105 → column 1 / row 26 offsets in landscape. SPI2 at 40 MHz, mode 0.

Resulting configuration in `firmware/include/mesp_board.h`: MOSI 11, SCLK 10, CS 12, DC 13, RST 14, no BL (`-1`), `MADCTL=0xA8`, inversion ON, offsets 1/26.

## Display configuration under test (old spike, T-Dongle-S3 pins: they do NOT match this board)

ST7735S 80×160 used in landscape (160×80): `MADCTL=0x68` (MX|MV|BGR), inversion ON (`0x21`), RGB565, column 1 / row 26 offset (equivalent to col 26 / row 1 in portrait, the typical value of 0.96" panels in 132×162 RAM). SPI2 at 26 MHz, mode 0.

Pattern: 1 px white frame on the outer edge; squares in the corners (top-left **red**, top-right **green**, bottom-left **blue**, bottom-right **yellow**); large text **"MicroESP"** at the top; four bars labelled **R G B W**; cyan line with the current step of the cycle; magenta line `BTN:n` with the press counter.

## Test cycle (repeats endlessly, ~30 s)

The cyan line on the screen says at all times what is being tested (and the log records it with a timestamp):

| Step | Duration | What is done |
|---|---|---|
| `BL38=0 (LOW)` | 3 s | GPIO38 at 0 |
| `BL38=1 (HIGH)` | 3 s | GPIO38 at 1 |
| `WS2812 G40 RED/GREEN/BLUE` | 3 × 0.7 s | WS2812 via RMT on GPIO40 |
| `WS2812 G39 …` | 2.1 s | same on GPIO39 |
| `WS2812 G48 …` | 2.1 s | same on GPIO48 |
| `WS2812 G38 …` | 2.1 s | same on GPIO38 (the backlight will flicker) |
| `WS2812 G21 …` | 2.1 s | same on GPIO21 |
| `APA D40 C39 …` | 2.1 s | APA102 DI=40 CLK=39 (bit-bang) |
| `APA D39 C40 …` | 2.1 s | APA102 DI=39 CLK=40 |
| `IDLE 5s` | 5 s | nothing |

## User checklist (visual confirmation)

With the dongle plugged into the PC (current firmware: TuyaOpen + TinyUSB + this cycle):

1. **Display**: is the pattern visible?
   - Are all **4 lines** of the white frame visible (top, bottom, left, right)? If one is missing and there is garbage on the opposite side, the offset is wrong: note which side.
   - Does the "MicroESP" text read correctly (not mirrored, not upside down)?
   - Is the top-left corner **red** and the "R" bar **red**? (if they come out blue → RGB/BGR order swapped).
   - Is the background **black**? (if it is white/light → inversion the wrong way round).
2. **Backlight**: during `BL38=0` the screen should be visible; during `BL38=1` it should switch off (active low). Note if it is the other way round or if it does not change.
3. **LED**: note in which step (text of the cyan line) the LED lights up and in which color: e.g. "WS2812 G40: red→green→blue correct" or "APA D40 C39: lights up".
4. **Button**: press BOOT briefly; the `BTN:` counter on the screen should increase. Do **not** hold it ≥2 s: with the TinyUSB firmware that puts the board in download mode (recovered by unplugging and plugging back in).
5. Optional **TF**: if a microSD (FAT) is inserted, it can be tested again with the standalone `hw/spikes/pinout` spike (it mounts at boot and lists the root).
6. Photo of the board on both sides for this document (acceptance criterion of US-0001).

The results are filled into the "Status" table.
