# EuroClack Green Screen 2350 — design review

Reviewed **8 September 2026**, using KiCad **10.0.6**, against the saved schematic, PCB and project settings. **Not ready for fabrication or power-up.** Five previous findings have been corrected in the schematic, but those changes have not reached the PCB. Six other finding groups remain open.

This report replaces the 7 September review. Corrected schematic complaints have been removed from the active issue list. Their implementation status is recorded below so they cannot disappear prematurely from the fabrication checklist. Design files were not edited. The previous report is preserved in [DESIGN_REVIEW.previous.md](analysis/2026-09-08_1152/DESIGN_REVIEW.previous.md).

## Changes since the previous review

| Previous finding | Current schematic evidence | Status |
|---|---|---|
| U12/U23 EN overstress | Both pin 5 EN inputs now have individual unconnected nets. TPS54302 explicitly permits floating EN. | Corrected in schematic; old 12 V connection remains on PCB |
| R4 disconnects +5 V; U12 divider gives 3.934 V | R4 removed; L2 output and R91 connect directly to +5V. R91=75k, R92=10k gives `0.596 × 8.5 = 5.066 V`. | Original fault corrected in schematic; old R4 and 56k remain on PCB |
| C9/C10 short audio input coupling | Both are now 1 µF capacitors. | Corrected in schematic; PCB values remain 0R |
| CV outputs cannot reach ±5 V | DAC-input resistors are 1.96k; negative-reference resistors 6.49k; feedback 10k. Forty current-value DC simulations agree with calculation. | Corrected nominal schematic transfer; eight PCB resistor values remain 20k |
| U2 XSMT floating | Pin 17 now connects to +3.3V, the same rail as DVDD. | Corrected in schematic; PCB still has isolated mute net |

Evidence: [current native connection inventory](analysis/2026-09-08_1152/component_connections.txt), [connectivity audit](analysis/2026-09-08_1152/connectivity_audit.json), [schematic delta](analysis/2026-09-08_1152/schematic_diff.json), and [review delta](analysis/2026-09-08_1152/review_delta.json). Floating EN is supported by [TPS54302, PDF p3](datasheets/TPS54302.pdf). The 5.066 V result resolves the old gross setpoint error; component tolerance and the required rail accuracy still need budgeting.

D3/D4 have also been added to the dedicated gate inputs. That is an attempted protection improvement, not a closed finding: see the comparator section. Library aliases have largely moved to the project-local EuroClack library. The PCB analyzer's tracked geometry/component comparison reports no changes from the prior PCB analysis; this is not a byte-for-byte equality claim.

## Active findings

| Priority | Finding | Evidence / confidence |
|---|---|---|
| Critical | PCB still implements the old supply, mute and component values; D3/D4 are missing | High: native parity and complete common-pin comparison |
| Critical | U13/U20 can overdrive the RP2350 ADC inputs | High: unchanged native nets, manufacturer limits and current DC simulation |
| Critical | U19 remains exposed to negative CV; U21 clamp current is uncontrolled | High connectivity confidence; clamp behavior depends on patch source and diode current |
| Critical | J20 reverses standard STEMMA QT power/signal order | High: native pad mapping and official connector specification |
| Critical, conditional | Q1–Q4 mapping conflicts with AO3400A/AO3401A if those are the intended parts | High conditional confidence; exact identities unresolved |
| Functional | DAC SCL/SDA are swapped relative to RP2350 hardware I2C1 | High: native nets and GPIO function definitions |
| Assembly / startup | TL072 MSOP ordering code and U15 memory remain unspecified; R3 is DNP | High documentation/population confidence; actual part compatibility unresolved |

### 1. Synchronize and route the PCB before release

The fresh DRC found **0 geometry errors, 28 warnings and 0 unconnected items**, but also **234 schematic parity issues**. Zero unrouted connections only describes the PCB's existing net assignments; it does not mean it implements the current schematic.

The following are substantive differences:

