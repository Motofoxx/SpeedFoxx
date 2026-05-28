# SpeedFoxx

A universal motorcycle speedo correction and telemetry device built around an ESP32. This project is designed to sit inline between the bike's existing speedo/odometer wiring and the cluster, correcting speed output after sprocket or wheel changes and providing a live OLED/dashboard interface.

## Project goals

- Correct factory speedometer/odometer output after sprocket changes
- Support bike input mode and simulated demo mode
- Provide a 128x64 OLED interface for configuration and telemetry
- Offer a web portal for live data, sprocket selection, and tuning
- Track maintenance metrics like oil life, engine hours, and trip meters
- Use a small ESP32-based device with optocoupler isolation

## What is included

- `src/main.cpp` — main firmware implementing hardware input handling, simulation, UI, and web server
- `platformio.ini` — build config for ESP32 using Arduino framework
- `docs/HARDWARE.md` — hardware design notes, wiring, and signal flow

## Hardware summary

- MCU: ESP32-WROOM-32E development board
- Display: SSD1306 128x64 OLED (I2C)
- Input isolation: 4x 4N35 optocouplers
- Output stage: 2N2222 transistor for corrected speed pulse output
- Power: 12V rail for bike power, 3.3V rails for logic
- Buttons: two momentary pushbuttons for menu control

## Pin mapping used by firmware

| Function | ESP32 Pin | Signal type | Notes |
|---|---|---|---|
| Speed input | GPIO14 | Input, interrupt | Active low from optocoupler output |
| RPM input | GPIO27 | Input, interrupt | Active low from optocoupler output |
| Neutral status | GPIO32 | Input | Active low, display/gear logic |
| Kickstand status | GPIO33 | Input | Active low, display-only |
| Corrected speed output | GPIO26 | Output | Drives 2N2222 stage to the stock cluster |
| Top button | GPIO2 | Input | Menu unlock and edit control |
| Right button | GPIO0 | Input | Page/option navigation |
| OLED SDA | GPIO21 | I2C | SSD1306 data |
| OLED SCL | GPIO22 | I2C | SSD1306 clock |

## Software features

- Web portal with live dashboard and controls
- Simulator mode with sprocket profile demo cycle
- Bike input mode reading real speed/RPM signals
- Corrected speed output frequency generation
- Shift light threshold and flash timing
- Odometer, trip meters, engine hour tracking
- Maintenance logging and reset operations

## Versioning and release notes

This project follows semantic versioning: `MAJOR.MINOR.PATCH`.

- `MAJOR` for breaking changes and major updates
- `MINOR` for new features and firmware improvements
- `PATCH` for small bug fixes, UI tweaks, and documentation updates

Release notes are tracked in `CHANGELOG.md`. Update that file for each new firmware release.

A GitHub release workflow is configured in `.github/workflows/release.yml`. When you push a version tag like `v1.1.0`, GitHub will automatically create a release.

A PR label workflow is also configured in `.github/workflows/draft-release.yml`. When a pull request is merged with one of these labels, GitHub will create a draft release for the version in `src/main.cpp`:

- `release: patch`
- `release: minor`
- `release: major`

Labels are maintained automatically from `.github/labels.yml` by the label sync workflow in `.github/workflows/label-sync.yml`.

A release note template is provided at `.github/release_template.md`.

## Build and flash

```bash
pio run
pio run --target upload
```

## Notes before you build

- The ESP32 uses `INPUT_PULLUP` for all optocoupler inputs. The optocoupler outputs must be pulled up to 3.3V.
- The corrected speed output is generated using the ESP32 LEDC peripheral on `GPIO26`.
- The board is designed to sit inline, so the corrected pulse output replaces or reformats the original speed sensor signal going to the cluster.
- This repo currently assumes stock front/rear sprocket references and allows changing sprocket setup through the web UI.

## Where to start

1. Install PlatformIO and open the project
2. Inspect `src/main.cpp` for pin mapping and page logic
3. Wire the optocouplers and `2N2222` output stage as described in `docs/HARDWARE.md`
4. Build and flash the ESP32
5. Use the OLED and web portal to test signals before connecting to the cluster

---

For full hardware design details, see `docs/HARDWARE.md`.
