# EE04 external SPI Flash: datasheet review and diagnostic status

Review date: 2026-10-17. Reference: the owner's 67-page Zbit ZB25VQ32 preliminary
datasheet. Page numbers below are PDF page numbers, which match printed pages.
The chip marking reported by the owner contains `25VQ32DSJG`.

## Pin contract

| Signal | U5 physical pin | ESP32 GPIO | ADALM2000 input, confirmed by owner |
| --- | --- | --- | --- |
| CS#, active low | 1 | 40, schematic SPI1_CS1 | DIO0 |
| DI / MOSI | 5 | **13, verified on this board** | DIO1 |
| DO / MISO | 2 | 12 | DIO2 |
| CLK | 6 | **11, verified on this board** | DIO3 |
| GND | 4 | GND | GND |
| VCC | 8 | Switched 3.3 V rail | No instrument power output |
| WP# | 3 | 3.3 V in schematic | No digital output |
| HOLD# / RESET# | 7 | 3.3 V in schematic | No digital output |

XIAO connector pin 7 / D6 / U0TXD is **GPIO43**, which enables the switched rail.
This is distinct from **U5 physical pin 7** and from GPIO7 (panel clock).
GPIO39 / SPI1_CS0 is the optional font-chip select and stays high.

The Arduino 3.3.12 implementation declares `begin(sck, miso, mosi, ss)`.
The supplied schematic and Arduino variant suggest `begin(13, 12, 11, 40)`.
However, **this physical board requires `begin(11, 12, 13, 40)`**. These are GPIO
numbers, not the D-number labels. In this ESP32-S3 core `FSPI` selects general-purpose
SPI2; it does not address the boot Flash. Arduino's board variant also declares
MOSI1=11, MISO1=12, SCK1=13, so using those names without checking the real board
also gives the wrong CLK/MOSI routing. Hardware SPI and the software probe now
use the measured GPIO mapping above. This observation applies to this unit;
the evidence does not establish which document/PCB revision introduced it.

The owner's probe mapping stays unchanged: CLK=DIO3, MISO=DIO2, MOSI=DIO1,
CS=DIO0. With both slave selects high, independent static GPIO tests gave:

| GPIO11 output | GPIO13 output | Read DIO1 | Read DIO3 |
| --- | --- | --- | --- |
| High | Low | Low | High |
| Low | High | High | Low |
| Low | Low | Low | Low |

CS stayed high and MISO was high during this identity test. Original routing
then failed all 16 RDID attempts. Swapping CLK/MOSI returned 5E4016 on all 16
attempts. This independent response confirms that firmware routing, not merely
the decoder label, needed to change. Earlier incorrect Scopy role labels have
been replaced using the owner's confirmed physical mapping.

## Protocol and timing review

- Pages 8-10: pinout, DI on rising clock, DO changes after falling clock;
  HOLD# low pauses selected Standard SPI and leaves DO high impedance. If pin7
  is configured as RESET#, low resets the device and prevents commands.
- Pages 19-20: Standard SPI supports modes 0 and 3. The bridge uses mode 0,
  MSB first. Hardware SPI and the software probe both sample on rising clock.
- Page 34, table 7.4: SPI RDID is **5E 40 16** (4 MiB); QPI is 5E 60 16.
- Page 53, section 7.5.4 and figure 7.31a: CS stays low for 8 command clocks
  carrying 9Fh, followed immediately by 24 result clocks. There is no address
  and no dummy-cycle gap. Bytes transmitted during result clocks are ignored
  by the Flash. WREN is not required to read ID.
- Pages 50-51 and 62: ABh exits deep power-down; allow at least 8 us afterward.
  Reset uses separate 66h and 99h transactions, with no intervening command;
  allow 10 us after reset and at least 40 ns before raising CS at its end.
  The software probe already waits 2 ms between these commands. A single-lane
  FFh transfer alone is not proof of successfully exiting a four-lane QPI state.
