# TPTCM WiFi Bridge - AI Agent Guide

This file provides essential information for AI coding agents working on the
tptcm_wifi_bridge project.

## Project Overview

**tptcm_wifi_bridge** is an ESP32-S3 firmware that turns a wired TPTCM60
ESC/POS thermal printer into a WiFi network printer. The ESP32 runs a RAW TCP
server on port 9100 (HP JetDirect style) and transparently forwards every
received byte to the printer's internal TTL UART through two SN74LVC1T45
level translators (3.3 V ↔ 5 V). Windows prints to it via a
**Standard TCP/IP Port** (Raw protocol) — no custom drivers, no ESC/POS
parsing on the bridge side.

### Key Features

- RAW TCP print server on port 9100, one client at a time
- WiFi provisioning: captive portal AP (`TPTCM60-Setup`) with an HTTP form,
  credentials persisted in NVS, wildcard DNS for portal auto-popup
- Optional Kconfig default credentials for development
- STA auto-reconnect; provisioning AP starts after repeated connect failures
  and stops automatically once the station gets an IP

### Target Hardware

- **MCU**: ESP32-S3-WROOM-1-N8R8 (dual-core, Wi-Fi, 8 MB flash, 8 MB octal
  PSRAM) — see `Altium/TPTCM_WiFi_Bridge/Bridge.SchDoc`
- **Link**: UART (default UART1, 115200 8N1) → SN74LVC1T45 (U2/U3) → TPTCM60
  internal 5 V TTL UART (pads TP4 `RX`, TP5 `TX`)
- **Pins**: TX = GPIO15 (U1-8, net `ESP_TX`), RX = GPIO17 (U1-10, net
  `ESP_RX`); configurable in menuconfig
- **Power**: the board is powered from the printer's 5 V pad (TP6) through an
  AMS1117-3.3 LDO (U4)
- **USB**: D−/D+ on GPIO19/GPIO20 (pads TP1 `DN`, TP3 `DP`) for flashing

> Board pin facts to respect in code: GPIO18 is pulled up to 3.3 V, GPIO16
> and GPIO8 are tied to GND — do not reuse them. The N8R8 module's octal
> PSRAM reserves GPIO35/36/37; GPIO15/17 are safe and are the UART pins.

## Technology Stack

- **Framework**: ESP-IDF v6.0.2
- **Language**: C (C99)
- **RTOS**: FreeRTOS (included in ESP-IDF)
- **Network**: lwIP (BSD sockets, `esp_http_server`), `esp_wifi`
- **Build System**: CMake with ESP-IDF's component-based build

## Project Structure

```
tptcm_wifi_bridge/
├── main/                       # Application entry point
│   ├── main.c                  # app_main(): NVS, UART, WiFi, TCP server
│   ├── include/main.h          # App_Context, App_State, App_SetState()
│   ├── CMakeLists.txt          # Component build rules
│   └── Kconfig.projbuild       # UART pins/baud, TCP port, WiFi defaults
├── components/                 # Custom ESP-IDF components
│   ├── printer_uart/           # UART driver for the printer link
│   ├── wifi_manager/           # STA + NVS credentials + AP provisioning
│   └── raw_tcp_server/         # RAW TCP :9100 server -> UART forwarder
├── scripts/
│   └── print_test.py           # TCP print test utility (PC side)
├── Altium/TPTCM_WiFi_Bridge/  # Hardware: Bridge.SchDoc, Bridge.PcbDoc
├── build_and_flash.ps1         # Windows build/flash script
├── sdkconfig.defaults          # Target chip and stack defaults
└── README.md
```

## Build & Flash Workflow

**The only supported way to build and flash this project on Windows is the
`build_and_flash.ps1` script in the project root.** It activates the ESP-IDF
environment itself — no manual environment setup is required or allowed.

> **Rule for AI agents:** if `build_and_flash.ps1` fails, **stop immediately
> and report the failure to the user**. Do NOT look for workarounds: no manual
> `PATH`/`IDF_PATH` setup, no raw `idf.py` invocation with hand-crafted
> environment variables, no alternative toolchains. Show the error output and
> wait for the user's instructions.

Requires **ESP-IDF v6.0.2** installed. The project targets **ESP32-S3**.

### Script paths (auto-configured, overridable via parameters)

| Parameter | Default value |
|-----------|---------------|
| `-IdfPath` | `C:\esp\v6.0.2\esp-idf` |
| `-IdfToolsPath` | `C:\Espressif` |
| `-PythonEnvPath` | `C:\Espressif\tools\python\v6.0.2\venv` |

### Usage

```powershell
# Build only
.\build_and_flash.ps1 -BuildOnly

# Clean build
.\build_and_flash.ps1 -BuildOnly -Clean

# Build and flash to COM3
.\build_and_flash.ps1 -Port COM3

# Build, flash, and open the serial monitor
.\build_and_flash.ps1 -Port COM3 -Monitor
```

> **Note:** The preferred way is to run the script from a native PowerShell
> prompt. It can also be invoked from Git Bash / agent shells:
> ```bash
> powershell.exe -NoProfile -ExecutionPolicy Bypass -File "C:\_Source\tptcm_wifi_bridge\build_and_flash.ps1" -BuildOnly
> ```

### Linux / macOS (reference only)

