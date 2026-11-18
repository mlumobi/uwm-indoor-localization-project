# ESP32-C3 firmware

Open this folder in VS Code with the Espressif ESP-IDF extension and select ESP-IDF v6.1 using **ESP-IDF: Select Current ESP-IDF Version**. The project defaults select the Seeed Studio XIAO ESP32C3, 4 MB flash, and the native USB Serial/JTAG console.

Use **ESP-IDF: Build your project** to compile. To load the firmware, connect the XIAO over USB, select its COM port, and use **ESP-IDF: Flash your project** followed by **ESP-IDF: Monitor Device**. At startup the console prints the DWM3000 device ID and confirms that the radio is ready for DS-TWR on channel 5.

From an activated ESP-IDF terminal, the equivalent commands are:

```powershell
idf.py build
idf.py -p COM3 flash monitor
```

Replace `COM3` with the XIAO's actual port if Windows assigns a different one. See the [PCB wiring reference](../sch/uwb_pin_connections.md) for the DWM3000 SPI, reset, and IRQ connections.

## Wi-Fi configuration dashboard

Each ESP32 creates a password-protected setup network named `UWM-xxxxxx`, using the last six hexadecimal digits of its MAC address. Open **ESP-IDF: Monitor Device** and look for the `Setup Wi-Fi` boot message. It shows the network name and dashboard URL. Every setup network uses the password `88888888`. Reset the board if the boot message has scrolled past.

1. Connect a phone or PC to the board's setup network using 88888888. If your device saved the old password, forget the network and rejoin.
2. Open [http://192.168.4.1](http://192.168.4.1) while connected.
3. Choose **Tag** or **Anchor**, enter a label, and assign an ID from 1 to 65534 that is unique within the deployment. Select **Save settings**.

Each board keeps its settings in flash across restarts. A save updates the role LED immediately. The dashboard shows the ESP32 MAC address and the DWM3000 device ID read over SPI. These hardware identifiers are read-only. The ranging task applies the saved Node ID and role between exchanges.

To range with a specific anchor, enter that board’s **Node ID** in **Target Anchor ID** on the tag’s dashboard and select **Save settings**. Clear the field and save to return to broadcast ranging.

## Two-way ranging

1. Flash the updated firmware on the tag. When setting up a fresh pair, flash both boards. Set one board to **Tag**, the other to **Anchor**, and give them different Node IDs in their Wi-Fi dashboards.
2. Power both boards and place them with a clear line of sight. Connect the computer to the **tag's** `UWM-xxxxxx` Wi-Fi network. With the November 14 bench roles, this is `UWM-FABAEC`. Use the password `88888888`.
3. Open [http://192.168.4.1](http://192.168.4.1) and check **Live TWR distance**. The page shows the distance in meters, tag and anchor IDs, and the UWB exchange sequence. The tag attempts an exchange roughly once per second. The display clears once the latest sample is at least three seconds old.
4. Move the boards through several measured antenna-to-antenna separations and compare the browser estimates with those measurements. The firmware uses nominal antenna delays, so the estimates may show bias.

The bench firmware uses double-sided two-way ranging on UWB channel 5. The tag sends a poll, the anchor responds, the tag sends a final frame, and the anchor reports its measured round-trip and reply intervals. The tag serves all four intervals in DW3110 ticks at `GET /api/twr`. JavaScript in the computer's browser calculates time of flight and distance from those intervals. See [Decawave application note APS013](https://forum.qorvo.com/uploads/short-url/x34DrF7EW5fQP9wY3aNESqPKz8z.pdf).

Use one anchor for broadcast ranging. With multiple anchors powered on, choose a **Target Anchor ID** on each tag to address its polls to the selected anchor. Multiple tags can share an anchor if each board has a unique Node ID. Tags vary the interval between polls and retry once after a short random delay if an exchange fails. At 115200 baud, each tag logs a local distance estimate and the anchor logs its replies for comparison by tag ID and exchange sequence.

After calculating the distance, the tag sends a result frame to the anchor. The anchor keeps the latest result for up to 32 tags and exposes the list at `GET /api/twr/tags`. The [computer TWR dashboard](../computer/README.md) reads those results through the reference anchor's Wi-Fi or USB connection.

## Local ranging log

Each board automatically saves its latest 2,048 ranging records to a dedicated flash partition. Recording works without a USB connection or Wi-Fi client. At roughly one record per second, the log holds about 34 minutes and overwrites the oldest records as it fills. Committed records survive power cycles.

Connect to the board's setup Wi-Fi and choose **Download CSV** under **Local ranging log**, or open `http://192.168.4.1/api/twr/log.csv`. Rows are ordered by record number. Use `boot_id` to group records from the same boot and `uptime_ms` to read time since that boot. Tag rows contain all four TWR intervals and the uncalibrated distance estimate. Anchor rows contain the intervals reported by that anchor and the lower 32 bits of its poll receive, response transmit, and final receive timestamps. Both roles record the node IDs and exchange sequence.

For an existing build with a generated `sdkconfig`, use `idf.py menuconfig` and select **Partition Table → Custom partition table CSV** with filename `partitions.csv`. Then run the full `idf.py -p PORT flash` so the board gets the new partition table. The settings, PHY, and app partitions retain their original offsets and sizes.

## SW1 button test

Open **ESP-IDF: Monitor Device** with the board connected on its USB serial port. Use **SW1 on the custom PCB**, which is wired to GPIO3.

1. Tap and release SW1. Look for `SW1 SHORT PRESS (<duration> ms)`.
2. Hold SW1 for about two seconds, then release it. Look for `SW1 LONG PRESS (<duration> ms)`.

GPIO3 should read HIGH at startup with SW1 released. A press logs `SW1 GPIO3 LOW (pressed)`, followed by one duration report on release. The test requires 30 ms of stable input to debounce each edge and classifies a press of 1,000 ms or more as long. Both events are diagnostic log messages.
