# TPTCM WiFi Bridge

ESP32-S3 firmware that turns a wired TPTCM60 ESC/POS printer (internal TTL
UART) into a WiFi network printer. The ESP32 runs a RAW TCP server on port 9100
(JetDirect) and transparently forwards every byte to the printer UART through
SN74LVC1T45 level translators (3.3 V ↔ 5 V TTL). Windows connects via a
**Standard TCP/IP Port** — no custom drivers needed.

```
[Windows] → [Standard TCP/IP Port  IP:9100] → [WiFi] → [ESP32-S3] → [SN74LVC1T45 ×2] → [TPTCM60 TTL UART]
```

## Features

- **RAW TCP print server** on port 9100 (one client at a time)
- **WiFi provisioning** — on first boot (or after repeated connection
  failures) the device starts AP `TPTCM60-Setup` (`setup12345`) with a captive
  portal on `http://192.168.4.1/`; credentials are stored in NVS
- **Kconfig default credentials** — optionally preconfigure SSID/password via
  `idf.py menuconfig`
- **Transparent byte stream** — no ESC/POS parsing, works with any driver

## Hardware

Based on the Altium project in `Altium/TPTCM_WiFi_Bridge`
(`ESP32-S3-WROOM-1-N8R8` module, see `Bridge.SchDoc`):

| Signal | ESP32-S3 GPIO | Translator | Printer pad |
|--------|---------------|------------|-------------|
| TX     | GPIO15 (U1-8) | U2 A→B (DIR=1) | TP4 `RX` |
| RX     | GPIO17 (U1-10)| U3 B→A (DIR=0) | TP5 `TX` |
| +5V    | —             | —          | TP6 `5V` (board supply) |
| GND    | —             | —          | TP2 `GND` |

- The printer link is **5 V TTL UART**, translated to 3.3 V by two
  `SN74LVC1T45DBVR` single-bit translators (U2 TX path, U3 RX path).
- The board is **powered from the printer's 5 V** pad through an
  `AMS1117-3.3` LDO (U4).
- USB D−/D+ for flashing are on GPIO19/GPIO20 (pads TP1 `DN` / TP3 `DP`).
- GPIO18 is pulled up to 3.3 V on the board; GPIO16 and GPIO8 are grounded —
  do not reuse them.
- The module is the **N8R8** variant (octal PSRAM): GPIO35/36/37 are reserved
  by the PSRAM and not usable. GPIO15/17 are safe.

UART: 115200 8N1 (must match the printer DIP-switch settings). Pins, baud rate
and ports are configurable in `menuconfig → TPTCM WiFi Bridge`.

## Building & Flashing (Windows)

**The only supported way is the `build_and_flash.ps1` script** — it activates
the ESP-IDF environment itself. Requires ESP-IDF v6.0.2.

```powershell
# Build only
.\build_and_flash.ps1 -BuildOnly

# Build and flash to COM3
.\build_and_flash.ps1 -Port COM3

# Build, flash and monitor
.\build_and_flash.ps1 -Port COM3 -Monitor
```

### Linux / macOS (reference only)

```bash
. $IDF_PATH/export.sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## First-run setup

1. Flash the firmware and open the serial monitor.
2. Connect to the AP `TPTCM60-Setup` (password `setup12345`).
3. Open `http://192.168.4.1/`, enter your WiFi SSID/password, save — the
   device reboots and joins your network. The assigned IP is printed in the
   monitor log (`STA connected, IP: ...`). Reserve it in your router's DHCP.
4. In Windows: **Add printer → Local printer → Create new port → Standard
   TCP/IP Port**, enter the ESP32 IP, protocol **Raw**, port **9100**, SNMP
   off. Driver: **Generic / Text Only** or your ESC/POS driver.

## Testing without a printer

```bash
# Port check (PowerShell)
Test-NetConnection -ComputerName 192.168.1.100 -Port 9100

# Print test
python scripts/print_test.py 192.168.1.100 "Hello from WiFi"
```

## Project Structure

```
tptcm_wifi_bridge/
├── main/                   # Application entry point
├── components/
│   ├── printer_uart/       # UART driver for the printer link
│   ├── wifi_manager/       # STA + NVS credentials + AP provisioning portal
│   └── raw_tcp_server/     # RAW TCP :9100 server -> UART forwarder
├── scripts/
│   └── print_test.py       # TCP print test utility
├── Altium/TPTCM_WiFi_Bridge/  # Schematic + PCB (Bridge.SchDoc / Bridge.PcbDoc)
├── build_and_flash.ps1     # Windows build/flash script (ESP-IDF aware)
└── sdkconfig.defaults      # Target and stack defaults
```

## License

Proprietary - Mistress Lukutar
