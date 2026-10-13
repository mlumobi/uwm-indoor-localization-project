# Sprint 1 Plan

## Mission

For a warehouse robotics engineer who needs to monitor multiple mobile robots indoors, the UWB Indoor Localization Project is a tracking prototype that displays robot positions and freshness; unlike integrating separately with each robot’s navigation system, it uses attachable UWB tags to support different robot platforms and expand tracking by adding tags.

## Target User

The primary target user is a warehouse or factory safety manager responsible for a shared workspace where mobile vehicles, people, robots, and machinery operate together. They need to see the locations of moving vehicles and robots relative to work areas, recognize potential proximity hazards, and know when tracking information is stale or unavailable so they can make informed safety decisions. The first prototype will focus on this user's need to monitor tagged vehicles or robots on a live indoor map.

## User stories

1. As a warehouse robotics engineer, I want to see a tagged robot’s position on an indoor map so that I can monitor where it is without integrating with its onboard navigation software.
   - **Acceptance criteria:** Display three fixed anchors and one moving tag on a 2D indoor map. Calculate the displayed position from simulated UWB arrival timestamps. At three known test positions, compare estimated and true coordinates and report position errors in meters. Display the tag ID and age of its last accepted position. Mark the position stale within one second after updates stop. Clearly label the demo "Simulation—no physical UWB hardware." Repeat the position checks with a physical tag after hardware bring-up.
2. As a warehouse safety manager, I want to distinguish the positions of two tagged robots on the same map so that I can monitor each robot without confusing their identities.
   - **Acceptance criteria:** Display two distinct tag IDs with separate labels or colors. Replay two simulated paths that cross and confirm that the IDs never swap. Report the achieved update rate and missing updates separately for each tag. Demonstrate this with simulation first; repeat with two physical tags after hardware bring-up.
3. As a warehouse safety manager, I want to see when a robot's position is stale or unavailable so that I do not mistake an old location for its current position.
   - **Acceptance criteria:** Show the age of the last accepted position. Stop one tag's updates and mark its position stale within one second of its last accepted update while the other tag continues updating. Preserve the last known position with a stale label, and clear that label when valid updates resume.
4. As a warehouse safety manager, I want abnormal tag motion and prolonged signal loss to be flagged so that I can investigate unreliable tracking information, including possible external attacks that alter location data.
   - **Acceptance criteria:** Configure and record a maximum plausible robot speed and a prolonged-loss timeout before testing; use five seconds as the initial simulated loss timeout. Flag a position jump whose implied speed exceeds the configured limit when the next update is processed. Raise a prolonged-loss alert within one display update after the timeout expires, separately from Story 3's one-second stale indication. Each alert shows the tag ID, time, and reason and is retained in the session log. Test a normal simulated path, an injected coordinate jump representing altered location data, and a signal gap longer than the timeout; the normal path produces no alerts, and each abnormal case produces the expected alert. Label alerts as abnormal motion or prolonged signal loss, rather than confirmed attacks, because these tests do not establish the cause or detect every attack.
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
| C++ and [VS Code with PlatformIO](https://docs.platformio.org/en/latest/integration/ide/vscode.html), using the Arduino framework | Write, build, upload, and monitor firmware for the anchors and tags; keep board settings and dependencies in a reproducible project configuration | Developer's laptop; firmware runs on the XIAO ESP32C3 |
| [Espressif Arduino-ESP32 core](https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html), including SPI and Wi-Fi libraries | Provide ESP32-C3 board support, communicate with the UWB module over SPI, and forward anchor records over Wi-Fi | Developer's laptop and XIAO ESP32C3 |
| [DW3000 Arduino driver maintained by Makerfabs](https://github.com/Makerfabs/Makerfabs-ESP32-UWB-DW3000) (candidate) | Start from existing UWB communication and ranging examples; adapt pins and verify XIAO ESP32C3 compatibility and receive-timestamp access | XIAO ESP32C3 connected to DWM3000 |
| Python with NumPy, SciPy, Matplotlib, and pyserial (proposed) | Collect serial test logs, process timing measurements, estimate positions, and plot locations and errors | Developer's laptop |
| Git and GitHub | Track firmware, schematics, documentation, and measurement logs so changes and results can be reviewed | Developer's laptop and project repository |
| RASTIC soldering station and multimeter | Assemble module connections; check continuity, shorts, supply voltage, and current during hardware bring-up | RASTIC electronics workspace |

Use PlatformIO board ID `seeed_xiao_esp32c3` with `framework = arduino`. Record and pin the selected PlatformIO platform/core versions and driver commit in the project configuration after the first successful build. The candidate UWB driver's supplied ranging examples do not establish that our TDoA timing and synchronization path works.

## Two Weeks Demo

At the end of two weeks, we will demonstrate the indoor tracking concept with a Python simulation, present the hardware purchased by the demo date and its delivery status, and explain the next steps for building and testing the physical prototype.

- **Concept simulation:** Show three fixed anchors and one moving tag on an indoor map. Generate simulated UWB arrival timestamps assuming synchronized anchor clocks, calculate the tag's estimated 2D position, and compare it with the simulated true position. Display update age and mark the position stale within one second after updates stop. Label the demo "Simulation—no physical UWB hardware."
- **Purchase progress:** Present the items actually ordered, quantities, costs, and expected delivery dates; show photos of any received parts. Distinguish purchased, received, and still-planned items. The feasibility table remains a planned purchase list until orders are confirmed.
- **Next steps:** After parts arrive, assemble and check the electrical connections at RASTIC, bring up the XIAO ESP32C3 and DWM3000 using PlatformIO, and test packet exchange between two nodes. Then expand to three anchors and one tag, test anchor clock correction, and connect real timestamp records to the Python processing and map. Add the second tag after the first works; design a PCB in a later sprint.

The simulation demonstrates the calculation and display concept. Physical UWB accuracy, firmware compatibility, and anchor synchronization remain to be tested with hardware.

## Project Assumption


## Baseline


## Related work


## Harm and Risk
