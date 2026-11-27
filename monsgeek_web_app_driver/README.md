# MonsGeek-bridge

A driver (bridge) for connecting MonsGeek keyboards to the web application [web.monsgeek.com](https://web.monsgeek.com) on Linux

## How it works

The web application [web.monsgeek.com](https://web.monsgeek.com) does not “communicate” directly with the keyboard; it sends requests to `iot_driver` via `http://127.0.0.1:3814`. This driver is not available on Linux, so `monsgeek-bridge` solves this problem by “responding” to the web app in the same way that `iot_driver` does, and forwarding commands from the web app to the keyboard via `/dev/hidraw*`.

```
web application web.monsgeek.com
   |  gRPC-web, http://127.0.0.1:3814
monsgeek-bridge
   |-- keyboard service channel: commands and responses
   `-- message channel: the keyboard itself reports changes
```

Tested on MonsGeek FUN60 Ultra (`3151:5030`, model number 2387). Only wired connections are supported; the 2.4G dongle and Bluetooth are not yet supported.

> [!IMPORTANT]
> Accepts commands only from `https://web.monsgeek.com`
> 
> The web app stores its data (macros, profiles) in `~/.local/share/monsgeek-bridge/`

## Building

Dependencies: `g++` C++20, `make`, `protobuf-compiler`, `libprotobuf-dev`, `libudev-dev`

```bash
sudo apt install g++ make protobuf-compiler libprotobuf-dev libudev-dev
make
make test
```

## Installation from a deb package

The package is built using [nfpm](https://nfpm.goreleaser.com) in Docker:

```bash
make deb        # dist/monsgeek-bridge_1.0.0-1_amd64.deb
make test-deb   # install and test the package in Docker with Debian:13
```

Installation:

```bash
sudo apt install ./dist/monsgeek-bridge_1.0.0-1_amd64.deb
```

The driver (bridge) is placed in `/usr/bin`, the udev rule in `/usr/lib/udev/rules.d`, and the systemd service in `/usr/lib/systemd/user`
Dependencies: libc6 (>= 2.38), libudev1

## Installation via `make install`

```bash
sudo make install
systemctl --user daemon-reload
systemctl --user enable --now monsgeek-bridge
```

- `/usr/local/bin/monsgeek-bridge` - driver (bridge)
- `/etc/udev/rules.d/70-monsgeek.rules` - rule for accessing the keyboard without `sudo`
- `/usr/local/lib/systemd/user/monsgeek-bridge.service` - systemd service for automatic startup

After installation, run `monsgeek-bridge --probe` to check if the system detects your keyboard
If it’s not in the list, reconnect the keyboard

## Testing

```bash
monsgeek-bridge --probe
```

Sample output: `/dev/hidraw6 3151:5030 id 2387 (events on /dev/hidraw5)`.
Logs:

```bash
journalctl --user -u monsgeek-bridge -f
```

## Removal

```bash
systemctl --user disable --now monsgeek-bridge
sudo make uninstall
```

## License

Copyright (C) 2026 GxdTxnz

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version. See [LICENSE](../LICENSE).

`third_party/httplib.h` is [cpp-httplib](https://github.com/yhirose/cpp-httplib) by Yuji Hirose, MIT License.

## Disclaimer

This is an unofficial project, not affiliated with or endorsed by MonsGeek. MonsGeek is a trademark of its owner. The software is provided without any warranty: use it at your own risk.
