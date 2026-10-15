# Sprint 1 Plan

## Mission

For **warehouse or factory safety managers** who **need to monitor people and moving targets, including robots, vehicles, and machines**, the **UWB TDoA Real-time Localization** is an **indoor tracking prototype** that **shows locations and flags potential safety concerns**. Unlike **monitoring each robot or machine through its own navigation system**, it **uses wearable or attachable UWB tags to track them on one shared map**.

## Target User

The primary target user is a warehouse or factory safety manager responsible for a shared workspace where mobile vehicles, people, robots, and machinery operate together. They need to see the locations of moving vehicles and robots relative to work areas, recognize potential proximity hazards, and know when tracking information is stale or unavailable so they can make informed safety decisions. The first prototype will focus on this user's need to monitor tagged vehicles or robots on a live indoor map.

## User stories

1. As a warehouse robotics engineer, I want to see a tagged robot’s position on an indoor map so that I can monitor where it is without integrating with its onboard navigation software.
   - **Acceptance criteria:** Display three fixed anchors and one moving tag on a 2D indoor map. Calculate the displayed position from simulated UWB arrival timestamps. At three known test positions, compare estimated and true coordinates and report position errors in meters. Display the tag ID and age of its last accepted position.
2. As a warehouse safety manager, I want to distinguish the positions of two tagged robots on the same map so that I can monitor each robot without confusing their identities.
   - **Acceptance criteria:** Display two distinct tag IDs with separate labels or colors. Replay two simulated paths that cross and confirm that the IDs never swap. Report the achieved update rate and missing updates separately for each tag. Demonstrate this with simulation first; repeat with two physical tags after hardware bring-up.
3. As a warehouse safety manager, I want to define safety zones on the indoor map and receive an alert when a tag that is not permitted enters a zone so that I can identify restricted-area intrusions.
   - **Acceptance criteria:** Define and save at least one rectangular zone with a name, map coordinates, and a list of permitted tag IDs; tags not on the list are not permitted. Display the zone boundary on the map and count boundary points as inside. When a fresh, accepted position places a non-permitted tag inside the zone, show an alert within one display update with the tag ID, zone name, and time, and retain it in the session log. Keep the alert active while the tag remains inside without generating duplicate entry events; allow a new entry alert after the tag leaves and re-enters. Test simulated paths for a permitted tag entering, a non-permitted tag staying outside, and a non-permitted tag entering, leaving, and re-entering. Only the non-permitted entries produce zone alerts. Stale or invalid positions must not produce new entry events.
4. As a warehouse safety manager, I want abnormal tag motion and prolonged signal loss to be flagged so that I can investigate unreliable tracking information, including possible external attacks that alter location data.
   - **Acceptance criteria:** Configure and record a maximum plausible robot speed and a prolonged-loss timeout before testing; use five seconds as the initial simulated loss timeout. Flag a position jump whose implied speed exceeds the configured limit when the next update is processed. Raise a prolonged-loss alert within one display update after the timeout expires, separately from Story 1's one-second stale indication. Each alert shows the tag ID, time, and reason and is retained in the session log. Test a normal simulated path, an injected coordinate jump representing altered location data, and a signal gap longer than the timeout; the normal path produces no alerts, and each abnormal case produces the expected alert. Label alerts as abnormal motion or prolonged signal loss, rather than confirmed attacks, because these tests do not establish the cause or detect every attack.
5. As a warehouse safety manager, I want to replay a robot's recorded movement on the indoor map so that I can review where it was during a selected period.
   - **Acceptance criteria:** Save tag IDs, accepted position timestamps, coordinates, and stale or invalid states. Replay a selected recorded session in time order with the same labels and positions, and visibly preserve gaps rather than inventing positions. Label simulated sessions as simulation and confirm that a second run reproduces the same trajectory.

## Feasibility

The planned hardware purchases are:

