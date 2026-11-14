# Sprint 2 Progress Report

## Scope and objective

Sprint 2 advances **UWB TDoA Real-time Localization** from project planning to a documented hardware design for the shared tag/anchor node. The target user remains a warehouse or factory safety manager who needs to monitor tagged robots and vehicles on one indoor map.

This report covers repository changes after the latest [Sprint 1 plan](SPRINT1.md) update, commit `3ba8cbd` on **October 15, 2026**, through commit `d1c8b9b` on **November 11, 2026**. The schematic and PCB development commits are dated October 27, followed by a schematic resistor-value revision on November 11. The repository was reviewed on **November 14, 2026**; these dates describe the available commit history, not an inferred sprint schedule.

**Result:** A KiCad schematic, local component libraries, a wiring reference, and a PCB layout with routed copper and ground pours are now committed. Firmware, physical bring-up, anchor clock correction, and measured localization performance remain unverified.

## Progress beyond Sprint 1

| Area | Sprint 1 baseline | Added in Sprint 2 | Current status |
| --- | --- | --- | --- |
| Shared node hardware | Proposed XIAO ESP32C3 and DWM3000 hardware for tags and anchors | KiCad project and schematic connecting both devices, power, indicators, and a sleep/wake button | Design files committed; hardware operation unverified |
| Electrical interface | Planned SPI communication and timestamp access | Pin-by-pin wiring reference, reset and mode-LED pin revisions, IRQ pull-down, button pull-up, and a later LED resistor revision | Connections documented; resistor values need reconciliation across artifacts |
| Expansion access | No committed node schematic or expansion connectors | Two six-pin headers exposing spare MCU pins and radio signals, including SYNC | Header assignments committed; external circuits untested |
| PCB | PCB design deferred to a later sprint | Component placement, board outline, mounting holes, copper routing, vias, ground pours, and a keep-out area | Layout developed; ERC/DRC and fabrication readiness unverified |
| Component libraries | Hardware references and purchase plan | Custom symbols and project-local footprints for the XIAO and DWM3000 | Libraries committed; physical fit still needs checking |
| Tracking software and evaluation | User stories, acceptance criteria, proposed Python simulation, and an archived browser visualization | No new firmware, localization implementation, or measurement logs in this comparison | Sprint 1 functional acceptance criteria remain open |

## Completed design work

### 1. Shared XIAO ESP32C3 / DWM3000 schematic

The [KiCad schematic](../sch/uwm_esp32/uwm_esp32.kicad_sch) now defines one core board intended to support either the tag or anchor role. Role selection is planned through firmware or a software interface; the schematic does not contain a separate role-selection button.

The design includes:

- SPI connections between the XIAO ESP32C3 and DWM3000, plus radio reset and IRQ.
- A common 3.3 V supply and ground, with C1 = 100 nF and C2 = 10 µF for local decoupling.
- Two opposite-state Tag/Anchor LEDs controlled by one MCU GPIO, each with its own series resistor.
- Four radio activity LEDs for RXOK, SFD, RX, and TX, each with a separate series resistor.
- An active-low sleep/wake button with a 10 kΩ external pull-up.
- A 100 kΩ IRQ pull-down to define the input level while the radio sleeps.

The current design uses the XIAO power pins and has no separate battery or external-power connector. Mobile-tag battery integration and runtime remain future work.

### 2. Reviewed pin assignments and firmware requirements

The [wiring reference, revision 1.3](../sch/uwb_pin_connections.md) records the schematic connections, connector numbering, and firmware requirements.

| Function | XIAO pin | ESP32-C3 GPIO | Required behavior |
| --- | --- | ---: | --- |
| SPI MISO | D2 | 4 | SPI input |
| SPI MOSI | D5 | 7 | SPI output |
| SPI clock | D4 | 6 | SPI clock |
| SPI chip select | D3 | 5 | Active-low; use SPI Mode 0 |
| DWM3000 reset | D7 | 20 | Open-drain: pull LOW or release |
| DWM3000 IRQ | D10 | 10 | Active-high input; external 100 kΩ pull-down |
| Tag/Anchor LEDs | D6 | 21 | LOW = Tag; HIGH = Anchor |
| Sleep/wake button | D1 | 3 | Active-low input; external 10 kΩ pull-up |

Reset moved to GPIO20 and the mode LEDs moved to GPIO21. This avoids UART0 TX boot messages driving the radio reset line, although the LEDs may flicker during startup. Firmware must configure reset as open-drain and avoid subsequently enabling UART0 on these pins.