```bash
. $IDF_PATH/export.sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## Skills & External Guidelines

This project relies on external skills for standardized workflows. **Always
invoke the relevant skill before acting** — do not inline the rules here.

| Topic | Skill to use | Why |
|-------|-------------|-----|
| **Git operations** | `git-safe-workflow` | All questions about commit, push, branch, merge, rebase, stash, tag, history, diff, status, or any repository mutation. |
| **C code style** | `lukutar-c-style-guide` | Writing, reviewing, refactoring, or completing any C code (drivers, libraries, modules). Enforces naming, Doxygen headers, `_camelCase` private helpers, `LibraryName_PascalCase` public APIs, NULL checks. |
| **Python code style** | `lukutar-python-dev` | Any Python code (e.g. `scripts/`). |

> **Note:** Do not duplicate skill content in this file. If a skill
> instruction conflicts with legacy text in this document, the skill takes
> precedence.

## Architecture

### Data Flow

```
Windows (Standard TCP/IP Port, Raw :9100)
    │  TCP byte stream (ESC/POS)
    ▼
raw_tcp_server  ── PrinterUart_Write() ──►  printer_uart (UART1)
    │                                          │
    └─ one client at a time,                   ▼
       UART flush on disconnect       SN74LVC1T45 ×2 ──► TPTCM60 TTL UART
```

### Boot Sequence

1. `app_main()` initializes NVS (erase+retry on corrupt partition)
2. `PrinterUart_Init()` — UART from Kconfig, 4 KB TX ring buffer
3. `WifiManager_Init()` — netif, event handlers, load credentials from NVS
   (fallback: Kconfig defaults, else none)
4. `WifiManager_Start()`:
   - credentials known → STA connect; TCP server starts on "got IP"
   - no credentials → provisioning AP + HTTP portal + wildcard DNS
5. Repeated STA failures (default 5) → provisioning AP additionally;
   the AP stops by itself once the station gets an IP

### Component Boundaries

- `raw_tcp_server` never touches WiFi APIs — it only depends on
  `printer_uart` (and `main` for `App_SetState`)
- `wifi_manager` never touches the printer or TCP server — it notifies via
  the `WifiManager_ConnectedCb` callback registered in `main.c`
- `printer_uart` has no network dependencies

### Thread Safety

- The TCP server runs in its own task (`raw_tcp`, 4 KB stack); the UART
  driver serializes writes internally via its TX ring buffer
- `wifi_manager` state is touched from the esp_event task and its own timers
  only; the connected callback runs in the esp_event task context
- `App_SetState()` is called from multiple tasks; the transition log is
  informational (state is a single enum word — atomic enough on Xtensa)

## Kconfig Options (menuconfig → TPTCM WiFi Bridge)

| Option | Default | Description |
|--------|---------|-------------|
| `TPTCM_UART_PORT` | 1 | UART peripheral (UART0 = console, do not use) |
| `TPTCM_UART_TX_GPIO` | 15 | ESP32 → SN74LVC1T45 (U2) → printer RX |
| `TPTCM_UART_RX_GPIO` | 17 | printer TX → SN74LVC1T45 (U3) → ESP32 |
| `TPTCM_UART_BAUD` | 115200 | Must match printer DIP switches |
| `TPTCM_TCP_PORT` | 9100 | JetDirect RAW port |
| `TPTCM_WIFI_SSID` | "" | Default SSID (empty = always provision on first boot) |
| `TPTCM_WIFI_PASSWORD` | "" | Default password |
| `TPTCM_STA_MAX_RETRY` | 5 | Connect failures before the provisioning AP starts |
| `TPTCM_AP_SSID` | TPTCM60-Setup | Provisioning AP name |
| `TPTCM_AP_PASSWORD` | setup12345 | Provisioning AP password (≥8 chars) |

## Testing

### Manual Testing

- `idf.py monitor` / `build_and_flash.ps1 -Monitor` for boot logs
- Port probe from Windows: `Test-NetConnection <ip> -Port 9100`
- Print test: `python scripts/print_test.py <ip> "Hello"`

### Provisioning Flow Test

1. Erase NVS (`idf.py erase-flash`) and flash → AP `TPTCM60-Setup` must
   appear, portal answers on `http://192.168.4.1/`
2. Save credentials → device reboots and joins the network
3. Power-cycle — device connects without the portal

### Debug Features

- State transitions logged (`State: X -> Y`): INIT → PROV → READY → PRINTING
- Client connect/disconnect logged with the peer IP
- UART ready line printed with the actual pins and baud rate

## Important Notes

- The bridge is one-directional (TCP → UART). Printer status responses
  (DLE/DC4) are not forwarded to the client; the UART RX buffer is installed
  but unused. Add forwarding in `raw_tcp_server` if bidirectional status
  is ever needed.
- `windows Standard TCP/IP Port` requires a stable device IP — reserve it in
  the router DHCP or set `TPTCM_WIFI_SSID` to a network with a known
  reservation.
- Do not use GPIO35/36/37 (reserved by the octal PSRAM of the N8R8 module),
  GPIO18 (pulled to 3.3 V on the board), or GPIO16/GPIO8 (tied to GND).
- The DNS hijack answers A queries only, which is sufficient for captive
  portal detection on Windows/Android/iOS.

## Dependencies

- ESP-IDF v6.0.2 (locked)
- No managed components

## License

Proprietary - Mistress Lukutar