| Component | Quantity | Unit/package price (USD) | Subtotal (USD) | Purchase link |
| --- | --- | --- | --- | --- |
| Qorvo DWM3000 UWB module (DWM3000TR13, cut tape) | 5 | $23.62 per module | $118.10 | [DigiKey](https://www.digikey.com/en/products/detail/qorvo/DWM3000TR13/24367995) |
| Seeed Studio XIAO ESP32C3 development board | 6 (two 3-packs) | $19.99 per 3-pack | $39.98 | [Amazon 3-pack](https://www.amazon.com/dp/B0DGX3LSC7) |
| JLJLUP protected 3.7 V, 1200 mAh LiPo battery | 2 (one 2-pack) | $15.99 per 2-pack | $15.99 | [Amazon](https://www.amazon.com/dp/B0FH94M8PS) |
| **Total** | | | **$174.07** | |

Prices checked on October 6, 2026; exclude tax, shipping, any applicable tariffs, and miscellaneous parts. Prices may change before purchase.

The five UWB modules and five of the six MCU boards are intended for three anchors and two mobile tags; the sixth MCU board is a spare. Each mobile tag will use one 1200 mAh battery from the selected two-pack. Actual runtime will be determined by measuring the assembled tag's current consumption.

Other miscellaneous parts can be found in RASTIC or purchased as needed.

## Tooling

Planned tools and their purposes:

| Tool | Purpose and reason | Where used |
| --- | --- | --- |
| [KiCad](https://www.kicad.org/about/kicad/) | Draw the wiring schematic and document connections; prepare PCB designs in a later sprint | Developer's laptop |
| C/C++ and [VS Code with the Espressif ESP-IDF extension](https://docs.espressif.com/projects/vscode-esp-idf-extension/en/latest/) | Write, build, upload, and monitor firmware for the anchors and tags; keep board settings and dependencies in a reproducible project configuration | Developer's laptop; firmware runs on the XIAO ESP32C3 |
| [Espressif ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/index.html), including SPI and Wi-Fi APIs | Provide ESP32-C3 board support, communicate with the UWB module over SPI, and forward anchor records over Wi-Fi | Developer's laptop and XIAO ESP32C3 |
| [DW3000 Arduino driver maintained by Makerfabs](https://github.com/Makerfabs/Makerfabs-ESP32-UWB-DW3000) (candidate) | Use existing UWB communication and ranging examples as a reference; port the required driver functions to ESP-IDF and verify XIAO ESP32C3 compatibility and receive-timestamp access | XIAO ESP32C3 connected to DWM3000 |
| Python with NumPy, SciPy, Matplotlib, and pyserial (proposed) | Collect serial test logs, process timing measurements, estimate positions, and plot locations and errors | Developer's laptop |
| Git and GitHub | Track firmware, schematics, documentation, and measurement logs so changes and results can be reviewed | Developer's laptop and project repository |
| RASTIC soldering station and multimeter | Assemble module connections; check continuity, shorts, supply voltage, and current during hardware bring-up | RASTIC electronics workspace |

Use ESP-IDF with target `esp32c3` and configure the XIAO ESP32C3 pin assignments. Record and pin the selected ESP-IDF version and driver commit in the project configuration after the first successful build. The candidate UWB driver's supplied ranging examples do not establish that my TDoA timing and synchronization path works.

## Two Weeks Demo

At the end of two weeks, I will demonstrate the indoor tracking concept with a Python simulation, present the hardware purchased by the demo date and its delivery status, and explain the next steps for building and testing the physical prototype.

- **Concept simulation:** Show three fixed anchors and one moving tag on an indoor map. Generate simulated UWB arrival timestamps assuming synchronized anchor clocks, calculate the tag's estimated 2D position, and compare it with the simulated true position. Display update age and mark the position stale within one second after updates stop. Label the demo "Simulation—no physical UWB hardware."
- **Purchase progress:** Present the items actually ordered, quantities, costs, and expected delivery dates; show photos of any received parts. Distinguish purchased, received, and still-planned items. The feasibility table remains a planned purchase list until orders are confirmed.
- **Next steps:** After parts arrive, assemble and check the electrical connections at RASTIC, bring up the XIAO ESP32C3 and DWM3000 using PlatformIO, and test packet exchange between two nodes. Then expand to three anchors and one tag, test anchor clock correction, and connect real timestamp records to the Python processing and map. Add the second tag after the first works; design a PCB in a later sprint.

The simulation demonstrates the calculation and display concept. Physical UWB accuracy, firmware compatibility, and anchor synchronization remain to be tested with hardware.

## Project Assumption

These are unverified assumptions. The simulation can test the calculation and display logic, but it cannot validate physical UWB timing or accuracy.

1. **Common anchor time reference — the critical assumption:** All anchors can be synchronized, or their clock offsets and drift corrected, to a sufficiently accurate common time reference for uplink TDoA within the project's time and budget. Absolute wall-clock time is unnecessary; the anchors need comparable receive timestamps. If this fails, the planned physical TDoA architecture cannot produce trustworthy positions, even if the simulation works.
2. **Shared hardware and component compatibility:** Tags and anchors can use the same core hardware and wiring design, based on a XIAO ESP32C3 and DWM3000. Mobile tags add battery power, while anchors use a fixed power supply. Their firmware roles and settings may differ. The controller, UWB module, and selected driver are assumed to support packet exchange and the required timestamp access. Test one shared design in both roles before assembling all nodes, and check the tag's battery power path separately.
3. **Acceptable localization error:** The physical system's position error is small enough for the planned monitoring demo, either directly or after calibration. Use the proposed static accuracy targets of median error ≤0.30 m and 95th-percentile error ≤0.75 m as initial criteria. Calibrate on separate data and evaluate at held-out surveyed positions, including obstructed paths. Do not assume calibration can remove every error; if the targets fail, revise anchor placement, the localization approach, or the demonstrated scope.
4. **Simulated tags supplement limited physical tags:** With only one or two physical tags available, additional simulated tags can demonstrate multi-tag display, zone-entry alerts, abnormal-motion alerts, signal-loss handling, and replay. Give simulated tags distinct IDs and visible simulation labels, and test crossing paths and simultaneous events. These tests demonstrate software behavior; physical multi-tag capacity, radio contention, and deployment performance remain unverified.
5. **Useful safety-monitoring information:** A warehouse or factory safety manager can understand the map, freshness indicators, and alerts and finds them useful for reviewing tagged vehicle or robot movement. Test this with a short interview and simulated scenarios, asking the user to identify a restricted-zone entry and a stale position. Treat the prototype as a monitoring demonstration; its suitability for operational safety decisions requires separate validation.

**Test the critical assumption first, using the cheapest physical experiment:** Use one tag and two anchors from the planned hardware, borrowed if available, with USB power and timestamp logs. Defer batteries, PCB design, and interface work for this test. First verify timestamp access, then implement the intended clock-correction method and compare corrected arrival-time differences with those expected from surveyed tag and anchor positions.

**Initial go/no-go criteria:** At every evaluation position, at least 95% of scheduled transmissions must be received and matched by both anchors, and the 95th-percentile absolute timing residual must be no greater than 1 ns. This is a proposed timing feasibility gate, not proof that the complete three-anchor system meets its position-accuracy targets. Time-box the experiment to two focused work sessions after packet exchange and timestamp logging work; record the actual session hours.

**Preferred approach:** Implement uplink TDoA for the physical prototype. Two-way ranging (TWR) is the fallback if anchor synchronization or clock correction cannot meet the timing criteria within the project's time and budget.

**Pivot line:** “I change direction if uplink TDoA cannot meet the timing criteria because of unresolved anchor synchronization or clock-correction issues within the project's time and budget. In that case, I will use two-way ranging (TWR) for the physical prototype. Uplink TDoA remains my preferred approach.”

## Baseline

I will evaluate the prototype against **ground truth: the measured physical location of the tag and the observed time of its movement or zone entry**. This reference lets me measure accuracy and delay; it does not establish that the system performs better than another localization method.

### Multi-location accuracy test

I will mark and survey nine test locations across the indoor test area, including its center and edges, using a common coordinate origin and a fixed tag height. I will keep calibration locations separate from evaluation locations. At each evaluation location, I will hold the tag stationary and record detected positions for 60 seconds. I will repeat the test for each physical tag available.

For every accepted position, I will compare the ground-truth coordinates `(x_true, y_true)` with the detected coordinates `(x_detected, y_detected)`, all measured in centimeters:

- **Coordinate error:** Report the signed x and y differences in cm.
- **Position error:** Calculate `sqrt((x_detected − x_true)² + (y_detected − y_true)²)` in cm. Report median and 95th-percentile error at each location and across the full test.
- **Normalized percentage error:** Calculate `100 × position error / surveyed test-area diagonal`, with both lengths in cm. Record the diagonal before testing and use it for every result. This expresses error relative to the area's size and avoids dividing by coordinates that may be zero.
- **Valid-position percentage:** Report the percentage of scheduled tag transmissions that produce accepted positions, separately from position error, so missing or rejected updates remain visible.

My initial accuracy targets are a median position error of no more than **30 cm** and a 95th-percentile error of no more than **75 cm**. These are proposed targets, not measured results.

### Tracking-delay test

I will move the tag along a marked path through surveyed checkpoints and compare the time it physically crosses each checkpoint with the time the displayed position crosses the same checkpoint line. I will use a recording that shows both the physical movement and the live display on one time reference. I will record at least ten crossings and measure **tracking delay in seconds** as `displayed crossing time − physical crossing time`.

I will report median and maximum delay, the recording's time resolution, and missed crossings. Negative differences will also be retained because position error can make a displayed crossing appear early. This measures observed tracking lag, including localization and display effects. I will also stop tag updates and verify that the display marks the position stale within **one second** of the last accepted estimate.

### Safety-zone alarm test

I will define a rectangular restricted zone whose physical boundary matches its map coordinates. Using a tag that is not permitted in the zone, I will perform at least ten entries and exits. I will compare the observed physical entry times with alarm times using the same recording time reference.

I will measure:

- **Entry detection rate:** The percentage of physical entries that produce an alarm before the tag exits.
- **Alarm delay:** Seconds from physical entry to the displayed alarm, reporting median and maximum delay for detected entries. Early alarms will be recorded separately.
- **False alarms:** Alarms during a tagged path that stays outside the zone or for a permitted tag entering the zone.
- **Event handling:** Whether an alarm remains active inside the zone without duplicate entry events and whether leaving and re-entering produces a new alarm.

The initial functional target is to detect all ten non-permitted entries, produce no alarms in the outside-only or permitted-tag tests, and display an alarm within one display update after a fresh, accepted position first places the tag inside the zone. I will report physical-entry-to-alarm delay separately; the display-update target alone does not guarantee low physical tracking delay. Stale or invalid positions must not generate new entry alarms.

I will retain coordinates, timestamps, position states, and alarm logs so the results can be reviewed. Any tests using additional simulated tags will be labeled as simulation and reported separately from physical measurements.

## Related work

I will use these **nine reference documents**, including four research papers, three hardware documents, and two vendor whitepapers, to guide implementation and evaluation. The selected UWB module is **DWM3000**. Results from other systems are background evidence; I will measure my prototype's performance separately.

### Research papers

1. **Sebastian Krebs and Tom Herter (2024). [Ultra-Wideband Positioning System Based on ESP32 and DWM3000 Modules](https://arxiv.org/abs/2403.10194).** Research preprint describing identical ESP32/DWM3000 boards configured as a tag and anchors using TWR. It informs my shared hardware design and TWR fallback, but does not verify XIAO ESP32C3 compatibility or uplink TDoA synchronization.

2. **Fengyun Zhang et al. (2021). [Design and Implementation of Real-Time Localization System (RTLS) based on UWB and TDoA Algorithm](https://arxiv.org/abs/2112.04839).** Research preprint covering an RTLS architecture, wireless clock synchronization, time-base selection, anchor deployment, and moving-target estimation. It guides my critical anchor-synchronization experiment and TDoA processing pipeline.

3. **Wenda Zhao, Abhishek Goudar, and Angela P. Schoellig (2022). [Finding the Right Place: Sensor Placement for UWB Time Difference of Arrival Localization in Cluttered Indoor Environments](https://arxiv.org/abs/2204.04508).** Published in *IEEE Robotics and Automation Letters*; linked author manuscript. It considers anchor geometry and obstacle-induced measurement bias. It informs my anchor placement and testing at central, edge, and obstructed locations.

4. **Wenda Zhao, Abhishek Goudar, Xinyuan Qiao, and Angela P. Schoellig (2022; manuscript revised 2024). [UTIL: An Ultra-wideband Time-difference-of-arrival Indoor Localization Dataset](https://arxiv.org/abs/2203.14471).** Presents TDoA measurements, receive diagnostics, and ground-truth robot poses using DWM1000 modules. It provides an example of reproducible ground-truth evaluation and recorded-data experiments. I will check hardware and measurement-format differences before adapting its data.

### Hardware documentation

5. **Qorvo. [DWM3000 Product Data Sheet, Rev B, May 2021](https://www.qorvo.com/products/p/DWM3000).** The official product page links the data sheet; a [local copy](../ref/DWM3000%20Data%20Sheet.pdf) is retained in this project. I will use its pin assignments, electrical requirements, SPI interface, and integration guidance for the shared tag/anchor design. Manufacturer performance figures are not measurements of my assembled system.

6. **Seeed Studio. [Getting Started with Seeed Studio XIAO ESP32C3](https://wiki.seeedstudio.com/XIAO_ESP32C3_Getting_Started/).** Official board guide covering pin assignments, development setup, battery connections, and charging guidance. It supports MCU bring-up, SPI wiring, and the mobile tag's power design.

7. **Espressif Systems. [ESP32-C3 Series Datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-c3_datasheet_en.pdf).** Official chip-level reference for electrical characteristics, peripherals, and wireless capabilities. I will read it alongside Seeed's board guide to check controller constraints and distinguish chip capabilities from signals exposed on the XIAO board.

### Vendor whitepapers

8. **HID / Sewio Networks, Lubomir Mraz. [UWB: The Decision-Maker's Guide](https://campaigns.hidglobal.com/all-you-need-to-know-about-ultra-wideband).** Vendor whitepaper on industrial UWB RTLS applications and deployment considerations; a [local copy](../ref/HID-whitepaper-UWB-guide-for-decision-makers.pdf) is retained in this project. It provides context for warehouse and factory monitoring. Its business-benefit and deployment claims are vendor background, not independent evidence of my prototype's accuracy or safety effectiveness.

9. **Qorvo (May 2021). [Getting Back to Basics with Ultra-Wideband (UWB)](../ref/qorvo-getting-back-to-basics-with-ultra-wideband-uwb-white-paper.pdf).** Manufacturer white paper covering UWB timing principles and TWR, TDoA, reverse TDoA, and PDoA topologies. Pages 4–6 explain TWR message exchanges and TDoA's use of synchronized anchor arrival timestamps forwarded to a central location engine. It informs my preferred uplink TDoA architecture and comparison with the TWR fallback. Its accuracy, scalability, and battery-life claims are vendor background, not measured results for my prototype.

The EC601 course planning links remain in the README, and the Makerfabs firmware repository remains in the Tooling section. They are supporting resources outside this nine-document Related work list. Uplink TDoA remains my preferred implementation; TWR is the fallback if synchronization cannot meet the timing criteria.

## Harm and Risk

Employees could be injured if inaccurate or stale locations cause missed safety-zone alarms; repeated false alarms could also reduce trust in warnings.

Linking tag IDs and movement histories to employee identities could expose personal information and daily routines if the data is accessed without permission.

The worst realistic misuse is using safety-tracking data for covert employee surveillance or unfair disciplinary decisions; my prototype should not be relied on as the sole protection against machinery or robots.

