# Bluetooth Scanner ESP

An ESP32-family edge scanner that observes both Bluetooth Classic and Bluetooth Low Energy (BLE), sends combined scan batches over Wi-Fi, and stores and visualizes them with the companion SensorHub server.

## Hardware overview

The firmware is designed for an ESP32-family development board with an external HC-05 Bluetooth module connected over a UART. The current sketch was developed on an ESP32-C3 and configures:

| Connection | Default in firmware |
| --- | --- |
| ESP32 receive from HC-05 TXD | GPIO 4 |
| ESP32 transmit to HC-05 RXD | GPIO 5 |
| HC-05 AT-command UART speed | 38400 baud |

Check the pin capabilities and voltage requirements of your particular ESP32 and HC-05 breakout before wiring it. Some HC-05 carrier boards include power regulation but still require care at their UART pins.

### Starting the HC-05 in AT-command mode

The HC-05 must be in its full AT-command mode for Classic discovery. In the build used for this project, the module's **EN/KEY pin is mechanically held high while the HC-05 boots**. Holding EN/KEY high at power-up makes the module start in AT-command mode at 38400 baud instead of its normal transparent serial-data mode.

The firmware then uses commands including:

- `AT` to confirm communication.
- `AT+ROLE=1` to select master role.
- `AT+INQM=0,9,9` to configure inquiry behavior.
- `AT+INIT` to initialize the Classic Bluetooth stack.
- `AT+INQ` to discover nearby Bluetooth Classic devices.
- `AT+RNAME?` to attempt a remote-friendly-name lookup.

HC-05 boards and firmware variants are not completely uniform. Verify the EN/KEY behavior, LED indication, AT baud rate, and supported commands for the exact module being used.

## Why the scanner uses both the HC-05 and ESP32 Bluetooth

Bluetooth Classic and BLE share the Bluetooth name but use different discovery and communication models.

### Bluetooth Classic / legacy serial Bluetooth

The HC-05 is normally known as a Bluetooth Classic Serial Port Profile (SPP) module. In ordinary data mode, it acts much like a wireless UART after pairing. This project does **not** use it as a transparent serial link. It keeps the HC-05 in AT-command mode and uses the module's Classic inquiry functions to observe discoverable legacy devices.

Classic inquiry can provide information such as a device address, Class of Device, an inquiry RSSI value, and sometimes a remote name. This is useful for seeing older headphones, computers, phones, automotive equipment, and other devices that may not advertise through BLE.

### Bluetooth Low Energy

BLE devices advertise short packets rather than relying on the Classic SPP-style connection model. The ESP32's built-in BLE radio scans those advertisements directly and records fields such as address, RSSI, advertised name, manufacturer data, service UUIDs, service data, and the raw advertising payload.

BLE is common in sensors, beacons, wearables, smart-home devices, and modern low-power peripherals. An HC-05 cannot perform this BLE advertisement scan, so the ESP32 handles BLE while the HC-05 handles Classic inquiry.

### Combined scan cycle

Each scan cycle therefore has three stages:

1. The ESP32 asks the HC-05 to run a Bluetooth Classic inquiry and optional name lookups.
2. The ESP32 performs a native BLE advertisement scan.
3. The ESP32 combines both result sets into one JSON payload and posts it to SensorHub over Wi-Fi.

RSSI is useful as a rough relative signal-strength or proximity indicator, but it is not a direction measurement and should not be treated as an exact physical distance.

## Repository layout

- `firmware/` — ESP scanner firmware.
- `sensorhub-server/` — Flask, SQLite, and Waitress receiver/dashboard for Windows or other Python hosts.

See [sensorhub-server/README.md](sensorhub-server/README.md) for server setup, API routes, and startup instructions.
