# Computer TWR dashboard

Run this dashboard to read live TWR distances from a reference anchor. Update the firmware on the anchor and tags. Power the tags separately. They send distance results to the anchor over UWB, and the anchor delivers the results to the computer over USB or Wi-Fi.

Give each board a unique Node ID. When several anchors are active, set each monitored tag's **Target Anchor ID** to the reference anchor using its onboard Wi-Fi settings page. Select the same anchor in the computer dashboard.

## Start the dashboard

From the repository root with the ESP-IDF Python environment active, run this for USB auto-detection:

```powershell
python computer/twr_dashboard.py
```

For another Python 3.10+ environment, install the serial dependency first:

```powershell
python -m pip install -r computer/requirements.txt
```

For Wi-Fi, join the anchor's `UWM-xxxxxx` network and run:

```sh
python computer/twr_dashboard.py --anchor-url http://192.168.4.1
```

Open [the dashboard](http://127.0.0.1:8765/) in a browser on the same computer. For USB, use `--ports COM4` on Windows or pass the anchor's `/dev/cu.*` device on macOS to select a specific port. Close any ESP-IDF monitor holding that port. Use `--http-port 8766` if port 8765 is in use. Press **Ctrl+C** to stop the collector.

The dashboard marks a distance stale after three seconds without a new measurement. Distances are the firmware's uncalibrated estimates.

## Record readings

Select **Start logging** to begin recording new distance readings from the connected anchors. Select **End logging** to finish the session. The dashboard displays the local CSV path and the number of recorded readings. Each session gets its own timestamped file, and the computer writes readings as they arrive.

The default folder is `computer/logs/`. Use `--log-dir` to choose a different folder. Each row contains the computer's UTC receive time, tag ID, anchor ID, exchange sequence, distance in meters, and connection source.
