# XIAO ESP32C3 + DWM3000TR13 Wiring Reference

**Rev 1.2 - 2026-10-27**  
**Source:** [KiCad schematic](uwm_esp32/uwm_esp32.kicad_sch), schematic blob `cd088ec23374f4d1c9c96b8a9672d03c35a510c2`.  
**Status:** Connection reference checked against the schematic nets. KiCad ERC, footprints, PCB layout, and hardware operation have not been validated.

## DWM3000TR13 24-pin net assignments

| Pin | Module signal | Actual connection | Design note |
|---:|---|---|---|
| 1 | EXTON | TP4 | Optional external power-control output; no external load fitted |
| 2 | WAKEUP | GND | Unused wake input; SPICSn may wake the device |
| 3 | RSTn | XIAO D7 / GPIO20 | Active-low reset; configure MCU open-drain, pull LOW / release |
| 4 | GPIO7 / SYNC | TP3 | Synchronization input available for testing |
| 5 | VDD1 | 3V3 | AON supply; 3.3 V is supported |
| 6 | VDD3V3 | 3V3 | Shares the supply decoupling network |
| 7 | VDD3V3 | 3V3 | Shares the supply decoupling network |
| 8 | VSS | GND | Common ground |
| 9 | GPIO6 / SPIPHA | TP6 | Internal pull-down selects CPHA=0 when externally undriven |
| 10 | GPIO5 / SPIPOL | TP7 | Internal pull-down selects CPOL=0 when externally undriven |
| 11 | GPIO4 | TP5 | Spare module GPIO |
| 12 | GPIO3 / TXLED | R6 (1k) -> D6 anode; D6 cathode -> GND | UWB TX activity indicator |
| 13 | GPIO2 / RXLED | R5 (1k) -> D5 anode; D5 cathode -> GND | UWB RX activity indicator |
| 14 | GPIO1 / SFDLED | R4 (1k) -> D4 anode; D4 cathode -> GND | Start-frame-delimiter indicator |
| 15 | GPIO0 / RXOKLED | R3 (1k) -> D3 anode; D3 cathode -> GND | Good-frame-reception indicator |
| 16 | VSS | GND | Common ground |
| 17 | SPICSn | XIAO D3 / GPIO5 | Active-low SPI chip select; module has internal pull-up |
| 18 | SPIMOSI | XIAO D5 / GPIO7 | MCU -> module |
| 19 | SPIMISO | XIAO D2 / GPIO4 | Module -> MCU |
| 20 | SPICLK | XIAO D4 / GPIO6 | SPI clock |
| 21 | VSS | GND | Common ground |
| 22 | IRQ / GPIO8 | XIAO D10 / GPIO10; R7 (100k) -> GND | Active-high IRQ; pull-down defines level while module sleeps |
| 23 | VSS | GND | Common ground |
| 24 | VSS | GND | Common ground |

The DWM3000 signal names GPIO0 through GPIO8 are module signals, not ESP32 GPIO numbers.

## XIAO ESP32C3 pin allocations

| XIAO pin | ESP32-C3 GPIO | Actual connection / role |
|---|---|---|
| D0 | GPIO2 | TP8 only; spare strapping pin |
| D1 | GPIO3 | SW1 Sleep Button to GND; R8 (10k) pull-up to 3V3 |
| D2 | GPIO4 | DWM3000 SPIMISO, pin 19 |
| D3 | GPIO5 | DWM3000 SPICSn, pin 17 |
| D4 | GPIO6 | DWM3000 SPICLK, pin 20 |
| D5 | GPIO7 | DWM3000 SPIMOSI, pin 18 |
| D6 | GPIO21 | ModeLED net; opposite-state Tag/Anchor LEDs |
| D7 | GPIO20 | DWM3000 RSTn, pin 3; open-drain control |
| D8 | GPIO8 | TP1 only; spare strapping pin |
| D9 | GPIO9 | TP2 only; strapping pin and onboard BOOT button |
| D10 | GPIO10 | DWM3000 IRQ, pin 22; R7 (100k) pull-down |
| 3V3 | — | DWM3000 supply, capacitors, Tag LED supply, and SW1 pull-up |
| GND | — | Common ground |
| 5V / VUSB | — | +5V net connected to the XIAO VUSB pin |

There is no separate Anchor/Tag mode-selection button in this schematic. Select the role in firmware or through a software interface.

### Reset and mode LED pin swap

- **RSTn uses D7/GPIO20**, replacing D6/GPIO21.
- **ModeLED uses D6/GPIO21**, replacing D7/GPIO20.
- GPIO20 defaults to UART0 RX (input), avoiding UART0 TX boot messages on the radio reset line.
- GPIO21 can transmit boot messages, so the mode LEDs may briefly flicker at startup.
- Configure GPIO20 as open-drain before reset control. Do not enable UART0 on these pins afterward.
- Configure GPIO21 as a push-pull GPIO output for the mode LEDs.

## Anchor/Tag mode LEDs

| Component | Value / role | Actual connection |
|---|---|---|
| D1 | Tag LED, intended red | Anode toward R1; cathode to ModeLED |
| R1 | 1k | 3V3 -> R1 -> D1 anode |
| D2 | Anchor LED, intended green | Anode to ModeLED; cathode toward R2 |
| R2 | 1k | D2 cathode -> R2 -> GND |

Each LED has its own series resistor. Select actual red and green LED parts during component selection; the schematic values currently identify their roles as Tag and Anchor.

