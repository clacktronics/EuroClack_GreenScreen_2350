# EuroClack Green Screen 2350 — design review

Reviewed 7 September 2026 against the current KiCad schematic and PCB. **Not ready for fabrication or power-up.** Several populated circuits exceed component limits or cannot provide their documented function. Native ERC/DRC reporting zero errors does not detect these faults.

The review left the design files unchanged. Evidence and reproducible calculations are under `analysis/`; manufacturer PDFs are under `datasheets/`. Reference designators and pin numbers below refer to the current native netlist, not the older PDF supplied with the project.

## Findings requiring correction

| Priority | Finding | Confidence / evidence |
|---|---|---|
| Critical | U12/U23 enable pins receive 12 V, exceeding their 7 V absolute limit | High; native connectivity and manufacturer datasheet |
| Critical | DNP R4 opens the main +5 V supply; U12 divider also sets only 3.934 V | High; raw population flags, connectivity and calculation |
| Critical | Bipolar CV can overdrive the RP2350 ADC inputs | High; native connectivity, limits and simulation |
| Critical | Negative CV reaches ground-powered LM393 inputs without protection | High; native connectivity and datasheet |
| Critical | C9/C10 populated as 0 Ω defeat the audio ADC input coupling/bias | High; raw values, connectivity and datasheet |
| Critical | J20 is wired in the reverse order to standard STEMMA QT | High; native pad mapping and connector convention |
| Critical, conditional | Q1–Q4 source/drain mapping conflicts with AO3400A/AO3401A | High if those are the intended parts; identities unresolved |
| Functional | Four CV outputs produce about 0–1.65 V instead of ±5 V | High; connectivity, calculation and simulation |
| Functional | U2 XSMT has no defined drive | High connectivity confidence; resulting state uncertain |
| Functional | DAC I²C is swapped relative to the MCU hardware peripheral | High; native nets and RP2350 function table |
| Assembly | TL072 MSOP footprints lack a matching specified ordering code; U15 memory unidentified | High documentation confidence; actual part compatibility unresolved |

### 1. Buck regulator enable overstress

