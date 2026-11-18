# Sprint 3–4 Progress Report

After the [Sprint 2 design and fabrication order](SPRINT2.md), work moved to board assembly, ESP-IDF firmware, and two-node ranging. The USB bench test produced distance estimates. Browser ranging over Wi-Fi, calibrated accuracy, and multi-anchor localization still need hardware validation.

## November 14, 2026 bench log

### Assembly and firmware setup

- Populated the [Sprint 2 PCB](SPRINT2.md) by soldering the DWM3000 and attaching the XIAO ESP32C3. Two ESP32-C3 boards were connected to the Windows PC over USB for the ranging test.
- Installed ESP-IDF v6.1 and Espressif's VS Code extension v2.3.0 in the `ESPIDF` profile. The [firmware project](../firmware/README.md) targets `esp32c3` with 4 MB flash and uses the native USB Serial/JTAG console. The starter build and final ranging build both passed. Verified the final application in flash on both boards.
- The second board initially ran an Arduino build on the ESP32-C3. Both boards ran the ESP-IDF project by the time of the ranging test. The initial USB test used the roles listed below.

### DWM3000 SPI and SW1

- The starter SPI diagnostic backed up the original `COM3` flash before programming. It reset the DWM3000 via open-drain GPIO20 and read `0xDECA0302` five times at 1 MHz in SPI mode 0. Both boards later returned that same chip-model ID. The ESP32-C3 runs the driver and controls the DWM3000 over SPI.
- SW1 on `COM3` uses active-low GPIO3. The pin initially read LOW at startup. Enabling the ESP32-C3's internal pull-up restored the HIGH idle state and allowed the physical switch to report three short presses (150, 160, and 190 ms) and one long press (2,830 ms). The [button diagnostic](../firmware/README.md#sw1-button-test) uses 30 ms debounce and reports the duration on release. The long-press threshold is 1,000 ms. The PCB's external pull-up path still needs an electrical check.

### Wi-Fi configuration

