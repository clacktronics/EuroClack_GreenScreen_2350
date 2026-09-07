# EuroClack Green Screen 2350

EuroClack Green Screen 2350 is a **programmable Eurorack synthesizer module built around the Raspberry Pi RP2350B**. It combines stereo audio input/output, control-voltage (CV) and gate interfaces, a colour display, hands-on controls, microSD storage and USB connectivity.

The module provides a general-purpose platform for developing digital instruments, audio effects, sequencers and modulation tools within a Eurorack system. Application firmware defines its musical behaviour, while the audio, CV and gate interfaces connect it to the rest of the patch. The display and physical controls support interactive visual instruments and direct parameter editing.

The included firmware demonstrates a Tempest-inspired game and a double-pendulum simulation with sound. Effects processing, sampling and sequencing require their own application firmware.

## Main functions

| Function | Circuit | Purpose |
|---|---|---|
| Processing | U1, RP2350B in an 80-pin package | Runs the application, processes audio and CV, generates gates and manages the interface. |
| Program storage | U4, W25Q128JVS | External 128-Mbit / 16-MiB flash for firmware and stored data. |
| Additional memory provision | U15, labelled `SRAM` | Shares the QSPI data/clock bus with a separate chip select. The actual memory part and capacity are unspecified. |
| Two audio inputs | U6 TL072 stages and U5 PCM1808 | Condition analog signals and convert them to digital audio for the processor over I²S. |
| Two audio outputs | U2 PCM5102 and output filters | Convert the processor's I²S stream into left/right analog audio. |
| Four CV inputs | U13/U20 amplifier stages and MCU ADC inputs | Scale and offset external control voltages for measurement by the processor. |
| Four gate-sensing paths | U19/U21 LM393 comparators | Detect gate/trigger levels: two share CV inputs and two use dedicated gate jacks. |
| Four CV outputs | U3 MCP4728 and U8/U17 amplifier stages | Convert four digitally programmed values into analog control voltages. |
| Two gate outputs | U18/U22 SN74LVC1T45 | Translate processor logic into nominal 0/5-V gate signals through series output resistors. |
| Display and controls | SPI display connections, two push encoders and buttons | Provide graphical feedback, parameter editing and navigation. |
| Storage and expansion | microSD, external I²C, USB-C device and USB-A host connections | Provide hardware paths for files, peripherals and computer/device communication, subject to firmware support. |

There are **six physical CV/gate input jacks**: two CV-only, two shared CV/gate and two gate-only. The shared inputs are measured by both an ADC path and a comparator path, allowing firmware to read a control voltage and detect gate transitions from the same connection.

## How signals move through the circuit

```text
Audio inputs --> analog conditioning --> PCM1808 ADC --I²S--> RP2350B
Audio outputs <-- output filters <----- PCM5102 DAC <--I²S-- RP2350B

CV inputs ----> scaling and offset -----------------------> MCU ADCs
Gate inputs --> comparator thresholds -------------------> MCU GPIO
CV outputs <--- amplifier stages <----- MCP4728 DAC <------ MCU control
Gate outputs <- 3.3-V to 5-V translation <----------------- MCU GPIO

                     RP2350B
                        |
         Display / encoders / buttons / microSD / USB / I²C
```

The audio converters handle continuous sampled sound. The separate CV DAC and gate circuitry let firmware interact with other modular instruments: reading modulation, following clock or trigger events, and generating control signals. The analog stages adapt these signals to the processor and converters; their resistor ratios and reference voltages determine the actual voltage ranges.

## Display, controls and example firmware

The supplied sketches target an **NV3030B colour TFT at 280 × 240 pixels in landscape orientation**. The schematic provides parallel eight-pin header and FFC display connections; the fitted panel must match the driver and pinout.

- [Tempest demo](demo_scripts/nv3030b_tempest/nv3030b_tempest.ino): an encoder-controlled arcade-style game with audio output.
- [Double-pendulum demo](demo_scripts/nv3030b_pendulum/nv3030b_pendulum.ino): a visual physics simulation whose second pendulum's X/Y position drives the left/right audio channels.

The sketches demonstrate display rendering, encoder input and audio generation. USB MIDI, sample playback, sequencing and effects require additional application firmware. See [PERIPHERALS.md](PERIPHERALS.md) for GPIO assignments, converter configuration and firmware considerations.

## Power and physical construction

Power enters through the ten-pin Eurorack connector J18 using +12 V, ground and −12 V. The design includes MOSFET protection circuitry, switching and linear regulators for its intended 5-V and 3.3-V rails, and a 3.3-V reference with an inverted reference for CV conditioning. The analog amplifier stages use the bipolar rails. USB-C VBUS is unconnected, so this design is intended to be self-powered rather than powered from a computer's USB port.

The PCB contains processor, jack/control and SD-extension sections. A 40-way FFC links the processor and jack sections, carrying signals and power; separate connections serve the SD extension. Cable orientation and pin mapping are documented in [PERIPHERALS.md](PERIPHERALS.md).

## Current development status

This is a prototype design with documented electrical issues. The existing [DESIGN_REVIEW.md](DESIGN_REVIEW.md) reports power-supply faults, input-protection gaps, audio coupling/mute issues and incorrect CV output scaling. In particular, **the intended ±5-V CV range is not an established working capability**: the review calculates approximately 0–1.65 V for the current CV output stage. Read that review before assembly or power-up.

This README documents the module's architecture and application scope. Electrical findings and validation limits are recorded in the design review.

## Project files

- `EuroClack - Green_Screen_2350.kicad_pro` — open this project in KiCad.
- `EuroClack - Green_Screen_2350.kicad_sch` — current circuit schematic.
- `EuroClack - Green_Screen_2350.kicad_pcb` — PCB layout.
- [libraries/README.md](libraries/README.md) — bundled custom libraries and dependencies. The schematic uses KiCad 10 format; the PCB uses KiCad 9 format.
- [PERIPHERALS.md](PERIPHERALS.md) — detailed connections and firmware reference.
- [DESIGN_REVIEW.md](DESIGN_REVIEW.md) — existing engineering review and limitations.
- `demo_scripts/` — example display, controls and audio sketches.

The root PDF represents an older RP2040 design; use the native RP2350 KiCad schematic as the source for this revision.