U12 pin 5 is tied to +12V; U23 pin 5 is tied to /12V_JACK. Both are TPS54302 EN inputs. Their 7 V absolute maximum is distinct from VIN's higher rating. Apply a valid EN circuit, such as the manufacturer's supported floating-enable arrangement or a calculated divider, before power-up. See [TPS54302, PDF pp3–4 and enable section](https://www.ti.com/lit/ds/symlink/tps54302.pdf). **Datasheet-verified and raw-file verified; high confidence.**

### 2. Main supply disconnected and incorrectly set

U12 SW → L2 → R4 → +5V. R4 is 0 Ω but marked DNP. C38/C57/C58/C60 are downstream of R4, leaving the converter's inductor output without that output-capacitor bank when assembled as specified. The disconnected +5V rail feeds U14, the reference and output circuitry, and the inter-board USB supply.

Even with R4 fitted, R91=56k and R92=10k set `0.596 × (1 + 56/10) = 3.9336 V`, not 5 V. Restore a permanent output/capacitor path and recalculate the divider for the intended rail. U23's 100k/22k divider gives 3.305 V and is nominally consistent with its 3.3 V rail. These calculations use the manufacturer's feedback reference; ideal SPICE independently reproduces both values. **Datasheet-verified, raw-file verified and calculated; high confidence.** [TPS54302](https://www.ti.com/lit/ds/symlink/tps54302.pdf)

### 3. Unprotected MCU ADC inputs

U13 outputs 1/7 and U20 outputs 1/7 connect directly to U1 ADC pins 49/52/53/54. The amplifiers run on ±12 V. Their nominal transfer is `VADC = 1.60147 − 0.33 × VCV`.

| External CV | Calculated ADC voltage |
|---:|---:|
| −12 V | 5.561 V |
| −5 V | 3.251 V |
| 0 V | 1.601 V |
| +5 V | −0.049 V |
| +12 V | −2.359 V |

The ADC pins are standard GPIO, with absolute limits −0.5 V to IOVDD+0.5 V, or 3.8 V with a 3.3 V I/O supply. Even the intended ±5 V range misses one endpoint and lacks tolerance margin. Add appropriately designed series limiting/clamping after the amplifiers and revise the scale/offset. The 100k input resistors precede the amplifiers and cannot protect the MCU from their outputs. Check unpowered startup and ADC settling too. **Datasheet-verified, raw-file verified and ideal-SPICE checked; high confidence.** [RP2350, electrical characteristics, printed pp1335–1339](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf)

### 4. Comparator inputs exposed to negative CV

U19 pins 3/5 receive the hybrid CV/gate signals through ferrites, with its negative supply grounded. R45/R48 are on the parallel amplifier branches and do not limit comparator input current. Normal −5 V CV violates the LM393 input minimum of −0.3 V. U21's dedicated gate inputs have similar exposure to bipolar patches. Add series resistance and suitable protection on each comparator branch without clamping the shared bipolar measurement signal. **Datasheet-verified and raw-file verified; high confidence.** [LM393, §5.1 and input-range notes](https://www.ti.com/lit/ds/symlink/lm393.pdf)

The open-collector comparator outputs also depend on MCU pull-ups: no external pull-ups were found. Firmware pull-ups can provide operation, but defined startup and rise times warrant external bias. Thresholds are approximately 1.091 V; assess chatter because explicit hysteresis is absent.

### 5. Audio ADC coupling removed

C9/C10 have value `0R` and are populated. U6's ground-centred outputs therefore drive U5 VINL/VINR through 1k resistors without AC isolation. PCM1808 expects an input centred at 0.5VCC (2.5 V here); its internal bias cannot overcome this low-impedance zero-volt source. Negative signal swings can also exceed its input minimum. Restore actual coupling capacitors, choosing their low-frequency cutoff, or redesign explicit DC bias. **Datasheet-verified and raw-file verified; high confidence.** [PCM1808, PDF pp4, 6 and 20 §8.2.2.4](https://www.ti.com/lit/ds/symlink/pcm1808.pdf)

### 6. STEMMA QT connector reverses the standard pin assignment

J20 uses the JST SH four-pin footprint, but the schematic assigns the PH-style order:

| Pad | Current J20 | Standard STEMMA QT SH |
|---|---|---|
| 1 | SCL | GND |
| 2 | SDA | V+ |
| 3 | +3.3V | SDA |
| 4 | GND | SCL |

A standard QT cable therefore connects power and signals incorrectly. Correct the pad mapping and verify the mating cable orientation. **Raw-file verified and official interface-document verified; high confidence.** [Adafruit technical specifications](https://learn.adafruit.com/introducing-adafruit-stemma-qt/technical-specs)

### 7. Resolve MOSFET identity and source/drain mapping

The values are `A3400A`/`A3401A`, without explicit manufacturer MPNs. The generic GDS symbols assign pads 1=gate, 2=drain, 3=source. If the intended devices are AO3400A/AO3401A, their actual mapping is 1=gate, 2=source, 3=drain. This changes the body-diode orientation in Q1/Q2 reverse-polarity protection and Q3 USB switching, and invalidates Q4's intended connection.

Confirm the intended parts and correct symbol-to-footprint mapping before assembly. AO3400A's ±12 V gate rating also leaves no margin for Q1 operating at a nominal 12 V gate-source difference. **Raw-file and candidate-datasheet verified; high confidence conditional on identity.** [AO3400A, p1](https://www.aosmd.com/res/data_sheets/AO3400A.pdf), [AO3401A, p1](https://www.aosmd.com/res/data_sheets/AO3401A.pdf)

### 8. CV output transfer does not meet the schematic's ±5 V target

U11 creates −3.3 V. Each U8/U17 summer uses 20k DAC and offset inputs with 10k feedback: `VCV = 1.65 − 0.5 × VDAC`. A 0–3.3 V DAC swing gives approximately 1.65–0 V. R54–R57 are inside the DC feedback and do not restore the missing gain. MCP4728 runs on +3.3VA, so selecting its internal 2.048 V reference at gain two cannot produce 4.096 V beyond its supply. Redesign the reference, supply and resistor ratios together. **Raw-file verified, datasheet-verified and ideal-SPICE checked; high confidence.** [MCP4728, §4.3](https://ww1.microchip.com/downloads/en/DeviceDoc/22187E.pdf)

### 9. Audio DAC unmute signal floats

U2 pin 17/XSMT is the sole pin on /I2S_DAC_MUTE. There is no drive or pull-up. Low means mute and high means unmute; the non-A PCM5102 datasheet does not establish a deterministic floating state. Provide a defined level or MCU control. **Datasheet-verified and raw-file verified; high confidence in missing control, uncertain floating behaviour.** [PCM5102, PDF pp3 and 22](https://www.ti.com/lit/ds/symlink/pcm5102.pdf)

### 10. DAC hardware I²C mapping

U3's own SCL/SDA pins are correct, but U1 GPIO22 connects to DAC SCL and GPIO23 to DAC SDA. The RP2350 hardware I2C1 functions assign GPIO22=SDA and GPIO23=SCL. Swap these nets for hardware I²C, or explicitly commit to a software/PIO implementation. **Raw-file and datasheet-verified; high confidence.** [RP2350, GPIO function table](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf)

### 11. Part and boot configuration gaps

All six TL072 packages use MSOP-8 3×3 mm footprints. The reviewed TI family package information does not establish a matching MSOP ordering code. Select a specific compatible amplifier/package rated for ±12 V or change footprints. The generic value alone is inadequate for assembly. The supply voltage itself is supported by the TL072 family. **Datasheet-backed compatibility gap; high confidence.** [TL072 package information](https://www.ti.com/lit/ds/symlink/tl072.pdf)

U15 is populated as generic `SRAM` using a flash-derived symbol and datasheet association. Its actual memory model, package and command compatibility remain unverified. U4's W25Q128JV pin functions match the expected SPI/QSPI mapping, but full ordering codes should be recorded. R3, the flash CS pull-up, is DNP; restore the power-up bias recommended by the [RP2350 hardware guide, flash interface section](https://datasheets.raspberrypi.com/rp2350/hardware-design-with-rp2350.pdf). R22 provides a populated pull-up for the second memory's CS.

## Power tree and thermal assessment

| Source | Conversion / consumers | Assessment |
|---|---|---|
| Eurorack ±12 V | Q1/Q2 protection; TL072 stages | Resolve FET pin mapping and gate rating |
| +12 V | U12 buck → L2/R4 → intended +5 V | EN overstress, open R4 and wrong divider |
| +5 V | U14 → +3.3V for MCU/digital loads | Dependent on main buck correction |
| +12 V | U9 → +3.3VA; U16 → /3V3_DAC | Significant linear-regulator dissipation |
| +12 V | U10 → +5VA for ADC | Lower estimated load, still verify actual budget |
| +5 V | U7 reference → U11 negative reference | Offset accuracy and startup affect CV channels |
| Inter-board /12V_JACK | U23 → 3.3 V jack rail | Divider nominally correct; EN overstressed |
| MCU 3.3 V | Internal regulator / L3 → +1V1 | No full frequency-dependent current budget established |

The thermal analyzer ran but assessed **zero components**; its result is SKIPPED, not a pass. Manual first-order SPX3819 estimates use 191 °C/W, 40 °C ambient and a 125 °C junction ceiling. Ignoring ground current, maximum currents from that model are approximately 51 mA for 12→3.3 V, 64 mA for 12→5 V, and 262 mA for 5→3.3 V. These are layout-dependent thermal bounds, not approved load ratings.

U16 at the PCM5102's 32 mA tabulated analog/charge-pump maximum dissipates about 0.278 W, implying 93 °C junction at 40 °C ambient before additional losses. U10 at 11 mA dissipates 0.077 W, implying about 55 °C. Establish full loads, ambient, duty cycles and copper cooling, especially U14 and external connector loads. The regulator's advertised 500 mA does not guarantee thermal feasibility. **Datasheet-based estimates; medium confidence in actual board temperatures.** [SPX3819, PDF p2](https://www.maxlinear.com/ds/spx3819.pdf)

## Connectivity, component and pin verification

The raw schematic contains 271 non-power symbol instances representing **255 physical component references**. The PCB has **273 footprints**, including 18 mechanical/depanelization instances. The native netlist and PCB agree on 903 common connected pins; no schematic pins are missing from the PCB. A complete reference/pin/net/footprint inventory is provided in [component_connections.txt](analysis/component_connections.txt), with machine-readable comparisons in [connectivity_audit.json](analysis/connectivity_audit.json).

| Component category | Count |
|---|---:|
| Capacitors | 94 |
| Resistors | 74 |
| ICs | 23 |
| Connectors | 31 |
| MOSFETs / diodes | 4 / 2 |
| Switches / jumpers | 6 / 2 |
| Ferrite-category parts, including some inductors | 16 |
| Other inductor / crystal / fuse | 1 / 1 / 1 |

DNP flags are present on R3, R4, R101 and R102. Explicit manufacturer-part-number property coverage is zero; recognizable value strings are not full assembly specifications.

Manufacturer PDFs were manually reviewed for critical family pin functions and use: RP2350, TPS54302, PCM1808, PCM5102, REF3033, MCP4728, SPX3819, LM393, TL072, SN74LVC1T45, BAT54SW and W25Q128JV, plus candidate AO MOSFETs. This does not establish exact ordering-code/package verification for every fitted component. Connector mechanical mating, unspecified passives' voltage/current ratings, U15 identity and exact package suffixes remain **unverified**. Two-terminal passive logical pinout is generally not meaningful; polarity and physical rating still require selected-part checks. No structured datasheet extraction cache was produced; there is no extraction-verified coverage claim.

## PCB, EMC and manufacturing

The layout is a four-layer, nominally 1.6 mm panel containing MCU, jack and SD sections, approximately 109.2×110 mm overall. The project uses 0.1 mm minimum clearance and traces. **392 vias use 0.25 mm pads with 0.15 mm holes, leaving 0.05 mm nominal annular rings.** Confirm these against the selected fabricator's finished-hole and registration capabilities before release; no fabricator has been specified, so this is a manufacturing constraint rather than a universal rejection.

The MCU and SD sections have inner ground zones. The jack section has ground zones on the outer layers only; its inner layers do not provide a continuous ground plane beneath outer-layer signals. J21/J22 provide only one explicit ground pin (18) in a 40-pin interconnect carrying mixed analog, digital, USB and power signals. Improve the return allocation and verify cable orientation and return paths. These are **raw-file-backed EMC risks**, not proof of emissions failure.

Approximate routed USB net-length totals, excluding FFC cable and packages, differ by 0.258 mm for the device link and 0.885 mm for the host link. These sums do not prove 90 Ω differential impedance or point-to-point timing compliance. Cable construction and the jack-section reference geometry are more consequential unresolved checks. USB ESD protection was not found. Review connector-side TVS placement, switching-node copper and board-edge routing/stitching.

U1's exposed pad includes nine plated thermal holes, so a heuristic claim of missing thermal vias is incorrect. No assembly fiducials were identified. Add appropriate fiducials if automated assembly is intended. Resolve silkscreen overlaps/edge clearance, library portability and the small dangling QSPI-clock track stub before release. No fabrication outputs were supplied, so layer output, drill alignment and final panel/depanelization rules remain unchecked.

The USB-C device connector intentionally leaves VBUS unconnected and uses separate 5.1k CC resistors: this is a self-powered implementation. Verify VBUS/attach behaviour in firmware. The host port uses GPIO/PIO USB, with R101/R102 external pulldown positions DNP; document termination ownership and the supported host load. F1's value `nSMD005 150mA` does not establish an unambiguous hold/trip-current specification.

For bring-up, expose or identify access to protected ±12 V, buck outputs, +3.3 V, +1V1, references, SWD/RUN, I²C and audio clocks. After corrections, start with current-limited supplies and verify rail and reference sequencing before connecting external CV or USB devices.

## Checks run and trust summary

| Check | Result / interpretation |
|---|---|
| Schematic analyzer | Full native schematic analysis; findings manually triaged |
| PCB analyzer | Full layout analysis |
| Cross-domain analyzer | Schematic/PCB correlation |
| Native KiCad 9.0.6 ERC | 0 errors, 226 warnings |
| Native DRC | 0 errors, 193 warnings, 0 unconnected items, 18 parity warnings |
| Zone refill and repeat DRC | Performed on a copy; 0 errors and 0 unconnected items; 229 warnings due partly to copy-local library resolution |
| Independent connectivity comparison | 903 common pins; 248 equivalent net groups; intentional NC USB VBUS grouping explains residual differences |
| EMC analyzer | 147 findings across 8 checked categories; heuristic score rejected as a compliance measure |
| Thermal analyzer | Ran, zero assessments; supplemented by manual estimates |
| SPICE | Nine targeted ideal DC checks through KiCad's ngspice shared library; all agree with hand calculations |
| Manufacturer datasheet review | Manual, family-specific coverage; exact MPN and package gaps remain |

The standard automatic SPICE runner could not find an executable simulator. The custom shared-library runner and saved netlists are in [spice_targeted](analysis/spice_targeted/results.json). Passing these numerical checks confirms the reported transfer functions, including the erroneous ones; it is not a functional pass. [Engineering calculations](analysis/engineering_calculations.json) and [deep-review evidence](analysis/deep_review.json) retain the supporting values and anchors.

**Trust summary:** critical electrical findings are grounded in raw/native connectivity plus manufacturer limits or reproducible calculations. MOSFET/package findings are conditional on unresolved procurement identity. Thermal, EMC and firmware-dependent conclusions retain medium or conditional confidence. No claim of complete datasheet coverage, compliance certification or manufacturing acceptance is made.

The deep-review evidence gate accepted 10 entries with zero quarantined claims. Three received partial evidence status because its `pdftotext` dependency was unavailable; PDF text anchors were independently read using pypdf. The gate validates schema, design anchors and evidence-file availability; its acceptance does not independently prove the engineering conclusion. Assembly and manufacturing gaps are documented in this report beyond those ten electrical entries.

## False positives and reviewer overrides

- Fourteen native parity warnings concern five renamed nets (CV_IN_1 and CV_OUT_1–4); their connected pin sets match. The other four concern JP1/JP2 BOM flags and U1/L3 footprint-library aliases. Resolve metadata, but do not treat these as 18 electrical opens.
- The schematic analyzer splits GND_JACK incorrectly. Ground-related automated scores cannot be accepted without native-netlist and copper checks.
- Most native warnings concern missing or mismatched library links, not missing embedded copper: original DRC includes 163 library warnings; ERC includes 165 footprint-link warnings. Preserve the project libraries for reproducibility.
- PCM5102 SCK tied to ground is valid PLL/three-wire operation. PCM1808 mode pins have internal pulldowns; open jumpers do not inherently leave them undefined.
- TL072 operation on ±12 V is not itself a fault; the unresolved package/ordering code is. U11's unused half is terminated as a follower.
- J19/J28 SD cable mapping deliberately reverses the pin order, with separate power/ground provision. A local connector-only missing-ground heuristic is insufficient.
- The DAC chip's SCL/SDA pins are correctly labelled; the remaining swap is at the RP2350 hardware function mapping.

## Existing notes, lifecycle and review limits

There was no previous formal review baseline for a run-to-run delta. The supplied schematic PDF is older and RP2040-based; a fresh [current schematic PDF](analysis/native/schematic_current.pdf) was exported. Existing schematic notes about the DAC mute and CV scaling remain relevant. Notes alleging unsuitable TL072 supply voltage are not supported; the I²C concern requires the MCU-side distinction above.

PCM5102 is marked not recommended for new designs in the reviewed manufacturer lifecycle information. A complete lifecycle audit was not performed because exact MPNs are absent. This status is a sourcing/longevity consideration, separate from the electrical blockers.

Not performed: Gerber/drill verification (no outputs); selected-fabricator rule validation (none named); complete per-part package/rating verification (MPNs missing); regulator switching-loop/stability and inrush simulation; analog noise/THD, ADC settling and anti-alias validation; worst-case resistor/reference tolerances; full operating power budget; measured thermal/EMC/ESD performance; firmware build/USB-host and memory validation. U15 identity and the intended Q1–Q4 part numbers remain unanswered.

Correct the electrical and part-selection blockers first, then rerun ERC/DRC and connectivity checks, regenerate manufacturing outputs and perform a controlled bring-up. **This revision should not be released for fabrication as reviewed.**
