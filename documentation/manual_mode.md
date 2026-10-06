# Manual mode

Firmware starts in MANUAL with the pump off and stepper outputs off. The valve
position is initially `Unknown`: there is no position sensor. DHT11 is configured
on A2. The pin assignments are those in `hardware_structure`.

## Controls

- SELECT opens the mode selector showing the current mode. LEFT/RIGHT changes
  the pending mode; SELECT confirms it. The existing mode remains active until
  confirmation, and actuator timers continue while the selector is visible.
- In MANUAL, LEFT/RIGHT cycles through five screens, wrapping at either end.
- Air: alternates temperature and relative humidity every 2.5 seconds.
- Soil: displays calibrated soil moisture as a percentage.
- Valve: UP opens clockwise for five seconds; DOWN closes counterclockwise for
  five seconds. The second row shows tank percentage and `U+ D-` controls.
  Opening/Closing changes to Open/Closed when the timer finishes. Repeating the
  same command does not restart its timer. Reversing direction starts a fresh
  five-second movement.
- Pump: UP runs the pump for two seconds; DOWN stops it early. Holding UP or
  pressing it again during a run does not extend or repeat the run. Release and
  press again after completion for another run.
- Diagnostics: displays SAFE when sensor readings are valid, fresh, within the
  configured limits, and the tank is neither empty nor at its full limit.
  Otherwise it displays NOT SAFE and the first detected issue.

AUTO is implemented as described in `auto_mode.md`. Changing modes stops the pump.
Use SELECT, LEFT/RIGHT, SELECT to return to MANUAL. The latest five valid tank
readings are filtered using median outlier rejection and a remaining-value mean
in both modes.

## Tank behavior and faults

Tank percentage is clamped to 0..100 using `(55 - distance_mm) / 50 * 100`:
55 mm is empty, 30 mm is 50%, 20 mm is 70%, and 5 mm is the requested full limit.
The documented 45 mm tank height is not used instead of these measured distances.

An empty tank or confirmed invalid/stale ultrasonic reading blocks/stops the pump.
Isolated rejected echoes hold the trusted level for less than 1.5 seconds; after
that it becomes a fault. Startup/recovery and jumps above 6 mm require three
consistent readings. See `auto_mode.md` for the shared filter details. Manual
valve UP/DOWN commands work independently of tank readings, as specified in
`software_and_device_working`. Full/invalid readings do not block opening or
automatically reverse a manual valve movement. Tank faults remain visible on the
valve and diagnostics screens; the operator controls filling and closing. A full
tank can still supply the pump. Air/soil faults are reported in diagnostics but
do not disable manual actuation.

The ultrasonic read has a 30 ms timeout and repeats every 500 ms. Distances below
20 mm, above 80 mm, and missing echoes are faults rather than valid percentages.
The requested 5 mm full point is below the HC-SR04's 20 mm minimum range.
With the current mounting, levels above 70% cannot be reliably measured and the
sensor validity check will fault before AUTO's >90% full warning. Raise the sensor
at least 15 mm and recalibrate both endpoints: a 15 mm rise gives approximately
70 mm empty / 20 mm full for the same requested water levels. Additional mounting
clearance provides margin above the minimum range. A missing echo cannot
distinguish overflow from a disconnected sensor. Closing itself takes five
seconds; leave enough physical filling margin for that travel time.

Soil is sampled every 250 ms. DHT reads run every 2.5 seconds when actuators are
idle to avoid interrupting their timing with the DHT library's blocking read.
Air/soil readings older than ten seconds are considered stale; tank readings use
the shorter 1.5-second trusted-reading limit. The LCD refreshes at most
five times per second and only rewrites changed rows. Buttons are debounced and
require a release between actions. Timers work across `millis()` rollover.

## Configuration and commissioning

Edit `include/config.h` for:

- `relayActiveLow`: currently true. Verify the relay is off at startup.
- `reverseStepper`: change if UP rotates counterclockwise on the actual motor.
- `stepIntervalUs`: currently 2000 microseconds per half step. Verify motor speed
  and torque; the specification gives travel time but not motor speed.
- `soilDryAdc` / `soilWetAdc`: provisional 800 / 350. Replace with readings from
  the actual sensor in dry and wet conditions. This is a relative percentage,
  not a measurement of volumetric water content.
- Button ADC thresholds, ultrasonic range, and sensor limits if calibration
  shows that the module needs different values.

DHT11 diagnostic limits default to 0..50 C and 20..90% RH. Soil readings at 0
or 1023 are treated as faults. An unplugged analog sensor can float to a plausible
value, so this check cannot guarantee connection detection. Diagnostics cannot
detect motor stalls, welded relay contacts, or actual valve position.

Initialize the valve mechanically before operation: use DOWN only when a full
five-second closing run is safe for the mechanism. The firmware releases the
stepper coils after movement and assumes the valve holds its position without
holding torque. Open/Closed are estimates based on time, not verified positions.
The firmware does not automatically home an unknown valve at startup.

## Validation

Build from the project directory with `pio run -e uno`.

Bench checks (require the actual hardware; not performed by the build):

1. Verify the pump is off at boot, valve shows Unknown, and each button works.
2. Navigate both directions through all five screens; confirm wraparound and
   alternating temperature/humidity. Check the soil percentage against calibration.
3. Verify tank readings at 55, 30, and 20 mm (0%, 50%, 70%). Reposition and
   recalibrate the sensor before testing the new full point. Disconnect ECHO and verify ERR.
4. At a valid intermediate level, verify UP opens and DOWN closes for five
   seconds, with the expected direction. Change screens during travel and confirm
   movement still finishes. Verify the coils switch off afterward.
5. Run the pump and measure its two-second pulse. Hold UP beyond two seconds:
   it must stop and remain stopped. Release/repress to run again; test DOWN stop.
6. While the pump runs, simulate empty tank or missing ECHO: it must stop.
   With missing ECHO, verify that valve UP still shows Opening and drives the
   motor for five seconds without reversing. DOWN must close it for five seconds.
   Repeat at the full threshold: both manual commands must still work while
   diagnostics reports the tank condition.
7. Check diagnostic faults for DHT disconnection, soil ADC rails, empty/full
   tank, and missing ECHO. Restore valid readings and check recovery.
8. Enter the selector during an actuator action and verify its timer continues.
   Choose AUTO without confirming: mode must not change. Confirm AUTO: pump
   stops and the five-second target screen appears. Return to MANUAL.
