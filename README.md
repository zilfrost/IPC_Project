# IPC — VCU Dashboard Project

An embedded vehicle instrument cluster built on 3 STM32F103 ECU nodes connected over CAN bus to a Raspberry Pi running a Qt/QML dashboard.

---

## System Architecture

```
[Node 1 - STM32F103]      [Node 2 - STM32F103]      [Node 3 - STM32F103]
 Powertrain / Odometer      BCM / BMS Simulator        Environment & Controls
 ─────────────────────      ────────────────────        ────────────────────
 TIM1 encoder → RPM         ADC DMA (3 pots):           BMP180 I2C → Temp
 TIM2 100Hz → Speed         PA0 → Battery SoC           DS3231 I2C → Clock
 Trip distance calc         PA1 → Gear selector         GPIO → Turn Signals
       |                          |                            |
       └─────────────── CAN bus 500 kbps ────────────────────┘
                                    |
                         [Raspberry Pi 4 / Pi 5]
                          MCP2515 SPI-CAN → can0
                          Qt/QML Dashboard
                          CanReader + VehicleModel + GpsReader
                                    |
                          [GPS Module - /dev/serial0]
                           NMEA $GPRMC @ 9600 baud
```

---

## CAN Frame Protocol

| CAN ID | Source | Rate | Description |
|--------|--------|------|-------------|
| 0x100 | Node 1 | 100 Hz | Speed km/h — uint16 big-endian |
| 0x101 | Node 1 | 100 Hz | RPM — uint16 big-endian |
| 0x102 | Node 2 | 10 Hz | Battery SoC % — uint8 (0–100) |
| 0x103 | Node 3 | 1 Hz | Temperature °C — int8 |
| 0x104 | Node 2 | 10 Hz | Gear index — uint8 (0=P, 1=R, 2=N, 3=D, 4=D1, 5=D2, 6=D3) |
| 0x105 | Node 3 | 1 Hz | DateTime — 5 bytes YYMMDDHHMM |
| 0x106 | Node 1 | 10 Hz | Trip distance — uint16 big-endian, 1 LSB = 0.1 km |
| 0x107 | Node 3 | 20 Hz | Turn signals — bit0=left, bit1=right |
| 0x108 | Node 2 | 10 Hz | Drive mode — uint8 (0=ECO, 1=NORMAL, 2=SPORT) |
| 0x1F1 | Node 1 | 2 Hz | Heartbeat — byte[0]: encoder health |
| 0x1F2 | Node 2 | 2 Hz | Heartbeat — bytes[0–2]: SoC/Gear/Mode pot rail check |
| 0x1F3 | Node 3 | 2 Hz | Heartbeat — bytes[0–2]: BMP180/DS3231/GPIO health |

---

## Project Structure

```
IPC/
├── docs/                           ← Reference specs, DBC files, datasheets
├── src/
│   ├── ECUSource/
│   │   ├── Node1/                  ← STM32CubeIDE: Powertrain / Odometer
│   │   ├── Node2/                  ← STM32CubeIDE: BCM/BMS (ADC DMA)
│   │   └── Node3/                  ← STM32CubeIDE: Environment (I2C + GPIO)
│   ├── VCUSource/
│   │   └── meta-dashboard/         ← Yocto layer: Qt/QML dashboard + autologin
│   ├── meta-linkupcan/             ← Yocto layer: CAN0 systemd bring-up (500 kbps)
│   ├── meta-mcp2515-pi5/           ← Yocto layer: MCP2515 DT overlay for Pi 5
│   ├── meta-mywifi/                ← Yocto layer: WiFi (wpa-supplicant)
│   └── Local_and_BBlayers/         ← local.conf + bblayers.conf for Pi 4 and Pi 5
├── tests/
└── .ai-workspace/                  ← AI session context (Main.md, Rule.md, Task.md, Memory.md)
```

---

## Tech Stack

| Component | Technology |
|-----------|-----------|
| ECU firmware | C, STM32 HAL, STM32CubeIDE, arm-none-eabi-gcc |
| MCU | STM32F103C8T6 × 3 @ 72 MHz (HSE + PLL×9) |
| Dashboard app | C++17, QML, Qt (qmake) |
| Dashboard OS | Yocto Linux on Raspberry Pi 4 or Pi 5 |
| CAN interface | MCP2515 SPI-CAN → SocketCAN can0 @ 500 kbps |
| I2C sensors | BMP180 (temperature), DS3231 (RTC) |
| GPS | UART /dev/serial0, NMEA $GPRMC, 9600 baud |

---

## Getting Started

### ECU Firmware (STM32)

Requirements: STM32CubeIDE, ST-LINK programmer

```
1. Open src/ECUSource/Node1 (or Node2, Node3) in STM32CubeIDE
2. Build → Release
3. Flash to STM32F103C8T6 board via ST-LINK
```

**Critical Node 2 CubeMX settings:**
- DMA: Memory Increment (MINC) must be ENABLED for ADC DMA
- ADC clock: PCLK2/6 (12 MHz) — do not use default PCLK2/2 (36 MHz, too fast)
- Init order: `MX_DMA_Init()` before `MX_ADC1_Init()` — never reorder

### VCU Dashboard — Yocto Build

Requirements: Yocto Poky, meta-raspberrypi, meta-qt5 or meta-qt6

```bash
# Copy config files for your target
# Pi 4:
cp src/Local_and_BBlayers/local_pi4.conf    poky/build/conf/local.conf
cp src/Local_and_BBlayers/bblayers_pi4.conf poky/build/conf/bblayers.conf

# Pi 5:
cp src/Local_and_BBlayers/local_pi5.conf    poky/build/conf/local.conf
cp src/Local_and_BBlayers/bblayers_pi5.conf poky/build/conf/bblayers.conf

# Add meta layers to your Yocto sources:
# src/VCUSource/meta-dashboard
# src/meta-linkupcan
# src/meta-mywifi
# src/meta-mcp2515-pi5   ← Pi 5 only (has COMPATIBLE_MACHINE guard)

# Build
cd poky/build
bitbake core-image-base

# Flash the resulting .wic image to SD card
```

### VCU Dashboard — Direct qmake Build (for development)

Requirements: Qt 5.x or Qt 6.x with QML, Linux with SocketCAN

```bash
cd src/VCUSource/meta-dashboard/recipes-dashboard/vcu-dashboard/files/qt_project
qmake vcu_dashboard.pro
make -j$(nproc)
# Requires can0 to be up: sudo ip link set can0 up type can bitrate 500000
```

---

## Pi 5 Notes

The Raspberry Pi 5 uses an RP1 I/O controller with a different interrupt domain (PCIe MSI) than the Pi 4's BCM2835 GPIO IRQ. The standard MCP2515 DT overlay from meta-raspberrypi does not work on Pi 5. This project includes a custom overlay in `src/meta-mcp2515-pi5/` that:
- Fixes the `interrupt-parent` to point at the RP1 node
- Sets `IRQ_TYPE_LEVEL_LOW` for the PCIe MSI domain

---

## License

This project is private. All rights reserved.