- The [configuration dashboard](../firmware/README.md#wi-fi-configuration-dashboard) runs at `http://192.168.4.1` on each board's setup network. It lets the user edit the role, label, and Node ID and saves them to flash. A role change updates the mode LED output and takes effect in the ranging task between exchanges.
- The setup SSID uses the last six hexadecimal digits of the ESP32 Wi-Fi MAC and stays the same when the label changes. The default Node ID comes from the last two MAC bytes, with a fallback for reserved values. Assign a distinct Node ID to each board. The dashboard displays the ESP32 MAC and the DWM3000 chip-model ID for diagnostics.
- Tested the dashboard from the PC on `COM3`. It rejected invalid values and reloaded a saved role, label, and Node ID after reboot. Later switched the project to a shared bench Wi-Fi password and verified the PC could reconnect to `COM3` and read its settings. Windows could see both access points during ranging setup. Assigned `COM4` its Anchor role over USB when the PC could not connect to its dashboard and verified that the role persisted after loading the standard firmware.

### Two-board UWB ranging

| USB port | ESP32 MAC suffix | Setup SSID | Role | Node ID |
| --- | --- | --- | --- | ---: |
| `COM3` | `BA:EC` | `UWM-FABAEC` | Tag | 47852 |
| `COM4` | `F2:AC` | `UWM-FAF2AC` | Anchor | 62124 |

- Integrated the [DW3110 driver](../firmware/components/dw3110/README.md) at pinned revision `67dfbb7`. The double-sided two-way ranging (DS-TWR) exchange uses UWB channel 5. The tag sends a poll, the anchor responds, the tag sends a final frame, and the anchor reports its timing intervals. The tag computes distance from both radios' hardware timestamps.
- Fixed a startup fault by initializing the driver's private state before reading radio status through the driver. Keeping the anchor receiver enabled between host polls resolved the repeated missed exchanges.
- In the initial logged run, the first attempt timed out during startup. Then both serial logs matched for **25 consecutive exchanges**, sequence numbers 1–25. The tag reported a median of **0.704 m** and a range of **0.633–0.800 m**. The physical separation was unmeasured and antenna delays used nominal values, so accuracy remains unverified.
- To repeat the [ranging test](../firmware/README.md#two-way-ranging), power both boards and open **ESP-IDF: Monitor Device** on the tag at 115200 baud. The tag publishes estimates about once a second and the anchor logs matching replies.
- A follow-up capture swapped the roles. COM4 (tag 62124) and COM3 (anchor 47852) had **63 consecutive matching sequence numbers (58–120)** in the overlapping portion of the logs. The tag reported **0.186–0.273 m** in that interval. The separation was unmeasured. The source captures are `COM4-usb-console-check.log` and `COM3-sequence-check.log` in the local bench workspace.

## November 15, 2026 design checks

The first review used KiCad CLI 9.0.2, which failed to load the KiCad 10 schematic and PCB. A later run with KiCad 10.0.3 checked local copies that matched the repository files byte for byte. The electrical rules check reported **29 warnings**. The design rules check reported **six errors** (five antenna keep-out violations and one incomplete thermal relief) and **19 warnings**. The reports are `erc.json` and `drc.json` in the local bench workspace's `.sprint2-build` directory.

## November 15, 2026 live TWR over Wi-Fi

- Added **Live TWR distance** to the tag's Wi-Fi dashboard. The tag forwards four DW3110 timing intervals plus the node IDs and exchange sequence. The computer's browser calculates double-sided TWR distance in meters and clears the reading once the last sample is at least three seconds old. The tag's serial estimate can be matched by sequence during a bench run.
- The bench test uses one tag and one anchor. Load the updated firmware on the tag, connect the computer to the tag's Wi-Fi, and compare the browser readings at measured antenna-to-antenna separations. Record the ground-truth distances, observed range errors, and whether the display clears when the anchor stops replying. See the [wireless bench procedure](../firmware/README.md#two-way-ranging).
- The ESP-IDF 6.1 build passed. The browser logic passed simulated responses for a 0.704 m reference case, stale data, a disconnected link, and recovery. Windows detected no ESP32 serial ports during that session. Flashing and a physical Wi-Fi ranging test remain pending in the project record.

## November 17, 2026 local ranging log

The firmware working tree adds a [local ranging log](../firmware/README.md#local-ranging-log) that retains up to **2,048 records** per board in a dedicated flash partition and overwrites the oldest records as it fills. Each board's dashboard can export those records as CSV. Tag records store all four TWR timing intervals and the distance estimate. Anchor records store the anchor intervals and the lower 32 bits of the poll RX, response TX, and final RX timestamps. Both store node IDs and the exchange sequence. Build, flashing, and hardware validation of this revision remain unverified in the project record.

## Status of the Sprint 1 user stories

All five tracking acceptance tests remain open.

| Sprint 1 story | Evidence and remaining work |
| --- | --- |
| One tag on an indoor map | One tag and one anchor returned pairwise distance estimates. A position solver, measured position error, and a freshness test are still needed. |
| Two distinct tracked tags | The shared board is intended for both node roles. No new crossing-path test, per-tag update-rate report, or physical two-tag test is committed. |
| Restricted-zone alerts | No new implementation or acceptance log for permitted IDs, entry-event handling, or stale/invalid suppression is committed. |
| Abnormal motion and prolonged signal loss | No new speed-threshold logic, loss-timeout implementation, or scenario results are committed. |
| Recorded-movement replay | No new timestamped position recordings, replay implementation, or reproducibility results are committed. |

The [Sprint 1 browser mockup](../work/uwb-site.tar.gz) shows three anchors, two movable tags, and a danger zone. It generates ideal arrival times from positions supplied by dragging. The timestamp-to-position solver and the planned Python simulation remain open.

## Remaining work

1. **Resolve the board findings.** Fix the antenna keep-out and thermal relief violations, then rerun the checks. Confirm the intended LED resistor values and DWM3000 GPIO drive limits, and bring the schematic, PCB, and wiring reference into agreement. The wiring review proposed evaluating 2.2–4.7 kΩ for the radio LEDs against the actual components.
2. **Finish electrical bring-up.** Check the external SW1 pull-up, IRQ, LEDs, sleep/wake behavior, and USB supply and continuity. Inspect grounding, decoupling, antenna clearance, connector orientation, and footprint fit. Measure mobile-tag power consumption and battery runtime.
3. **Measure TWR accuracy and validate data capture.** Compare USB and Wi-Fi ranging at several measured antenna-to-antenna separations, calibrate antenna delays, and check whether the browser clears stale readings. Verify flash logging, CSV export, and persistence across power cycles.
4. **Test uplink TDoA with one tag and two anchors.** Log matched tag and anchor IDs, sequence numbers, raw receive timestamps, and diagnostics. Compare corrected timing against surveyed positions. The [Sprint 1 feasibility gate](SPRINT1.md#project-assumption) requires at least **95% matched reception** and a **95th-percentile absolute timing residual of at most 1 ns** at each test position. Time-box the experiment to two focused sessions and document the TWR fallback decision if the gate cannot be met.
5. **Complete the simulation and tracking acceptance tests.** Implement the timestamp-to-position solver, then test map position, identity, freshness, zone alerts, abnormal motion, loss, and replay. After the timing gate passes, expand to three anchors and one physical tag before adding the second tag and measuring position error against surveyed ground truth.
