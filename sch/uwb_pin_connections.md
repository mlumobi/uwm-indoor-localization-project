# XIAO ESP32C3 + DWM3000TR13 Wiring Reference

**Rev 1.1 - 2026-10-26**  
**Status: preliminary schematic reference; not a validated KiCad electrical netlist.**

## DWM3000TR13 24-pin net assignments

| Pin | Module signal | Connect to | Design note |
|---:|---|---|---|
| 1 | EXTON | NC or test point | Optional external power-control output |
| 2 | WAKEUP | GND if unused | UWB wake input; CS may also wake device |
| 3 | RSTn | XIAO D6 / GPIO21 | Active-low reset; drive low / release (open-drain) |
| 4 | GPIO7 / SYNC | Test point / NC | Optional external synchronization |
| 5 | VDD1 | 3V3 | AON supply; Qorvo application circuit shows 3.3 V |
| 6 | VDD3V3 | 3V3 | Local 100 nF ceramic decoupling |
| 7 | VDD3V3 | 3V3 | Local 100 nF ceramic decoupling |
| 8 | VSS | GND | Ground plane |
| 9 | GPIO6 / SPIPHA | NC | Internal pull-down defaults SPI CPHA=0 |
| 10 | GPIO5 / SPIPOL | NC | Internal pull-down defaults SPI CPOL=0 |
| 11 | GPIO4 | NC | Unused GPIO |
| 12 | GPIO3 / TXLED | NC / optional TX indicator | If used: separate resistor and driver configuration |
| 13 | GPIO2 / RXLED | NC / optional RX indicator | If used: separate resistor and driver configuration |
| 14 | GPIO1 | NC | Unused GPIO |
| 15 | GPIO0 | NC | Unused GPIO |
| 16 | VSS | GND | Ground plane |
| 17 | SPICSn | XIAO D3 / GPIO5 | SPI CS active low; optional 10k pull-up |
| 18 | SPIMOSI | XIAO D5 / GPIO7 | MCU -> module |
| 19 | SPIMISO | XIAO D2 / GPIO4 | Module -> MCU |
| 20 | SPICLK | XIAO D4 / GPIO6 | SPI clock |
| 21 | VSS | GND | Ground plane |
| 22 | IRQ / GPIO8 | XIAO D0 / GPIO2 | IRQ; GPIO2 is ESP32 boot-strapping pin - validate |
| 23 | VSS | GND | Ground plane |
| 24 | VSS | GND | Ground plane |

## XIAO ESP32C3 pin allocations

| XIAO | ESP32-C3 GPIO | Connection / role |
|---|---|---|
| D0 | GPIO2 | DWM IRQ; boot-strapping caution |
| D1 | GPIO3 | Wake button (Deep Sleep-capable) |
| D2 | GPIO4 | DWM MISO |
| D3 | GPIO5 | DWM CS |
| D4 | GPIO6 | DWM SCLK |
| D5 | GPIO7 | DWM MOSI |
| D6 | GPIO21 | DWM RSTn, open drain |
| D7 | GPIO20 | Opposite-state red/green LEDs |
| D8 | GPIO8 | Reserved; boot-strapping |
| D9 | GPIO9 | Reserved; BOOT button |
| D10 | GPIO10 | Anchor/Tag mode button |

## Discrete LED circuit (recommended, two resistors)

```text
3V3 -> R1 (1k) -> RED anode -> RED cathode -> D7/GPIO20
D7/GPIO20 -> GREEN anode -> GREEN cathode -> R2 (1k) -> GND
```

| D7 output | Red LED | Green LED | Meaning |
|---|---|---|---|
| LOW | ON | OFF | Tag |
| HIGH | OFF | ON | Anchor |
| High impedance / Deep Sleep | Not guaranteed OFF | Not guaranteed OFF | Ensure shutdown separately if needed |

**Important correction:** A single shared resistor is NOT safe in the previously described arrangement: when GPIO is HIGH, the green LED has no series current limiting. Use one resistor per LED. A single-resistor alternative requires additional circuitry and verification; it is not included here.

## Buttons

- **Wake/Sleep:** D1/GPIO3 with a normally-open pushbutton to GND and approximately 10k pull-up to 3V3. Configure active-low deep-sleep wake-up. Verify deep-sleep wake wiring/firmware for the ESP-IDF version.
- **Mode toggle:** D10/GPIO10 with normally-open pushbutton to GND and firmware input pull-up, or external 10k pull-up if desired.

## Power & passives

- DWM3000 VDD1 (pin 5), VDD3V3 (pins 6,7) -> 3V3 according to Qorvo representative application circuit. Confirm 3V3 supply current capability and layout in your actual design.
- DWM3000 VSS pins 8,16,21,23,24 -> continuous GND plane.
- Add 100nF ceramic decoupling close to each DWM3000 supply connection and a nearby 4.7-10uF bulk capacitor; verify Qorvo recommended PCB implementation.
- SPI CS (pin 17) may use 10k pull-up to 3V3 to keep CS deasserted while MCU resets.
- DWM IRQ (pin 22): Qorvo example suggests optional 100k pull-down against floating IRQ in sleep, but ESP32 GPIO2 is a strapping pin; test boot at reset before populating this part.
- DWM RSTn (pin 3) is active-low and must only be externally driven low or released (open drain); do not actively drive high.
- DWM GPIO5 (pin 10) and GPIO6 (pin 9): unconnected, internal pull-down defaults select SPI Mode 0; ESP32 configure SPI .mode=0.
- WAKEUP (pin 2): grounded when unused. EXTON (pin 1): no-connect/test point. GPIO7/SYNC (pin 4): test point recommended.
- Keep the module antenna outside the PCB copper/component keep-out according to Qorvo datasheet.

## Sources

- Qorvo DWM3000 Rev B datasheet: https://www.decawave.com/wp-content/uploads/2021/01/DWM3000-Datasheet-1.pdf
- Seeed XIAO ESP32C3 official pin map: https://wiki.seeedstudio.com/XIAO_ESP32C3_Getting_Started/
- Espressif ESP32-C3 Deep Sleep GPIO wake: https://docs.espressif.com/projects/esp-idf/en/v5.2/esp32c3/api-reference/system/sleep_modes.html

## Remaining pre-fabrication checks

1. Validate IRQ/GPIO2 boot-strap and optional pull-down on real hardware.
2. Confirm actual LED forward voltages, desired brightness, and deep-sleep current.
3. Confirm DWM module recommended passive values, supply ripple, and antenna keep-out.
4. Run KiCad ERC and inspect physical routing before manufacturing.
