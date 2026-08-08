# TraceView

Telemetry dashboard for line-following robots, built in C++ with Qt.

> **Status:** early development. Core architecture and telemetry protocol are being defined — see [CONTRIBUTING.md](CONTRIBUTING.md) for the branching/feature workflow and [docs/ECOSYSTEM.md](docs/ECOSYSTEM.md) for how this project fits with the rest of the Bally ecosystem.

## Part of the Bally ecosystem

TraceView is the desktop telemetry client for the [Bally](https://github.com/AlisonTristao/bally_robot) line-follower platform:

- [bally_robot](https://github.com/AlisonTristao/bally_robot) — robot hardware
- [bally_OS](https://github.com/AlisonTristao/bally_OS) — robot firmware (ESP32-S3)
- [t_dongle_develop](https://github.com/AlisonTristao/t_dongle_develop) — receiver dongle firmware (LilyGO T-Dongle-S3)
- **TraceView** — telemetry visualization (this repository)

## Build

Requirements: CMake >= 3.21, Qt6 (Widgets, SerialPort), a C++17 compiler.

```sh
cmake -B build -S .
cmake --build build
```

## License

[MIT](LICENSE)