- U12.5 remains tied to +12V and U23.5 to /12V_JACK in the PCB, despite the schematic floating-enable correction. The old copper exceeds the TPS54302 EN absolute maximum of 7 V. [TPS54302, PDF pp3–4](datasheets/TPS54302.pdf)
- R4 remains a DNP 0 Ω footprint between L2 and the main +5V/output-capacitor bank. The current schematic has removed it and joined that path. Do not remove its footprint without restoring the copper connection.
- R91 remains 56k on the PCB versus 75k in the schematic. C9/C10 remain 0R versus 1u. R17/R36/R39/R42 remain 20k versus 1.96k; R13/R26/R37/R40 remain 20k versus 6.49k. These **11 value mismatches** can propagate incorrect assembly values.
- U2.17 remains on the old isolated /I2S_DAC_MUTE net instead of +3.3V.
- D3/D4 and all six pads are absent from the PCB. Their circuit also needs the protection corrections below before layout.

Native parity categorizes the 234 items as 199 field mismatches, 13 symbol/footprint mismatches, 19 net conflicts, two missing footprints and one extra footprint. Many field differences concern datasheet metadata. The 13 symbol/footprint mismatches comprise the eleven values above and two JP1/JP2 BOM-attribute differences. Five renamed nets have equivalent connected-pin sets and are not electrical opens.

Update the PCB from the corrected schematic, inspect the affected copper and population data, then rerun parity and DRC. Merely accepting renamed nets or reducing the warning count does not resolve the old EN tracks or R4 supply break. [DRC/parity report](analysis/2026-09-08_1152/drc.json)

### 2. MCU ADC input protection and range remain unresolved

U13 outputs 1/7 and U20 outputs 1/7 still directly drive U1 ADC pins 49/52/53/54. All four amplifiers run on ±12 V; there is no output-side limiting/clamping network. The input resistors are before the amplifiers and cannot protect the MCU from their output voltage.

The unchanged nominal transfer is `VADC = 1.60147 − 0.33 × VCV`:

| External CV | Nominal ADC voltage |
|---:|---:|
| −12 V | 5.561 V |
| −5 V | 3.251 V |
| 0 V | 1.601 V |
| +5 V | −0.049 V |
| +12 V | −2.359 V |

The ±12 V cases exceed the standard ADC GPIO absolute limits, −0.5 V to IOVDD+0.5 V. The +5 V case is outside the normal unipolar measurement range even though −0.049 V alone is not an absolute-maximum violation. Revise scale/offset with tolerance headroom and design output-side current limiting and clamping; include unpowered behavior and ADC settling. **High confidence: native topology, manufacturer electrical limits, and ideal DC checks.** [RP2350, electrical characteristics](datasheets/RP2350.pdf)

### 3. Comparator input protection is only partially addressed

U19 pins 3/5 still connect to the hybrid CV/gate signals through FB12/FB5. R45/R48 are on the parallel amplifier branches, not in series with the comparator inputs. Normal negative CV can therefore drive the ground-powered LM393 inputs below their −0.3 V limit. This previous finding remains unchanged for U19. [LM393, §5.1 and input-range notes](datasheets/LM393.pdf)

D4 now connects U21 pin 3 and D3 connects U21 pin 5 to dual clamps between GND and +5V. Their orientation is consistent with the BAT54SW series-diode pinout. However, the path from each dedicated gate input through the interconnect to its clamp has **no board-side series current-limiting resistor**. R95/R96 are 1 MΩ shunts, not series limiters. Clamp current therefore depends on the external source impedance; positive overvoltage also injects into +5V. A clamp is not a safe substitute for controlling that current and checking the receiving rail's ability to absorb it.

BAT54SW's forward-voltage maximum is 320 mV at 1 mA and rises at higher current, so its presence alone does not establish the LM393's −0.3 V input limit. Add a calculated series/protection arrangement on each comparator branch and evaluate clamp current, negative input behavior and powered/unpowered rails. Avoid clamping the shared bipolar measurement signal before the CV amplifier branch. D3/D4 are also absent from the PCB. **High confidence in missing current limiting; actual stress depends on the connected source.** [BAT54SW, pinning and electrical characteristics](datasheets/BAT54SW.pdf)

Comparator outputs still have no external pull-ups. Firmware pull-ups may provide operation; document startup state and rise-time requirements. Thresholds remain approximately 1.091 V and explicit hysteresis was not found.

### 4. J20 still has the wrong STEMMA QT pin order

