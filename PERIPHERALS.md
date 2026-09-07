# EuroClack Green Screen 2350 — peripheral / firmware reference

Basis: current RP2350 schematic and PCB, 7 September 2026. **GP numbers are MCU GPIO numbers; `U1.n` means physical QFN-80 pad n.** This is the RP2350B / 48-GPIO design, not the RP2040 design in the older root PDF. All I²C addresses below are **7-bit**. Logic is nominally 3.3 V unless stated otherwise.

Evidence: [native component connections](analysis/component_connections.txt), PCB comparison and matching [source hashes](analysis/manifest.json); linked datasheets for protocols; existing demo for the display. Pin mappings have high connectivity confidence; analog formulas are nominal calculations. Panel settings and unidentified memory remain conditional. Firmware operation has not been tested.

## Before bring-up

Resolve the power and input-protection faults in [DESIGN_REVIEW.md](DESIGN_REVIEW.md) before operating this revision: U12/U23 EN overstress; DNP R4 and the incorrect main +5 V divider; candidate MOSFET pinout mismatches; unprotected bipolar CV/comparator inputs. Audio input coupling, DAC mute and CV output scaling also need the corrections described below. Firmware cannot repair these electrical faults.

Power enters **J18: 1–2 = −12 V, 3–8 = GND, 9–10 = +12 V**, through Q1/Q2. Intended rails: U12 → +5 V; U14 → digital +3.3 V; U9 → +3.3VA for CV DAC; U10 → +5VA for audio ADC; U16 → 3V3_DAC for audio DAC; U23 → 3V3_SD_SCREEN on jack board. U7 supplies the 3.3 V ADC reference; U11 generates −3.3 V for CV offsets. These supplies have no software address or MCU enable. Y1 is annotated **12 MHz**; match the clock configuration to the fitted crystal.

## MCU pin allocation

| Peripheral / signal | GPIO → U1 physical pad | Interface / direction at MCU |
|---|---|---|
| External I²C SDA, SCL | GP4 → 1; GP5 → 2 | Hardware **I2C0**, open drain |
| CV DAC SCL, SDA | GP22 → 22; GP23 → 23 | **Software/PIO I²C** as wired |
| Audio DAC BCK, LRCK, DIN | GP6 → 3; GP7 → 4; GP8 → 6 | PIO I²S, all outputs |
| Audio ADC BCK, LRCK, DOUT | GP9 → 7; GP10 → 8; GP11 → 9 | PIO I²S: clocks out, data in |
| Audio ADC SCKI / MCLK | GP21 → 21 | CLOCK_GPOUT0 or suitable PIO clock |
| Display DC, CS, SCK, MOSI | GP12 → 11; GP13 → 12; GP14 → 13; GP15 → 14 | GPIO DC/CS; **SPI1** clock/TX |
| Display RESET, backlight | GP16 → 16; GP17 → 17 | GPIO reset; GPIO/PWM backlight |
| Gate outputs 1, 2 | GP18 → 18; GP19 → 19 | Digital outputs, active high |
| USB host D−, D+ | GP1 → 78; GP2 → 79 | PIO USB |
| USB host power control | GP24 → 25 | GPIO; intended **high = on** |
| MENU / BOOT, USER1, USER2 | GP25 → 26; GP26 → 27; GP27 → 28 | Inputs, pull-ups, active low |
| Encoder 1 switch, A, B | GP28 → 36; GP29 → 37; GP30 → 38 | Inputs with pull-ups |
| Encoder 2 switch, A, B | GP31 → 39; GP32 → 40; GP33 → 42 | Inputs with pull-ups |
| SD CLK, CMD/MOSI, DAT0/MISO | GP34 → 43; GP35 → 44; GP36 → 45 | **SPI0**, or PIO SDIO |
| SD DAT1, DAT2, DAT3/CS | GP37 → 46; GP38 → 47; GP39 → 48 | SDIO data; GP39 GPIO CS in SPI mode |
| CV inputs 1–4 | GP40 → 49; GP41 → 52; GP42 → 53; GP43 → 54 | ADC channels **0, 1, 2, 3** |
| Gate inputs 1–4 | GP44 → 55; GP45 → 56; GP46 → 57; GP47 → 58 | Digital inputs with pull-ups |
| Second QSPI memory CS | GP0 → 77 | QMI CS1n, active low |
| USB device D−, D+ | Dedicated pads 66, 67 | Native USB controller; no GP number |
| SWCLK, SWDIO, RUN | Dedicated pads 33, 34, 35 | Debug / reset; RUN active low |
| Unconnected GPIOs | GP3 → 80; GP20 → 20 | No peripheral / connector assigned |