The current schematic is the source for connection assignments; the wiring reference documents the October 27 revision and has not incorporated the later resistor-value change described below. In particular, commit `277c92e` is titled “Add IRQ pull-up,” but the current circuit uses an IRQ **pull-down**. Older pin definitions and intermediate commit titles should not be used as the current wiring specification.

### 3. Expansion headers

The former test-point symbols were replaced by two 1×6, 2.54 mm headers:

| Header | Signals in connector pin order, 1–6 | Intended use |
| --- | --- | --- |
| J1 — ESP32 Edge Header | +3V3, D0/GPIO2, GND, D8/GPIO8, D9/GPIO9, GND | MCU expansion and probing |
| J2 — DWM Edge Header | GND, EXTON, SYNC, DWM GPIO4, SPIPHA, SPIPOL | Radio expansion and synchronization experiments |

J1 exposes MCU strapping pins, so attached circuits must preserve the required startup levels. J2 exposes radio signals rather than spare ESP32 GPIOs; external equipment must respect their direction and startup behavior. Exposing SYNC provides access for experiments but does not implement or validate anchor synchronization.

### 4. PCB layout and component libraries

The [PCB layout](../sch/uwm_esp32/uwm_esp32.kicad_pcb) contains:

- Front and back copper layers on a nominal 1.6 mm board.
- An approximately **85.60 × 53.98 mm** rounded rectangular outline and four M3 mounting-hole footprints.
- Placed MCU, UWB module, headers, button, LEDs, resistors, and capacitors.
- Copper tracks, vias, ground pours on both copper layers, and a keep-out area beside the UWB module.
- Revised routing for D0, IRQ, and SPI, plus adjustments to ground-via placement.
- XIAO through-hole pads sized at 1.6 mm with 1.0 mm drills, reflected in the local XIAO footprint as well as the board.

The repository also includes the [custom symbol library](../sch/uwm_esp32.kicad_sym), [XIAO footprint](../sch/uwm_esp32/uwb_esp32.pretty/MODULE_XIAO_ESP32C3.kicad_mod), [DWM3000 footprint](../sch/uwm_esp32/uwb_esp32.pretty/XCVR_DWM3000TR13.kicad_mod), and [project footprint-library table](../sch/uwm_esp32/fp-lib-table). The table uses `${KIPRJMOD}` to locate the footprint library within the project.

These artifacts establish layout progress. They do not establish complete connectivity, antenna-clearance compliance, correct physical fit, or readiness to manufacture.

### 5. Later schematic resistor revision

Commit `d1c8b9b` on November 11 changes R1–R6 from **1 kΩ to 330 Ω** in the schematic and adds a dated KiCad backup archive. The PCB and wiring reference still specify 1 kΩ, so this revision has not been propagated across all design artifacts. The changed values document a design choice, not measured LED current or validated GPIO loading. Reconcile the intended values and verify the electrical limits before assembly or fabrication.

## Status of the Sprint 1 user stories

Hardware design supports the original stories, but none can be marked accepted from the added repository evidence.

| Sprint 1 story | Evidence and remaining work |
| --- | --- |
| One tag on an indoor map | Hardware interface is now defined. No new timestamp-to-position solver, three-position error report, or freshness test is committed. |
| Two distinct tracked tags | The shared board is intended for both node roles. No new crossing-path test, per-tag update-rate report, or physical two-tag test is committed. |
| Restricted-zone alerts | No new implementation or acceptance log for permitted IDs, entry-event handling, or stale/invalid suppression is committed. |
| Abnormal motion and prolonged signal loss | No new speed-threshold logic, loss-timeout implementation, or scenario results are committed. |
| Recorded-movement replay | No new timestamped position recordings, replay implementation, or reproducibility results are committed. |

The existing [browser visualization archive](../work/uwb-site.tar.gz) was added in `3ba8cbd`, so it belongs to the comparison baseline rather than the new Sprint 2 work. Its HTML contains three anchors, two manually movable tags, a rectangular danger zone, and ideal arrival-time calculations. It explicitly states that times are generated from the dragged position and that no position solver is applied. Static source inspection of this archive does not establish completion of the planned Python simulation or the Sprint 1 acceptance tests.

## Review and validation

Repository review compared `3ba8cbd..d1c8b9b`, inspected the schematic and PCB data, checked the local footprints, and read the wiring reference and archived visualization source. The design details above describe the saved files, not measured hardware behavior.

