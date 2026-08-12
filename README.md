# TraceView

Real-time telemetry dashboard for ESP32/ESP-NOW robots, built in C++ with Qt.

> **Status:** early development. Core architecture and telemetry protocol are being defined — see [CONTRIBUTING.md](CONTRIBUTING.md) for the branching/feature workflow and [docs/ECOSYSTEM.md](docs/ECOSYSTEM.md) for how this project fits with the rest of the ecosystem.

## Ecosystem

TraceView is the desktop client for [BTP](https://github.com/AlisonTristao/BTP), a binary protocol for real-time communication and data plotting over ESP32/ESP-NOW:

- [bally_robot](https://github.com/AlisonTristao/bally_robot) — robot hardware
- [Bally_OS](https://github.com/AlisonTristao/Bally_OS) — robot firmware (ESP32-S3)
- [Bally_dongle](https://github.com/AlisonTristao/Bally_dongle) — receiver dongle firmware (LilyGO T-Dongle-S3)
- [BTP](https://github.com/AlisonTristao/BTP) — the shared wire protocol and codec
- **TraceView** — telemetry visualization (this repository)

## Build

Requirements: CMake >= 3.21, Qt6 (Widgets, SerialPort), a C++17 compiler.

```sh
cmake -B build -S .
cmake --build build
```

If Qt6 isn't auto-discovered (e.g. a Qt Online Installer setup on Windows),
point CMake at the kit explicitly:

```sh
cmake -B build -S . -G Ninja -DCMAKE_PREFIX_PATH="C:/Qt/6.9.2/mingw_64"
cmake --build build
```

## License

[MIT](LICENSE)
