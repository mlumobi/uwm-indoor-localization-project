# UWB Indoor Localization Project

An EC601 course project to build and evaluate an indoor location system for tagged objects using ultra wideband (UWB) radio and uplink time difference of arrival (TDoA).

This is a one-person project. Mingjin Lu is responsible for hardware integration, firmware, localization software, evaluation, and documentation, with AI assistance.

**Status:** Proposal stage. This repository currently contains the project proposal and reference documents. Firmware, localization software, and measured performance results are not yet included. Accuracy, update rate, and reliability values below are proposed targets, not demonstrated capabilities.

## Five Ws and How

| Question | Project definition |
| --- | --- |
| **What** | A prototype that displays the positions of two tagged objects and shows when a position is stale or invalid. |
| **Who** | An equipment coordinator is the proposed primary user. The sole developer operates the system, and course staff evaluate its evidence. This user need still requires interviews. |
| **Why** | Test whether automatic location updates help users find equipment, compared with their current workflow. No productivity improvement has been measured. |
| **When** | During a supervised course demo. The proposal suggests four one-week sprints; actual deadlines and the solo developer’s available time remain to be confirmed. |
| **Where** | A surveyed indoor laboratory area with known tag height. Factory and warehouse use are potential later applications requiring separate validation. |
| **How** | Tags broadcast UWB frames; fixed anchors record arrival timestamps; a computer corrects anchor clocks, calculates time differences, estimates positions, and displays them. |

## Planned demonstration

- Two tags and three fixed anchors, each using a DWM3000 UWB module with an ESP32 or another compatible microcontroller.
- A controlled 2D test area with surveyed anchor positions and known tag height.
- Wi-Fi forwarding of timestamp records to a central computer; Bluetooth remains an option to evaluate.
- A live map with tag IDs, position age, and invalid or stale states.
- Raw measurement logs and replayable evaluation results.

Qorvo lists the DWM3000 for TWR and TDoA tag or anchor use, with a customer-supplied MCU. The module does not integrate Bluetooth, so any Bluetooth connection must come from another component. These specifications support the proposed architecture but do not establish this prototype’s performance. [Qorvo DWM3000](https://www.qorvo.com/products/p/DWM3000)

## Localization architecture

```text
Two UWB tags
    │ UWB broadcasts with tag ID and sequence number
    ▼
Three fixed anchors
    │ Hardware arrival timestamps and receive diagnostics
    │ Wi-Fi backhaul
    ▼
Central computer
    Clock correction → frame matching → TDoA solver → map and logs
```

In uplink TDoA, anchors observe the same tag transmission. Differences in its arrival times correspond to differences in path lengths. Anchor clock synchronization or correction is essential; Wi-Fi or Bluetooth transports the records and does not, by itself, establish the required UWB timing basis. The proposed records therefore contain **arrival timestamps**, rather than independently measured tag-to-anchor distances. [ESP32 and DWM3000 study](https://arxiv.org/html/2403.10194v1)

**Recommended first experiment:** Verify that the selected boards and driver can produce consistent, corrected timestamps before building the full interface. A published ESP32/DWM3000 system uses two-way ranging (TWR); it provides hardware precedent but does not validate our uplink TDoA design. TWR may serve as a hardware baseline or an explicitly labeled fallback. [ESP32 and DWM3000 study](https://arxiv.org/html/2403.10194v1)

Three anchors provide two independent time differences for a known-height 2D model. This is a minimal configuration with no extra measurement redundancy; it does not guarantee a unique or well-conditioned position. A fourth anchor is a recommended improvement if resources allow. Anchor placement and obstructed radio paths must be evaluated because both affect TDoA localization quality. [Sensor placement study](https://arxiv.org/abs/2204.04508)

## Proposed evaluation targets

These targets require confirmation after user interviews and the initial timing experiment.

| Measure | Proposed criterion |
| --- | --- |
| Static horizontal accuracy | Median error ≤0.30 m and 95th-percentile error ≤0.75 m across nine surveyed test positions. |
| Valid-position ratio | At least 95% of scheduled tag transmissions produce accepted positions, reported separately for each tag. |
| Tag transmission rate | Start at 10 Hz per tag and report the achieved valid-position update rate. |
| Stale-state detection | Mark a position stale within one second of the last accepted estimate. |
| Reproducibility | A reviewer can replay retained records and reproduce the reported metrics using the documentation. |

Calibration and evaluation data should be separated. Report missed transmissions, rejected estimates, per-position errors, and obstructed-path results. Human tracking, robot control, free-height 3D localization, and industrial deployment remain outside the initial demonstration.

## Development plan

1. Interview prospective users, confirm hardware and course dates, and test a paper map.
2. Verify board communication and anchor clock correction; create a clearly labeled recorded-data demo.
3. Demonstrate one live tag end to end, then evaluate the surveyed area.
4. Add the second tag and test identity, update behavior, and failure states.
5. Publish reproducible measurements and document failed criteria and limitations.

The proposal contains five user stories with acceptance criteria, an INVEST review, thin demo slices, and ranked assumptions with tests and pivot criteria. No feasibility test results are available yet.

## Repository contents

```text
README.md
proposal/
    UWB_Project_Proposal.docx
    UWB_Project_Proposal.pdf
ref/
    DWM3000 Data Sheet.pdf
    HID-whitepaper-UWB-guide-for-decision-makers.pdf
```

- [Project proposal — Word](proposal/UWB_Project_Proposal.docx)
- [Project proposal — PDF](proposal/UWB_Project_Proposal.pdf)
- [DWM3000 data sheet](ref/DWM3000%20Data%20Sheet.pdf)
- [HID UWB whitepaper](ref/HID-whitepaper-UWB-guide-for-decision-makers.pdf)

The HID whitepaper is retained as a background reference; it was not used as evidence in the current proposal. Setup and execution instructions will be added when runnable software is available.

## Sources and course guide

- [EC601 Defining Your Project](https://docs.google.com/presentation/d/1mjnqe2xNa9wXm628SlQiQ_eo9ZSn7dLjxyB_iZE9pWU/edit) — project definition, mission, users, stories, INVEST, thin slicing, and assumption testing.
- [Qorvo DWM3000 product information](https://www.qorvo.com/products/p/DWM3000) — component capabilities and intended applications.
- [Espressif ESP32 Series Datasheet](https://documentation.espressif.com/esp32_datasheet_en.html) — MCU radio and peripheral capabilities; verify the exact selected part separately.
- [Krebs and Herter, 2024](https://arxiv.org/html/2403.10194v1) — an ESP32/DWM3000 TWR prototype and its measured limitations.
- [Zhao, Goudar, and Schoellig, 2022](https://arxiv.org/abs/2204.04508) — TDoA sensor placement and obstacle-induced measurement bias.

External technical claims should be supported by sources. Design recommendations, assumptions, and measured project results should remain distinguishable throughout development.

## AI collaboration

This one-person project is developed in collaboration with an AI agent, following the EC601 course guidelines. AI assistance is used for research, drafting, design review, and development support. The sole project owner remains responsible for verifying sources, testing implementations, and reporting results accurately.