| D6 / GPIO21 output | D1 Tag | D2 Anchor | Meaning |
|---|---|---|---|
| LOW | ON | OFF | Tag |
| HIGH | OFF | ON | Anchor |
| High impedance / Deep Sleep | Not guaranteed OFF | Not guaranteed OFF | Verify leakage and desired sleep behavior |

## UWB activity LEDs

| DWM3000 pin | Net | Resistor | LED reference | Function |
|---:|---|---|---|---|
| 15 | RXOKLED | R3 = 1k | D3 | Good frame received |
| 14 | SFDLED | R4 = 1k | D4 | Start frame delimiter detected |
| 13 | RXLED | R5 = 1k | D5 | Receive-mode activity |
| 12 | TXLED | R6 = 1k | D6 | Transmit activity |

All four branches run from the module output through a resistor to the LED anode, with the LED cathode connected to GND. Enable the corresponding LED functions in the DW3000 driver. Verify resistor values against the selected LEDs and the module GPIO drive specifications before final component selection.

**Naming:** LED component D6 is distinct from the XIAO header pin named D6.

## Sleep / wake button

- SW1 is a normally-open pushbutton between XIAO D1/GPIO3 and GND.
- R8 = 10k connects D1/GPIO3 to 3V3.
- Released = HIGH; pressed = LOW.
- GPIO3 supports ESP32-C3 Deep Sleep wake-up. Configure active-low wake-up using the appropriate ESP-IDF API.
- A sleep action such as long-press detection is implemented in firmware. Wait for button release before entering active-low wake-enabled sleep to avoid immediate wake-up.

## Power and passive components

| Component / net | Schematic implementation | Purpose |
|---|---|---|
| DWM3000 VDD1, VDD3V3 | Pins 5, 6, 7 -> 3V3 | Module power |
| DWM3000 VSS | Pins 8, 16, 21, 23, 24 -> GND | Common ground |
| C1 | 100nF between 3V3 and GND | High-frequency bypass |
| C2 | 10uF between 3V3 and GND | Local bulk buffering |
| R1-R6 | 1k each | Six separate LED current-limiting resistors |
| R7 | 100k between IRQ and GND | Prevent floating IRQ during radio sleep |
| R8 | 10k between D1/GPIO3 and 3V3 | SW1 pull-up |
| SPICSn | No external pull-up fitted | DWM3000 internal pull-up provides default deselection |
| RSTn | Direct connection to D7/GPIO20; no external transistor | MCU must pull LOW or release |
| Power flags | +3V3, +5V, GND | ERC declarations; not physical power sources |

C1 and C2 share the 3V3 net; this schematic contains one 100nF capacitor, not a separate capacitor for each supply pin. Determine placement and any additional bypassing during PCB design.

The drawing uses the XIAO power pins; it does not include a separate external power input connector or battery connector. USB power is supplied through the XIAO board. Verify supply capacity and any future external-power connection before use.

## Test points

| Reference | Connected signal |
|---|---|
| TP1 | XIAO D8 / GPIO8 |
| TP2 | XIAO D9 / GPIO9 |
| TP3 | DWM3000 GPIO7 / SYNC, pin 4 |
| TP4 | DWM3000 EXTON, pin 1 |
| TP5 | DWM3000 GPIO4, pin 11 |
| TP6 | DWM3000 GPIO6 / SPIPHA, pin 9 |
| TP7 | DWM3000 GPIO5 / SPIPOL, pin 10 |
| TP8 | XIAO D0 / GPIO2 |

GPIO2, GPIO8, and GPIO9 require attention to startup levels if external circuitry is later attached. SPIPHA and SPIPOL must remain at the intended levels during module startup; with only undriven test points, internal pull-downs select SPI Mode 0.

## Firmware connection summary

| Function | ESP32 GPIO | Configuration |
|---|---:|---|
| SPI MISO | 4 | SPI2 input |
| SPI MOSI | 7 | SPI2 output |
| SPI SCLK | 6 | SPI2 clock |
| SPI CS | 5 | Active-low chip select |
| DWM3000 reset | 20 | Open-drain; 0 = reset, 1 = release |
| DWM3000 IRQ | 10 | Input; active-high interrupt |
| Mode LED | 21 | Push-pull output; LOW = Tag, HIGH = Anchor |
| Sleep/wake button | 3 | Active-low input; external 10k pull-up |

Use SPI Mode 0. Update any older firmware definitions that used GPIO21 for reset, GPIO20 for ModeLED, GPIO2 for IRQ, or GPIO10 for a mode button.

## Sources

- [Qorvo DWM3000 Rev B datasheet](https://mm.digikey.com/Volume0/opasdata/d220001/medias/docus/6530/DWM3000%20Data%20Sheet.pdf)
- [Seeed XIAO ESP32C3 official pin map](https://wiki.seeedstudio.com/XIAO_ESP32C3_Getting_Started/)
- [Espressif ESP32-C3 datasheet](https://documentation.espressif.com/esp32-c3_datasheet_en.pdf)
- [ESP32-C3 GPIO driver and open-drain modes](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/api-reference/peripherals/gpio.html)
- [ESP32-C3 sleep and wake-up APIs](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/api-reference/system/sleep_modes.html)

## Remaining design checks

1. Run KiCad ERC and resolve or document its findings.
2. Select actual LEDs and confirm GPIO drive current, brightness, and sleep consumption.
3. Select capacitor voltage ratings and confirm supply stability.
4. Assign and verify footprints when component selection begins.
5. During layout, implement module antenna keep-out, grounding, and local decoupling.
6. Test cold power-up, ESP32-only reset, UWB reset, and sleep/wake behavior on hardware.
