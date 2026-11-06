# ESP32KVM

A remote **"KVM without the VM"**: an ESP32-S3 that shows up as a **USB HID keyboard** on the
target computer and lets you drive it (type text, Ctrl+Alt+Del, F-keys, BIOS keys, ...) through an
**SSH shell** over WiFi. It also exposes a few guarded **GPIO** commands (e.g. to wire up a relay
or the target's power/reset header).

> Designed for use on a trusted local network. All access is protected by SSH (password
> authentication) - **do not expose it to the internet.**

PlatformIO + Arduino, SSH via [LibSSH-ESP32](https://github.com/DigitalzombieTLD/LibSSH-ESP32).

## Hardware

* ESP32-S3 board with **16 MB flash and 8 MB octal PSRAM** (e.g. ESP32-S3-DevKitC-1-N16R8).
  The PlatformIO config uses `esp32-s3-devkitc-1` with `qio_opi` memory type and the `default_16MB.csv`
  partition table (the ~6 MB app slot + SPIFFS, which stores the SSH host key).
* A board with **two USB-C ports** (labelled `UART` and `USB`):

| Port | Use |
|------|-----|
| **USB** (native, GPIO19 D-/GPIO20 D+) | plug into the **target computer** - this is the HID keyboard |
| **UART** (USB-UART bridge) | power + flashing + serial log (115200 baud, UART0 GPIO43/44) |

The firmware runs TinyUSB (`ARDUINO_USB_MODE=0`, `ARDUINO_USB_CDC_ON_BOOT=0`), so the native port is
*only* a keyboard and never a serial port. Power the board from the UART port (or the target's USB port).

### GPIO safety

These pins are **refused** by the `gpio` commands:

| Pins | Reason |
|------|--------|
| 19, 20 | USB D-/D+ (the HID link) |
| 26-32 | SPI flash / PSRAM |
| 33-37 | octal PSRAM (N16R8 modules) |
| 43, 44 | UART0 console |
| 22-25 | do not exist on ESP32-S3 |

"Caution" pins are allowed but print a warning: 0, 3, 45, 46 (strapping pins) and 48 (RGB LED on some boards).
Everything else (1-2, 4-18, 21, 38-42, 47) is free. GPIO levels are 3.3 V and a pin sources only a few mA - use a
transistor/optocoupler/relay board for anything real, never connect 5 V to a pin.

Wiring example (power-button press): `GPIO4` -> optocoupler/transistor -> motherboard PWR_SW header, common GND.

## Configuration

1. `cp include/config.example.h include/config.h` and edit it (WiFi, hostname, SSH user/password, optional
   fixed IP). `include/config.h` is git-ignored - never commit it.
2. These are only **defaults**. At runtime you can change them with shell commands and persist them
   in NVS with `save`; saved values take precedence over `config.h`. `factoryreset` erases NVS.

| Setting | config.h | runtime command |
|---------|----------|-----------------|
| WiFi SSID / password | `CFG_WIFI_SSID`, `CFG_WIFI_PASSWORD` | `set wifi <ssid> [password]` |
| Hostname | `CFG_HOSTNAME` | `set hostname <name>` |
| DHCP on/off | `CFG_DHCP` | `set dhcp on\|off` |
| Fixed IP, gateway, subnet, DNS | `CFG_IP`, `CFG_GATEWAY`, `CFG_SUBNET`, `CFG_DNS` | `set ip ...`, `set gateway/subnet/dns` |
| SSH user / password / port | `CFG_SSH_USER`, `CFG_SSH_PASSWORD`, `CFG_SSH_PORT` | `set user`, `set password`, `set port` |
| Key press delay | `CFG_KEY_DELAY_MS` | `set keydelay <ms>` |

Network changes take effect after `reboot`.

## Build & flash

```sh
pip install platformio
cp include/config.example.h include/config.h   # then edit
pio run                      # build
pio run -t upload            # flash via the UART port
pio device monitor           # serial log (115200)
```

The project uses the [pioarduino](https://github.com/pioarduino/platform-espressif32) platform (Arduino core 3.0.7,
one of the versions LibSSH-ESP32 is tested with), and LibSSH-ESP32 pinned to a commit via `lib_deps`
(see `platformio.ini`). The firmware also carries the LibSSH-ESP32 requirements: the `_impure_ptr` / reent setup, a
dedicated task with a 20 KB stack, `libssh_begin()` and SPIFFS for the host key.

First boot: an ed25519 host key is generated (takes a few seconds) and stored in SPIFFS at
`/.ssh/id_ed25519`, so the host fingerprint stays stable across reboots and firmware updates (unless you erase flash).

## Usage

```sh
ssh admin@esp32kvm.local     # or the IP shown on the serial console
```

(Use your configured user; mDNS name resolution depends on your network, the IP always works.) After login you get a banner
and a `kvm>` prompt (line editing, history with up/down, Ctrl-C clears the line, Ctrl-D exits). One-shot commands also work:
`ssh admin@esp32kvm "typeln hello"`. One session at a time.

## Command reference

### Keyboard

| Command | Description |
|---------|-------------|
| `type <text>` | type text exactly as given (spaces preserved), **no** Enter |
| `typeln <text>` | type text followed by Enter |
| `key <combo> [count]` | press a key or combination: `key ctrl+alt+del`, `key alt+f4`, `key win+r`, `key shift+tab`, `key a` |
| `enter` `esc` `backspace` `tab` `space` `delete` `insert` `home` `end` `pgup` `pgdn` | single keys |
| `up` `down` `left` `right` | arrow keys |
| `f1` ... `f12` | function keys |
| `ctrlaltdel` | Ctrl+Alt+Del |
| `ctrlc` | Ctrl+C |

Single-key commands accept a repeat count, e.g. `down 3`. Combo modifiers: `ctrl`, `shift`, `alt`, `altgr`, `gui`
(= `win`/`super`/`meta`/`cmd`). Key names: single printable ASCII characters plus the names above, `capslock`,
`printscreen`, `scrolllock`, `pause`. A literal plus is `ctrl++`. Text is typed using the host's US keyboard layout
(printable ASCII only).

### GPIO

| Command | Description |
|---------|-------------|
| `gpio list` | all existing pins with safety class (ok/caution/BLOCKED) and enabled pins |
| `gpio enable <pin>` | enable a pin (starts as input) |
| `gpio disable <pin>` | reset the pin to its default state and disable it |
| `gpio mode <pin> in\|in_pullup\|in_pulldown\|out` | set direction / pull resistor |
| `gpio write <pin> 0\|1\|high\|low\|toggle` | set output level (pin must be an output) |
| `gpio read <pin>` / `gpio read all` | read a pin / all enabled pins |

### Network / configuration

| Command | Description |
|---------|-------------|
| `status` / `ifconfig` | WiFi, IP, MAC, SSH, USB HID state, uptime, memory |
| `show config` | current (RAM) settings, no secrets |
| `set hostname <name>` | letters, digits, `-` (max 32) |
| `set dhcp on\|off` | |
| `set ip <ip> [gateway] [subnet] [dns]` | fixed address (used when DHCP is off) |
| `set gateway <a>` / `set subnet <a>` / `set dns <a>` | individual fixed values |
| `set wifi <ssid> [password]` | password prompted (hidden) if omitted; empty = open network |
| `set user <name>` / `set password [new]` | SSH login; password (8-64 chars) is prompted hidden if omitted |
| `set port <n>` | SSH port (after reboot) |
| `set keydelay <ms>` | key press/release delay 0-500 |
| `save` | persist the settings to NVS |
| `factoryreset` | erase saved settings (build-time defaults after reboot) |
| `reboot [force]` | restart; refuses if there are unsaved changes unless `force` |
| `help` / `?` | list commands |
| `exit` / `quit` / `logout` | close the session |

`set` commands change the running configuration only; run `save` to keep them and `reboot` to apply network
changes. Quote values with spaces: `set wifi "My Network" secret123`.

## Security notes

* Only SSH **password** authentication is offered (no keys, no none-auth). Login attempts per connection are limited
  to 3 with a 1 s delay per failure, and comparison is constant-time. Change the default password immediately
  (the banner warns while the build-time password is in use).
* The SSH host key is generated on the device and stored in SPIFFS **unencrypted**. WiFi and SSH passwords are stored in
  **plaintext in NVS** (flash). Enable ESP32 flash encryption if physical access is a concern.
* Whoever can log in can type arbitrary keystrokes on the target machine - treat the SSH credentials like admin access
  to that computer. Keep the device on a trusted/isolated network; do not port-forward it.
* Avoid passing secrets as command arguments (`set password` without an argument prompts hidden input).
* `include/config.h` holds your secrets; it is git-ignored. Don't commit it.
* There is no lockout beyond the per-connection limit; a new connection gets 3 new tries.

## Project layout

```
platformio.ini            board/flags/libs
include/config.example.h  build-time defaults template
src/main.cpp              setup, WiFi
src/ssh_server.cpp        libssh server, auth, host key
src/shell.cpp, term.cpp   command shell and line editor
src/kvm_keyboard.cpp      USB HID keyboard
src/kvm_gpio.cpp          guarded GPIO
src/settings.cpp          defaults + NVS persistence
```

## License

LibSSH-ESP32 / libssh are LGPL licensed; see their repositories. This project's own code is released under the MIT license.