| Pad | Current schematic and PCB | STEMMA QT SH convention |
|---|---|---|
| 1 | SCL | GND |
| 2 | SDA | V+ |
| 3 | +3.3V | SDA |
| 4 | GND | SCL |

Correct the pad mapping and verify the mating cable orientation before connecting a standard device. This finding is unchanged. **High confidence: raw/native mapping and the manufacturer's interface documentation.** [Adafruit technical specifications](https://learn.adafruit.com/introducing-adafruit-stemma-qt/technical-specs)

### 5. MOSFET identity and pin mapping remain conditional blockers

Q1/Q4 are still labelled A3400A and Q2/Q3 A3401A without explicit manufacturer MPNs. Their symbols assign 1=gate, 2=drain, 3=source. Candidate AO3400A/AO3401A parts instead use 1=gate, 2=source, 3=drain. If these are intended, the body-diode and switching topology is wrong. Resolve identity and symbol-to-pad mapping before assembly. Q1's nominal 12 V gate-source operation would also leave no margin against AO3400A's ±12 V gate rating. **High confidence conditional on the intended part.** [AO3400A, p1](datasheets/AO3400A.pdf), [AO3401A, p1](datasheets/AO3401A.pdf)

### 6. DAC hardware I²C mapping is still swapped

U1 GPIO22 goes to U3 SCL and GPIO23 to U3 SDA. RP2350's hardware I2C1 function uses GPIO22=SDA and GPIO23=SCL. Swap the connections for hardware I²C, or document and implement a software/PIO bus. The DAC's own symbol pin names are correct. **High confidence: native connectivity and GPIO function definitions.** [RP2350, GPIO22_CTRL/GPIO23_CTRL](datasheets/RP2350.pdf)

### 7. Assembly identity and boot bias remain unresolved

U6/U8/U11/U13/U17/U20 still use generic TL072 values with MSOP-8 3×3 mm footprints. The saved TI family datasheet does not establish a matching ordering code for this footprint. Specify a compatible part/package or change footprints. ±12 V operation itself is not the fault. U15 remains generic SRAM with no exact device to establish its command, voltage and package compatibility. [TL072, package information](datasheets/TL072.pdf)

R3 remains DNP in both schematic and PCB. Restore or justify the flash-CS startup bias against the [RP2350 hardware guide, flash interface](datasheets/RP2350-hardware.pdf). R22 provides a populated pull-up for the second memory. Exact passive ratings, F1 hold/trip current, connector mating and procurement suffixes remain assembly gaps; recognizable value strings do not constitute full MPN coverage.

## Power tree and analog status

| Source | Conversion / consumers | Current assessment |
|---|---|---|
| Eurorack ±12 V | Q1/Q2; six dual amplifier packages | Resolve conditional FET mapping and gate stress |
| +12 V | U12/L2 → +5V | Schematic 5.066 V nominal, direct output path and floating EN; PCB stale |
| +5 V | U14 → +3.3V MCU/digital rail | Budget total current and thermal rise |
| +12 V | U9 → +3.3VA; U16 → /3V3_DAC | Large LDO voltage drop remains |
| +12 V | U10 → +5VA audio ADC | Budget dissipation and loads |
| +5 V | U7 → 3.3 V reference; U11 → −3.3 V | Defines ADC/CV offset accuracy and startup behavior |
| /12V_JACK | U23 → /3V3_SD_SCREEN | 3.305 V nominal; schematic EN corrected, PCB stale |
| MCU 3.3 V | Internal regulator / L3 → +1V1 | Full dynamic load/inrush budget not established |

U12's output-capacitor bank is connected directly in the schematic after the R4 removal; that connection is the supply-path correction, not evidence of converter stability. U23's 100k/22k divider remains nominally correct. Both divider calculations were rerun through ideal DC servo simulations.

The corrected CV summer gives `Vjack = 5.084745763 − 0.002551020408 × code` with MCP4728 internal 2.048 V reference, gain ×1 and normal mode. Codes 33/1993/3953 give approximately +5.000562/+0.000562/−4.999438 V. Firmware must apply those DAC settings, restrict requested CV to ±5 V, and calibrate each channel. The schematic note calls for 0.1% resistor networks. This is a nominal transfer correction, not a measured precision or capacitive-load stability result. [MCP4728, reference selection](datasheets/MCP4728.pdf)