- Page 59: VCC operating minimum 2.3 V; wait at least 10 us from valid VCC
  before CS low. The bridge waits 100 ms; the power-cycle probe waits 1 s.
  R31 provides CS pull-up during rail startup in the supplied schematic.
  A power-cycle reset also requires VCC below 1 V for at least 10 us; a GPIO
  enable transition alone does not prove that voltage condition was met.

| Parameter, table 8.6 on page 62 | Datasheet requirement | Current diagnostic setting |
| --- | --- | --- |
| CLK high / low, non-03h instructions | >=4 ns each | Nominal 5 us each at 100 kHz |
| CLK high / low, 03h read | >=6 ns each | Nominal 5 us each at 100 kHz |
| CS active setup / hold | >=5 ns | Explicit >=1 us guards |
| CS deselect, array read to array read | >=100 ns | Explicit >=1 us guard |
| DI setup / hold | >=2 ns / >=5 ns | Hardware mode0; software probe has >=5 us phases |
| CLK falling to DO valid | <=7 ns | Next rising sample nominally 5 us later |
| ABh release to next instruction | >=8 us wait | Probe waits 2 ms |
| Reset end to CS high / next command | >=40 ns / >=10 us | Software probe delays exceed these values |

The original 10 MHz setting has nominal 50 ns high/low phases; it was not
obviously an overclock. The 45 MHz limit in table 8.6 explicitly applies to 03h;
the 120 MHz entries explicitly name fast-read operations and should not be
reported as a separately specified 9Fh maximum. The table allows DC operation,
so there is no need to keep the diagnostic clock fast.

Lowering frequency does not prove signal integrity. Page 61 requires input
high >=0.8*VCC and low <=0.2*VCC (2.64 V and 0.66 V at 3.3 V). Table 8.6
requires clock edge slew >=0.1 V/ns. These need analog measurements; a logic
analyzer's threshold and 10 ns sample interval at 100 MS/s cannot establish
2 ns setup time or analog edge slew. Use the instrument's oscilloscope if needed.

## Changes and verification

`tools/flash_bridge/Bridge.h` now has named GPIO constants, a 100 kHz default,
explicit CS timing guards, a reported SPI initialization result, and strict
5E4016 identity validation before enabling capacity-dependent operations.
Preloading GPIO output levels uses the ESP-IDF GPIO API: in Arduino 3.3.12,
`digitalWrite` before `pinMode` is not a reliable way to preload a new GPIO.

The updated diagnostic bridge compiled, was programmed and its write verified.
After restarting with corrected defaults, hardware SPI returned 5E4016 and
reported 4 MiB capacity. The independent software probe returned the same ID
in both modes 0 and 3 after rail cycling, wake-up and reset. The owner measured
U5 pin8 VCC and pin7 HOLD#/RESET# at approximately 3.3 V relative to pin4.

| Requested SPI clock | Correct RDID results | Four 4 KiB reads at separated offsets |
| --- | --- | --- |
| 100 kHz | 128 / 128 | Same bytes across all tested speeds |
| 500 kHz | 128 / 128 | Same |
| 1 MHz | 128 / 128 | Same |
| 2 MHz | 128 / 128 | Same |
| 5 MHz | 128 / 128 | Same |
| 10 MHz | 128 / 128 | Same |

The owner subsequently supplied a valid 100 MS/s capture with 32 clocks and
decoded response bytes 5E 40 16 using the corrected physical DIO mapping.
The captured command and result bytes agree with section 7.5.4.

These are short functional read tests, not long-duration or temperature-corner
qualification. No nanosecond analog timing compliance is claimed. SPI clock
settings are nominal until measured on a valid capture. The default remains
100 kHz for the current logic-analyzer investigation.

The initial diagnostic review used only reads, wake-up and reset commands.
Subsequently, under the owner's explicit filesystem-migration request, all
4 MiB original U5 contents were backed up privately before erase/program.
The old internal LittleFS image was cloned, fully readback-verified and mounted
on U5, then grown to its full 4 MiB capacity. Production 0.6 now uses that
external filesystem. See [the current memory map and migration](OTA.md).

ESP32 routing reference:
[Espressif SPI Master Driver](https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/api-reference/peripherals/spi_master.html).