**ERC/DRC remain unverified.** During this review, both checks were attempted with the installed KiCad CLI **9.0.2**. The schematic and board declare generator version **10.0**, and the CLI returned “Failed to load schematic” and “Failed to load board.” No successful ERC or DRC report was produced. Repeat the checks with a compatible KiCad version; these load failures are not electrical-rule results.

The repository contains no new evidence confirming component orders or receipt, assembly, successful firmware builds, packet exchange, receive-timestamp logging, current consumption, or localization accuracy. The Sprint 1 budget remains a purchase plan rather than proof of spending.

## Remaining risks and lessons

- **The critical timing assumption is still open.** A schematic, routed board, and SYNC header do not demonstrate comparable anchor timestamps. Uplink TDoA remains the preferred approach, with TWR as the documented fallback.
- **LED resistor values and radio GPIO loading need review.** The schematic now specifies 330 Ω for R1–R6, while the PCB and wiring reference retain 1 kΩ. The wiring review already identified a possible mismatch between 1 kΩ radio LED resistors and the DWM3000 GPIO drive specification, recommending evaluation of 2.2–4.7 kΩ against the actual LEDs. Lowering resistance to 330 Ω increases the expected load at the same output and LED voltages. Confirm the intended values and allowable drive current, then synchronize the schematic, PCB, and wiring reference.
- **Layout and assembly checks are still required.** Verify antenna keep-out, grounding, decoupling placement, supply capacity, connector orientation, and footprint fit against the actual parts. The mounting-hole footprints also retain placeholder references.
- **Hardware work moved ahead of the original feasibility sequence.** Sprint 1 recommended testing timestamp access and clock correction before PCB and interface work. Sprint 2 developed the PCB first; the timing risk therefore remains unresolved despite the additional design work.
- **Documentation needs to follow implementation.** The README still describes the repository as being at the proposal stage and omits the new hardware artifacts. Future status updates should distinguish design progress, running software, and measured results.

## Next sprint priorities

These are proposed follow-up tasks, not completed Sprint 2 results.

1. **Validate the electrical design with compatible KiCad tooling.** Reconcile the LED resistor values and verify GPIO loading, then run ERC and DRC, inspect schematic/PCB consistency, resolve or document findings, verify footprints and antenna clearance, and retain review reports before generating fabrication outputs.
2. **Create a reproducible ESP-IDF bring-up project.** Target `esp32c3`, use the reviewed GPIO assignments, pin the ESP-IDF version and selected driver commit, and demonstrate SPI device identification, reset, IRQ, LEDs, and button behavior. Record USB-powered supply and continuity checks.
3. **Verify packet exchange and timestamp access.** Start with two nodes, then use one tag and two anchors. Retain tag IDs, sequence numbers, raw receive timestamps, and diagnostics so receptions can be matched and missing frames counted.
4. **Run the Sprint 1 timing feasibility gate.** After packet exchange and logging work, time-box clock-correction evaluation to two focused work sessions and record the hours. At each evaluation position, require at least 95% matched reception and a 95th-percentile absolute timing residual no greater than 1 ns. If the gate cannot be met within the project constraints, document the TWR fallback decision.
5. **Complete the simulation and tracking acceptance evidence.** Implement the timestamp-to-position calculation and record the original map, identity, freshness, zone, motion, loss, and replay tests. After timing feasibility is established, expand to three anchors and one physical tag, then add the second tag and collect the planned ground-truth measurements.

## Commit evidence

| Commits | Contribution |
| --- | --- |
| `e0f0667` | Initial ESP32C3/DWM3000 KiCad design |
| `277c92e`, `2c90e97`, `71a7b0d` | Bias circuitry and updated schematic connection documentation |
| `14658c6`, `6649af6` | Expansion headers and reviewed header documentation |
| `f6708a4`, `ac58d68` | PCB layout and component-placement revisions |
| `2377b3c`, `ca693e3` | XIAO drill and pad dimensions |
| `6e1b214`, `be9c92e` | Board routing, ground pour, and ground-via placement |
| `028a430`, `f86ac31`, `421d775` | Connection, D0, IRQ, and SPI routing refinements |
| `d1c8b9b` | Schematic R1–R6 revision to 330 Ω and a dated design backup; PCB and wiring-reference values remain unchanged |

Commit `b15fabe` adds a KiCad schematic lock file; it is not a functional hardware milestone. The reviewed endpoint is `d1c8b9b`.