C9/C10 now restore AC isolation into the PCM1808 input network. The former 0 Ω bias-short complaint is removed for the schematic. Validate effective capacitance, low-frequency response and maximum audio input amplitude during bring-up. U2's XSMT is now high in the schematic. [PCM1808, §8.2.2.4](datasheets/PCM1808.pdf), [PCM5102, XSMT pin function](datasheets/PCM5102.pdf)

## Component and connectivity verification

Raw-file counting finds **272 non-power symbol instances, 256 physical references and 273 PCB footprints**, including mechanical/depanelization items. The native netlist contains 256 components. The inventory covers every native component's reference, value, footprint, pin function and net. All **901 common connected schematic/PCB pins** were compared, with 242 equivalent net groups; the substantive differences are listed above. D3/D4 account for six schematic pins absent from the PCB. Separate intentional NC USB VBUS pads explain another grouping difference.

The analyzer reports 94 capacitors, 73 resistors, 23 ICs, 31 connectors, four MOSFETs, four diodes, six switches, two jumpers, sixteen ferrite-category parts, and one each of crystal, other inductor and fuse. Treat this as a classification summary: the native inventory is authoritative. For example, R3 is DNP but the analyzer's summary DNP count omits it; direct inspection finds R3/R101/R102 DNP in the schematic, plus R4 on the old PCB.

Manual manufacturer-family checks and cached PDF evidence support the electrical findings above. They do not establish exact selected-part compatibility for every component. The current inventory and saved PDFs allow repeat checks, but no complete structured extraction cache or per-MPN verification coverage is claimed. The previous `analysis/deep_review.json` describes the old design and is historical only; the current [review_delta.json](analysis/2026-09-08_1152/review_delta.json) supersedes its stale supply, mute, coupling and CV-output conclusions.

## PCB, EMC and manufacturing

The panel remains approximately **109.2×110 mm, four copper layers**, with 3330 track segments, 393 vias and 14 zones. The project now permits 0.09 mm minimum track width, 0.1 mm clearance and 0.05 mm via annular width. The previous small-via concern remains: 392 vias use 0.25 mm pads and 0.15 mm drills, nominally 0.05 mm annular rings. Obtain fabricator acceptance for the actual stackup/process before release; none is specified here.

DRC with in-memory zone refill found 6 silk-edge-clearance, 4 silk-over-copper, 17 silk-overlap warnings and one dangling track. Resolve the QSPI-clock stub and artwork issues. Several checks, including library footprint matching and missing courtyards, are configured as ignored: fewer warnings do not demonstrate that every formerly reported library issue was fixed.

EMC analysis reran with **147 heuristic findings across eight checked categories**. Its aggregate score is not a compliance result. The meaningful retained risks are the jack-section reference geometry, mixed-signal interconnect returns, connector-side ESD protection and clock transitions. J21/J22 still allocate only pin 18 explicitly to ground in each 40-pin interconnect. The MCU/SD sections have inner ground zones, while the jack section relies on outer-layer ground zones. Whole-panel plane-gap estimates can overstate individual signal problems; assess actual return paths per section.

USB routed-length totals retain the previous approximate mismatches of 0.258 mm for the device link and 0.885 mm for the host link. These are trace totals, not a validated 90 Ω differential channel including FFC and packages. Connector-side USB TVS protection was not found. U1 has nine embedded plated thermal holes; do not repeat a missing-thermal-vias heuristic for it. Fiducials were not identified; provide them if required for automated assembly.

The self-powered USB-C implementation leaves VBUS unconnected and has separate 5.1k CC resistors. Firmware attach/VBUS behavior, PIO-host termination and permitted host load remain to be validated. Identify access to protected rails, +1V1, references, SWD/RUN, I²C and audio clocks for bring-up.

## Thermal and simulation results

