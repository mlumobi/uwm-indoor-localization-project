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

## Two-way ranging

1. Flash this firmware on both boards. Use the Wi-Fi dashboards to set one board to **Tag**, the other to **Anchor**, and give them different Node IDs.
2. Place the boards with a clear line of sight. Open the serial monitor on the tag at 115200 baud. The tag reports one distance estimate per completed exchange. The anchor logs its replies.
3. Compare the readings at measured antenna-to-antenna distances before using them for localization.

The bench firmware uses double-sided two-way ranging on UWB channel 5. The tag sends a poll, the anchor replies, the tag sends a final frame, and the anchor reports its measured timestamp intervals. The tag calculates the range from both radios' hardware timestamps. The initial poll is broadcast, so this test uses one tag and one anchor. Antenna delays use nominal values. Range estimates are uncalibrated and can show bias at short separations.


## SW1 button test

Open **ESP-IDF: Monitor Device** with the board connected on its USB serial port. Use **SW1 on the custom PCB**, which is wired to GPIO3.

1. Tap and release SW1. Look for `SW1 SHORT PRESS (<duration> ms)`.
2. Hold SW1 for about two seconds, then release it. Look for `SW1 LONG PRESS (<duration> ms)`.

GPIO3 should read HIGH at startup with SW1 released. A press logs `SW1 GPIO3 LOW (pressed)`, followed by one duration report on release. The test requires 30 ms of stable input to debounce each edge and classifies a press of 1,000 ms or more as long. Both events are diagnostic log messages.
