# Computer uplink TDoA dashboard

The computer receives timestamp events from one main anchor. The main anchor also broadcasts clock-sync beacons and reports its own reception of tag blinks. Other anchors timestamp those beacons and blinks and send their reports to the main anchor. Tags communicate over UWB. The PC maps remote timestamps onto the main anchor's clock, then estimates tag positions on a floor map at a fixed tag height.

## Run the simulation

Use Python 3.10 or later from the repository root.

```sh
python computer/tdoa_dashboard.py --simulate
```

Open [the TDoA dashboard](http://127.0.0.1:8767/). It labels the input as **Simulation** and generates DW3000-style timestamps for two moving tags with independent anchor clock drift and 40-bit wraparound. **Load floor plan** overlays a local image fitted to the configured rectangle. Orient the image with +X right and +Y up. Its edges should match the surveyed map bounds. The browser keeps the image for the current page session.

Use **Start logging** and **End logging** to save a local session under `computer/logs/`. The session contains `events.jsonl`, `positions.csv`, `timing.csv`, and `layout.json`. The timing CSV records each corrected arrival-time difference in nanoseconds, including two-anchor observations before position solving is possible. The event log includes recent sync events to give replay clock context. `--log-dir` sets a different folder. To replay a session at its recorded pace, run:

```sh
python computer/tdoa_dashboard.py --replay path/to/events.jsonl
```

## Connect a main anchor

Copy `tdoa_layout.json` and fill in the surveyed antenna positions `[x, y, z]` in meters, the known tag height, the main anchor's Node ID, and the floor-map bounds `[min_x, min_y, max_x, max_y]`. One main anchor and one other anchor can exercise timestamp intake and synchronization. The PC needs observations from at least three synchronized anchors for a 2D fix. A fourth provides redundant observations for checking the fit.

For USB, install `computer/requirements.txt` and choose the main anchor's serial device. Close other apps reading that port, including any TWR collector using it.

```sh
python computer/tdoa_dashboard.py --port COM4 --layout path/to/surveyed-layout.json
```

On macOS, pass the anchor's `/dev/cu.*` path. For Wi-Fi, join the main anchor's network and supply its base URL.

```sh
python computer/tdoa_dashboard.py --anchor-url http://192.168.4.1 --layout path/to/surveyed-layout.json
```

Both live transports require the gateway firmware described below. The current TWR firmware does not emit these events. In live mode, watch for incoming sync events before placing the tag. The dashboard displays the last valid position and marks it stale at one second. The fit residual measures consistency with the geometry. Surveyed tag positions are needed to measure true accuracy.

## Firmware interface, version 1

The gateway groups receptions of each UWB transmission by beacon sequence or by `(tag, tag_boot, seq)` for a blink. It emits one finalized group per transmission after a bounded collection window, with the observations it received. Keep completed sync and blink groups in transmission order within a gateway boot. A batch may omit an anchor that missed the frame. Duplicate events are ignored by the PC.

USB sends one newline-terminated UTF-8 record per event at 115200 baud. Prefix event JSON with `TDOA ` and keep the record under 4,096 bytes. Other serial lines can carry diagnostics. Examples:

```text
TDOA {"v":1,"type":"sync","gateway_boot":43,"seq":10,"tx_ticks":128000000000,"rx":[{"anchor":1002,"boot":44,"ticks":129000002800},{"anchor":1004,"boot":45,"ticks":130000002950}]}
TDOA {"v":1,"type":"blink","gateway_boot":43,"tag":2001,"tag_boot":91,"seq":72,"rx":[{"anchor":1003,"boot":43,"ticks":131200001100},{"anchor":1002,"boot":44,"ticks":132200002100},{"anchor":1004,"boot":45,"ticks":133200002400}]}
```

- `tx_ticks` is the main anchor's actual adjusted beacon transmit timestamp. Each `ticks` is the receiving DW3000's adjusted RX timestamp. Use unsigned integer ticks from the full 40-bit counter, before any ESP32 or host-time conversion. Configure or calibrate antenna delays consistently across the radios.
- Tag and anchor IDs are integers from 1 to 65534. Boot IDs and packet sequences are unsigned 32-bit integers. Change a boot ID whenever its radio timebase or sequence counter restarts. The main anchor's blink observation uses `boot` equal to `gateway_boot`.
- Start with beacons every 200 ms. Keep each beacon-to-beacon interval below one second even when reports are missed. The PC uses two bracketing beacons for each remote anchor in the same boot, estimates its drift with the interval ratio, and adds the surveyed beacon propagation delay. It waits up to 1.1 seconds for synchronized observations, rejects poor fits, and leaves the tag's last accepted position available with its age.
- The Wi-Fi gateway serves `GET /api/tdoa/events?after=CURSOR&boot=BOOT`. For the first poll the PC sends `after=0` and omits `boot`. Return `{"v":1,"gateway_boot":43,"cursor":125,"events":[...]}` with at most 64 ordered events per response and one cursor per gateway boot. Cursor 0 precedes the stream. Advance the cursor only through delivered records. If the requested boot differs, return the current boot and a page from its retained events. Keep the replay buffer short enough for near-live intake, ideally under two seconds. Return the same cursor with an empty event list when the PC is caught up. Return only events from the response's gateway boot.

## Hardware checks after firmware integration

Start with one tag and two anchors to compare corrected arrival-time differences with surveyed distances. The Sprint 1 timing gate requires at least 95% matched reception and a 95th-percentile absolute timing residual of at most 1 ns at each test position. Add a third anchor to test map positions. The simulation and replay exercise the software pipeline before those hardware measurements are available.

Run `python -m unittest discover -s computer -p test_tdoa_solver.py` to check timestamp rollover, restart handling, and input validation.