The thermal analyzer ran but assessed **zero components** and returned SKIPPED. It is not a thermal pass. The previous first-order SPX3819 model remains applicable: 191 °C/W, 40 °C ambient, 125 °C junction ceiling, ignoring ground current. This gives approximate current ceilings of 51 mA for 12→3.3 V and 64 mA for 12→5 V. At the new 5.066 V main rail, the same model gives about 252 mA for U14's 3.3 V output. These are conditional thermal bounds, not approved load ratings. U16 at 32 mA estimates about 93 °C junction; U10 at 11 mA about 55 °C. Establish actual loads, ambient and copper cooling. [SPX3819, PDF p2](datasheets/SPX3819.pdf)

The standard SPICE runner could not find an executable simulator. The existing KiCad ngspice shared-library method was adapted to current values: **40 CV-output channel/load cases plus nine targeted DC cases passed numerical agreement checks**. These tests validate calculations, including the still-unsafe ADC transfer; they do not validate real op-amp limits, transients, regulator stability, thermal operation or damage tolerance. Scripts and results are saved in this run's [CV output checks](analysis/2026-09-08_1152/cv_output_spice/results.json) and [targeted checks](analysis/2026-09-08_1152/spice_targeted/results.json).

## Native checks, false positives and reviewer overrides

| Check | Current result |
|---|---|
| Schematic analyzer | Rerun; 171 findings before manual triage |
| PCB analyzer | Rerun with --full; compared with prior output |
| Cross-domain analyzer | Rerun; 17 findings, including the value/missing-part mismatches |
| KiCad ERC | 1 error, 65 warnings |
| KiCad DRC with parity and refill | 0 geometry errors, 28 warnings, 0 unconnected items, 234 parity items |
| Independent native connectivity | Complete common-pin comparison and missing-pad audit |
| EMC / thermal | Both rerun; heuristic EMC risks / zero thermal assessments |
| SPICE | Standard runner unavailable; 49 shared-library ideal DC checks completed |
| Prior-review delta | Schematic/PCB JSON comparison and manual reconciliation of all 11 prior finding groups |

The ERC error is **#FLG03 connected to #FLG05**, two PWR_FLAG annotations on a common net. Remove the redundant flag; it does not prove physical source contention. Warnings include off-grid endpoints, library-symbol differences, dangling wires/NC markers and alias labels. The retained I2S_DAC_MUTE label now aliases +3.3V and I2S_DAC_SCK aliases GND; clean up misleading labels as appropriate. [ERC report](analysis/2026-09-08_1152/erc.json)

Other overrides retained after checking current nets:

- The analyzer still splits GND_JACK incorrectly. Use the native netlist for ground connectivity conclusions.
- RP2350's 1.1 V core is not its GPIO voltage domain; automated 1.1 V/3.3 V interface warnings are not evidence that all digital buses need level shifters. The ADC overdrive finding instead follows the actual amplifier transfer and GPIO limits.
- REF3033 supplies ADC_AVDD through /VREF_ADC; the automated claim of no DC supply path is not accepted.
- Grounded PCM5102 SCK supports PLL/three-wire operation. PCM1808 mode pins have internal pulldowns, and U11's unused half is a terminated follower.
- Five net renamings have identical common-pin sets. J19/J28 connector-only ground heuristics require the separate power/ground connections to be considered.
- Heuristic EMC plane gaps and missing-filter lists are risks to inspect, not a count of proven emissions failures.

## Review limits and release conditions

No Gerber/drill outputs were present, so fabrication-output analysis was not applicable. No fabricator was selected. Lifecycle audit was not run because full MPN coverage is absent; the prior PCM5102 lifecycle observation was not freshly rechecked and is not presented as a current status claim. Existing PDFs and cached text support family-level checks; complete selected-part pin/package/rating verification remains unavailable for unspecified parts, especially U15 and Q1–Q4.

Not performed: switching-loop/stability, inrush, startup and fault-current simulation; full noise/THD/anti-alias/ADC-settling validation; worst-case tolerance verification; complete load budget; firmware build or memory/USB validation; measured thermal/EMC/ESD tests; new rendered schematic/layout visual review. In-memory refill was used for DRC without saving the board; analyzer copper geometry uses the saved fills.

**Release remains blocked by the stale PCB and the active electrical/assembly findings.** Correct the remaining schematic issues, synchronize and route the PCB, then rerun parity, ERC/DRC and the affected electrical checks. The five corrected schematic findings above should not be reopened as design mistakes; their remaining work is implementation in the PCB and validation of the stated operating assumptions.
