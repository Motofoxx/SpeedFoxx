# SpeedFoxx Hardware Design

This document describes the physical hardware architecture for the SpeedFoxx inline speed correction and telemetry device.

## Overall layout

The design uses two stacked perma-proto boards:

- **Top board**: ESP32 dev board, SSD1306 OLED, buttons, logic rails
- **Bottom board**: power conditioning, 4x 4N35 optocouplers, pulse conditioning, output transistor stage

The top and bottom boards are joined via pillar pins/jumpers so that the ESP32 is mounted on the top board while the sensor interface sits on the bottom board.

## Power rails

- **12V rail**: bike power input, used by regulators and potentially the optocoupler LEDs
- **3.3V rails**: logic supply for ESP32, SSD1306, and optocoupler outputs
- **Ground**: common ground reference between ESP32, display, optocouplers, and bike wiring

## Key components

- **ESP32-WROOM-32E**: main controller, runs the firmware and web server
- **SSD1306 OLED**: 128x64 display, I2C interface
- **4x 4N35 optocouplers**: isolate the bike signals from the ESP32
- **2N2222 transistor**: output stage for corrected speed pulses back to the factory cluster
- **Capacitors**: 100nF and 10nF decoupling near the regulator and ESP32 supply
- **Resistors**: pull-up resistors and input current limiting for optocouplers

## Signal flow

### Speed input

- Bike speed/hall sensor pulse enters through one optocoupler channel.
- The optocoupler output is pulled up to 3.3V and fed to `GPIO14` on the ESP32.
- The firmware uses an interrupt on falling edge to measure the pulse interval and derive input frequency.
- In bike mode, the device uses the front sprocket pulse rate plus gear/tire data to determine actual wheel speed.

### RPM input

- Bike RPM pulse enters through another optocoupler channel.
- The optocoupler output is pulled up to 3.3V and fed to `GPIO27` on the ESP32.
- The firmware uses an interrupt on falling edge to measure RPM interval.

### Neutral and kickstand inputs

- `GPIO32` reads neutral status from an optocoupler channel.
- `GPIO33` reads kickstand status from an optocoupler channel.
- Neutral status affects gear display logic and confirms when the bike is in neutral.
- Kickstand status is displayed but does not alter core speed correction logic.

### Corrected speed output

- The ESP32 generates the corrected speed pulse on `GPIO26`.
- `GPIO26` drives a `2N2222` transistor stage to source or sink the signal appropriate for the cluster.
- The output should be wired so that the corrected pulse replaces or modulates the original speed signal going into the dash.

## Signal polarity

- All inputs are configured as **active low**.
- Optocoupler outputs should therefore pull the ESP32 input pins low when a pulse is present.
- The firmware expects this arrangement and uses `INPUT_PULLUP` on all input pins.

## Recommended optocoupler wiring

For each 4N35 channel:

1. Connect the optocoupler LED input to the bike signal and ground through a current-limiting resistor.
2. Use the 3.3V rail to pull the transistor output up via a 10k resistor.
3. Feed the output to the corresponding ESP32 pin.
4. Add a small decoupling capacitor near the ESP32 and pull-up point if needed.

## Recommended output stage wiring

Use the 2N2222 as an open-collector or open-emitter driver depending on the cluster input requirement.

Example:

- ESP32 `GPIO26` -> base resistor -> `2N2222` base
- `2N2222` emitter -> ground
- `2N2222` collector -> cluster speed input with pull-up to 3.3V or to the cluster’s expected logic voltage

**Important:** verify cluster input requirements before connecting. A safe approach is to use the 2N2222 as the final isolation stage and avoid driving the cluster directly with the ESP32.

## Board construction notes

- Keep high-current/12V traces separate from low-voltage logic traces.
- Use a common ground star point for the optocoupler returns and ESP32 ground.
- Place the 0.1µF decoupling capacitor near the ESP32 3.3V pin.
- Ensure the 4N35 transistor outputs share the same ground reference as the ESP32.

## Mechanical / layout notes

- The current build uses two halves of a perma-proto board: top for the ESP32/display, bottom for power and isolation.
- The row alignment is reversed on one board, so confirm the pin mapping between the two halves before soldering.
- Pillar pins give mechanical support and help maintain stable connections.

## Safety and testing

- Test the optocoupler outputs with a meter before connecting the ESP32.
- Validate the speed output stage with a bench signal generator first.
- Do not connect the corrected output to the cluster until its logic levels are verified.

## Future hardware improvements

- Add configurable tire circumference or wheel circumference input for more precise speed correction.
- Add a separate regulator board for cleaner 3.3V power isolation.
- Add status LEDs for each optocoupler channel and output stage.
