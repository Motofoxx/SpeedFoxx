# Contributing to SpeedFoxx

Thank you for your interest in contributing to SpeedFoxx! This project is intended to be a stable, hardware-focused motorcycle telemetry and speed correction platform.

## How to contribute

1. Fork the repository on GitHub.
2. Clone your fork locally.
3. Create a descriptive feature branch:
   ```bash
git checkout -b feature/my-feature
```
4. Make your changes and test them carefully.
5. Commit with a descriptive message:
   ```bash
git commit -m "Add hardware documentation for 4N35 optocoupler wiring"
```
6. Push your branch to your fork and open a pull request.

## Code style

- Keep firmware changes in `src/main.cpp` or add new source files in `src/`.
- Use clear comments for hardware-specific logic and pin mappings.
- Keep documentation updates in `README.md` or `docs/HARDWARE.md`.

## Testing

- Use PlatformIO to build and verify the code.
- Validate any GPIO or signal changes on real hardware before pushing.
- Ensure the web portal data remains consistent if you change the `/data` endpoint.

## Reporting issues

- Open GitHub issues for bugs, hardware questions, or feature requests.
- Include as much detail as possible: firmware version, hardware setup, and observed behavior.

## Pull request guidance

- Describe the purpose of the change clearly.
- Reference any relevant issue numbers.
- Keep pull requests focused and small when possible.
- If you change hardware wiring or electrical design, include an update to `docs/HARDWARE.md`.