Use an RP2350B board configuration supporting GPIOs 0–47 and its ADC mapping; do not inherit Pico 2's physical pin map, GP25 LED assumption, or 4 MiB flash size. Hardware function assignments: [RP2350 §1.2](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf).

## CV DAC — U3 MCP4728, four outputs

- **Address:** normally `0x60`; probe `0x60–0x67` for a reprogrammed/factory-selected address. At `0x60`, wire address bytes are `0xC0` write / `0xC1` read. Commands replace a numbered-register map.
- **Bus:** GP22 → U3.2 SCL; GP23 → U3.3 SDA. R105/R106 provide 10 kΩ pull-ups. Start at 100 kHz, drive low/release high and check ACKs. Hardware I2C1 would require GP22=SDA, GP23=SCL: use software/PIO or physically swap the nets.
- **Startup:** explicitly select normal operation (`PD=00`), VDD reference (`VREF=0`), gain 1. For `ch=0..3` and 12-bit `code`, send volatile Multi-Write payload `[0x40 | (ch << 1), (code >> 8) & 0x0F, code & 0xFF]`.
- **Updates:** Fast Write sends `[(code >> 8) & 0x0F, code & 0xFF]` per channel, A→D. LDAC (U3.4) is grounded: ordinary writes update sequentially. RDY/BUSY (U3.5) is NC. Use volatile writes for CV; reserve EEPROM writes for startup presets. Reprogramming the address requires unavailable LDAC transitions. [MCP4728 §§5.3–5.6](https://ww1.microchip.com/downloads/en/DeviceDoc/22187E.pdf)

| DAC channel | U3 output | Analog stage | Front-panel output |
|---|---|---|---|
| A / 0 | Pin 6 | U8A | CV1, J7 tip |
| B / 1 | Pin 7 | U8B | CV2, J8 tip |
| C / 2 | Pin 8 | U17A | CV3, J9 tip |
| D / 3 | Pin 9 | U17B | CV4, J10 tip |

**Actual nominal transfer:** `Vout = 1.65 − 0.5 × Vdac`; with VDD=3.3 V, `Vdac ≈ 3.3 × code/4096`. Increasing code decreases jack voltage. Code 0 gives about +1.65 V; 4095 gives about +0.0004 V. The present circuit cannot produce its intended ±5 V range. Internal-reference gain 2 cannot yield 4.096 V from a 3.3 V supply. Recalculate and calibrate this mapping after correcting the analog stage.

## CV inputs and gate inputs

| Front-panel input | Analog path | ADC channel / GPIO | Comparator path / GPIO |
|---|---|---|---|
| CV1, J3 tip | U13A output 1 | ADC0 / GP40 | None |
| CV2, J4 tip | U13B output 7 | ADC1 / GP41 | None |
| CV/Gate3, J5 tip | U20A output 1 | ADC2 / GP42 | U19.1 → GP46 |
| CV/Gate4, J13 tip | U20B output 7 | ADC3 / GP43 | U19.7 → GP47 |
| Gate1, J23 tip | None | None | U21.1 → GP44 |
| Gate2, J24 tip | None | None | U21.7 → GP45 |

ADC reference is U7's nominal 3.3 V on U1.59. Configure GP40–43 as analog inputs, disable pulls and select ADC0–3. For a raw 12-bit sample `r`, `Vadc ≈ r × Vref/4096`; current nominal scaling is `Vadc = 1.60147 − 0.33 × Vjack`, hence `Vjack ≈ (1.60147 − Vadc)/0.33`. Calibrate offset/gain per channel. The nominal ±5 V input range is not fully contained within the ADC range; protection and scaling must be corrected before bipolar tests.

GP44–47 are **digital gate inputs**, even though their pads also support ADC4–7. Enable pull-ups: LM393 outputs are open collector and have no external output pull-ups. Jack voltage above approximately **1.091 V reads high**; below threshold reads low. Hybrid CV/Gate3 and 4 feed both paths simultaneously. There is no explicit hysteresis; debounce/qualify slow or noisy edges. Negative voltages require the hardware protection correction. [LM393 output behaviour](https://www.ti.com/lit/ds/symlink/lm393.pdf)

## Gate outputs

| Output | MCU → level shifter | Front-panel connection |
|---|---|---|
| Gate1 | GP18 → U18.3 A; U18.4 B → R44 / FB14 | J26 tip |
| Gate2 | GP19 → U22.3 A; U22.4 B → R53 / FB13 | J27 tip |

U18/U22 are SN74LVC1T45 with DIR tied high, VCCA=3.3 V and VCCB=5 V: **non-inverting, nominal 0/5 V gates**. Initialise MCU outputs low before enabling output direction. Each jack has a 1 kΩ series output resistor; loaded high voltage is lower than 5 V. No address, enable or feedback input. [SN74LVC1T45 function table](https://www.ti.com/lit/ds/symlink/sn74lvc1t45.pdf)

## Audio — separate I²S input and output links

Neither audio converter has an I²C/SPI control address. Use PIO I²S plus DMA; pins named BCK/LRCK on the two converters are separate nets.

| Converter | Wiring | Fixed configuration / initial stream |
|---|---|---|
| U2 PCM5102 DAC | GP6 → pin13 BCK; GP7 → pin15 LRCK; GP8 → pin14 DIN | Pin16 FMT=0: I²S. Pin10 DEMP=0: off. Pin11 FLT=0: normal latency. Pin12 SCK=GND: BCK-derived PLL, no MCLK wire required |
| U5 PCM1808 ADC | GP9 → pin8 BCK; GP10 → pin7 LRCK; pin9 DOUT → GP11; GP21 → pin6 SCKI | Pin12 FMT=0: 24-bit I²S. MD1/MD0 are low via internal pull-downs (JP1/JP2 can ground them): **slave**, MCU supplies all clocks |

Start with **48 kHz LRCK, 64 BCK/frame = 3.072 MHz, 32-bit slots** on both links. ADC data are signed 24-bit MSB-first samples in the slots; observe the one-bit I²S delay and sign-extend according to the driver's packing. Supply ADC SCKI at **256×fs = 12.288 MHz**, synchronised with its LRCK/BCK. Derive clocks from a common source; nominally similar independent frequencies can cause repeated ADC resynchronisation. Discard startup/fade-in samples. [PCM1808 §§7.3–7.4](https://www.ti.com/lit/ds/symlink/pcm1808.pdf)

| Jack / numbered channel | Converter channel | Circuit note |
|---|---|---|
| Input 1: J15 tip | **U5 VINR, pin14 — right slot** | U6B → C10 → R15; gain ≈ −0.33 |
| Input 2: J2 tip | **U5 VINL, pin13 — left slot** | U6A → C9 → R16; gain ≈ −0.33 |
| Output 1: J14 tip | U2 OUTL pin6 — left slot | R1 / C1 / FB6 output filter |
| Output 2: J6 tip | U2 OUTR pin7 — right slot | R2 / C2 / FB7 output filter |

**Hardware dependencies:** C9/C10 are populated as 0 Ω, defeating ADC input coupling; restore coupling/bias before recording. U2.17 XSMT has no drive or pull-up and no MCU connection: provide a defined high level to unmute. DAC full-scale output is nominally 2.1 Vrms before load attenuation; there is no Eurorack output gain stage. [PCM5102 pin functions and output specification](https://www.ti.com/lit/ds/symlink/pcm5102.pdf)

## Display — J12 header / J30 FFC

These connectors are parallel alternatives for the same display signals. **SCL/SDA here mean SPI clock/data, not I²C.**

| Connector pin | Signal | MCU |
|---|---|---|
| 1 | GND | GND_JACK |
| 2 | Supply | 3V3_SD_SCREEN |
| 3 | SCL / SCK | GP14, SPI1 SCK |
| 4 | SDA / DIN / MOSI | GP15, SPI1 TX |
| 5 | RES | GP16, active low |
| 6 | DC | GP12, 0=command / 1=data |
| 7 | CS | GP13, active low |
| 8 | BLK | GP17, high=on in demo; PWM optional |

The schematic does not identify the panel controller; the [Tempest demo](demo_scripts/nv3030b_tempest/nv3030b_tempest.ino) targets **NV3030B, 280×240 landscape**. Reuse its complete `lcd_init()` vendor sequence for that panel: mode 0, MSB-first, RGB565 (`0x3A=0x05`), rotation `0x36=0x68`, column window 20–299, row window 0–239. Commands `0x2A/0x2B` set the window; `0x2C` writes pixels, high byte first. Reset low 5 ms, then high 50 ms; software reset waits 150 ms; sleep-out waits 120 ms. Keep backlight off until initialisation completes.

No MISO or touch-controller connection exists. Keep GP12 as DC; do not let SPI1 RX configuration take it over. The demo requests 125 MHz SPI: treat that as a demo setting, not a guaranteed panel/cable limit; start slower and validate. The net names `HSTX12...17` do not require HSTX mode. Panel identity and safe bus/backlight limits still depend on the fitted module.

## microSD — J1 and SD interconnects

Use **SPI0** initially: GP34=SCK, GP35=MOSI/CMD, GP36=MISO/DAT0, **GP39=software-controlled CS/DAT3**. GP39 is not SPI0's hardware CS function. Leave GP37/38 as inputs with pull-ups in SPI mode. No separate card-detect switch is wired; detect insertion by card responses. The same six signals support a suitable PIO 4-bit SDIO driver.

| Card signal | J1 pad | MCU-side J19 pin | Card-side J11 / J28 pin |
|---|---|---|---|
| DAT2 | 1 | 1 → GP38 | 8 |
| DAT3 / CS | 2 | 2 → GP39 | 7 |
| CMD / MOSI | 3 | 3 → GP35 | 6 |
| VDD | 4 | 4 unconnected | 5 |
| CLK | 5 | 5 → GP34 | 4 |
| VSS | 6 | 6 unconnected | 3 |
| DAT0 / MISO | 7 | 7 → GP36 | 2 |
| DAT1 | 8 | 8 → GP37 | 1 |

J19's signal mapping requires **1↔8, 2↔7, …** relative to J11/J28. Power is supplied separately via **J29.5 = 3V3_SD_SCREEN, J29.3 = GND_JACK**, matching card-side pins 5 and 3; other J29 pins are NC. Verify actual mating/cable orientation. J1 shield pad9 is NC.

Use an SD SPI library that initialises at ≤400 kHz, identifies card capacity/addressing, then raises the clock and mounts the filesystem. CS selects the card; there is no fixed device address. Provide suitable CMD/DAT pull-ups; none were found as external resistors here. [SD Association specifications](https://www.sdcard.org/downloads/pls/)

## External I²C — J20 / J31

Hardware **I2C0: GP4=SDA, GP5=SCL**, with R104/R103 10 kΩ pull-ups to +3.3 V. Start at 100 kHz; addresses depend on the attached peripheral. This bus is separate from U3's DAC bus.

| Connector | Actual pad assignment |
|---|---|
| J20, labelled STEMMA_QT | **1=SCL, 2=SDA, 3=3.3 V, 4=GND** |
| J31, three-pin | **1=SCL, 2=GND, 3=SDA**, mounting pads=GND; no power pin |

**J20 is reversed relative to standard STEMMA QT SH wiring** (1=GND, 2=V+, 3=SDA, 4=SCL). Correct the connector or use a deliberately remapped adapter before connecting a standard QT cable. [Adafruit connector specification](https://learn.adafruit.com/introducing-adafruit-stemma-qt/technical-specs)

## USB

| Port | Data path | Power / setup |
|---|---|---|
| J16 USB-C device | U1.66 DM via R43=27 Ω; U1.67 DP via R24=27 Ω → J21/J22 → J16 | Native USB device stack; connector VBUS is NC, board is self-powered. CC1/CC2 each have 5.1 kΩ to ground. There is no VBUS-sense GPIO |
| J17 USB-A host | GP1=D−, GP2=D+ → J21/J22 → J17 pins2/3 | PIO USB host stack; VBUS comes from 5V_JACK through Q3/F1; J17.4=GND |

For Pico-PIO-USB configure **`pin_dp=2`, `pinout=PIO_USB_PINOUT_DMDP`**, meaning D−=D+−1. Reserve the library's PIO state machines, DMA and timer so they do not collide with audio or SDIO. Host device addresses are assigned during enumeration. [Driver configuration](https://raw.githubusercontent.com/sekigon-gonnoc/Pico-PIO-USB/main/src/pio_usb_configuration.h), [resource requirements](https://github.com/sekigon-gonnoc/Pico-PIO-USB)

**Host power polarity:** despite the `~USBH_PWR_EN` net name, the intended Q4→Q3 circuit gives **GP24 high=VBUS on, low=off**. R100 pulls this signal toward 5 V, so reset does not guarantee power-off. Resolve the Q3/Q4 part/pad ambiguity before relying on switching; initialise low, then enable after host setup. No overcurrent-status input exists. F1's ambiguous `nSMD005 150mA` value does not establish a usable host current budget. R101/R102 host data pulldown positions are DNP; ensure termination is supplied appropriately. Device-port attach/VBUS handling also needs a deliberate self-powered implementation.

## Buttons, encoders, boot and debug

| Control | Reference | Wiring / behaviour |
|---|---|---|
| USER1 | SW1 | GP26, switch closes to ground |
| USER2 | SW2 | GP27, switch closes to ground |
| Encoder 1 | SW3 | A=GP29, B=GP30, push=GP28; common/push return=GND |
| Encoder 2 | SW4 | A=GP32, B=GP33, push=GP31; common/push return=GND |
| MENU / BOOT | SW6 | GP25; also connected through **R23=1 kΩ to flash CS** |
| RUN / reset | SW7 | U1.35 RUN directly; not a readable application GPIO |
| SWD header | J25 | **1=SWCLK, 2=GND, 3=SWDIO**; no target-power pin |

Use input pull-ups and debounce switch contacts; decode both encoder phases and determine clockwise sign mechanically. The demos call SW4 “encoder 1” and SW3 “encoder 2”, and swap each A/B naming convention; use the schematic assignments above for board-level names.

Hold SW6, press/release SW7, then release SW6 to request ROM USB boot (normal boot configuration). Do not drive GP25 as an output: it is shared with flash CS. Flash traffic can appear in GP25 reads; a reliable runtime MENU read needs a flash-idle, RAM-resident sampling routine with interrupts and the other core coordinated, then normal flash operation restored. Restore DNP R3's intended flash-CS bias as noted in the review. Use J25/SWD for recovery and debugging; no separate UART connector or user LED is fitted.

## Boot flash and second memory

| QSPI signal | U1 pad | U4 W25Q128JVS | U15 “SRAM” |
|---|---|---|---|
| CS0n | 75 | Pin1 | — |
| CS1n | GP0 / 77 | — | Pin1 |
| SCK | 71 | Pin6 | Pin6 |
| IO0 | 72 | Pin5 | Pin5 |
| IO1 | 74 | Pin2 | Pin2 |
| IO2 | 73 | Pin3 | Pin3 |
| IO3 | 70 | Pin7 | Pin7 |
| Supply / ground | QSPI I/O=3.3 V | Pin8=3.3 V, pin4=GND | Pin8=3.3 V, pin4=GND |

U4 is labelled **128 Mbit = 16 MiB** boot flash; normal QMI/XIP window starts at `0x10000000`. Set the board's flash size and boot configuration for the fitted device; use its JEDEC ID/SFDP and supported SDK flash routines instead of hard-coding an assumed suffix. The cached Winbond PDF covers a DTR variant, so it does not establish the exact fitted ordering code.

U15 shares all clock/data pins and uses GP0's QMI CS1n function. Its optional mapped window starts at **`0x11000000` after configuration**. The design specifies only “SRAM”; actual part number, capacity, commands, dummy cycles and maximum clock are unknown. Keep CS1 high and leave external RAM disabled until that part is identified and a matching QMI/PSRAM initialiser is selected. R22 supplies a 10 kΩ CS1 pull-up. Avoid changing the shared QMI bus while executing from flash. [RP2350 QMI §12.14](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf)

## Main inter-board cable — J21 ↔ J22

Required mapping is **same-number to same-number**; the cable/contact orientation must realise that mapping. MCU-side `_MICRO` and jack-side `_JACK` labels are separate native nets joined by this cable.

| Pins | Signals, in listed order |
|---|---|
| 1–4 | Audio input1, input2, output1, output2 |
| 5–8 | Hybrid CV/gate3, hybrid CV/gate4, gate input1, gate input2 |
| 9–12 | Host D+, host D−, device D+, device D− |
| 13–16 | Gate output1, gate output2, CV input1, CV input2 |
| 17–18 | +5 V, GND |
| 19–24 | Display DC, CS, SCK, MOSI, RESET, backlight |
| 25 | +12 V |
| 26–29 | MENU/BOOT, RUN, USER1, USER2 |
| 30–35 | Encoder1 push/A/B, encoder2 push/A/B |
| 36 | USB host power control |
| 37–40 | **CV output2, output4, output3, output1** |

All front-panel signal jacks use **tip=signal, sleeve=GND_JACK; ring is NC**. The 40-way link carries power and the only explicit ground connection between these sections, so correct assembly is required even for peripherals using separate data headers.

## Suggested firmware bring-up order

1. After hardware corrections, measure rails/references; establish SWD or ROM USB boot and an RP2350B build with correct crystal/flash settings.
2. Set gate outputs low, host power low, display CS high/backlight low; configure button, encoder and gate-input pull-ups. Leave unidentified U15 disabled.
3. Exercise controls/display; probe external I2C0 and the separate software/PIO DAC bus. Set explicit DAC configuration and calibrated safe output codes.
4. Start DMA audio with zero DAC samples and coherent ADC clocks; check the documented channel swap. Measure CV/gate channels with controlled inputs after protection corrections.
5. Initialise SD over SPI0, then PIO USB host. Allocate PIO/DMA centrally and recheck clocks under simultaneous display/audio/storage/USB load.
