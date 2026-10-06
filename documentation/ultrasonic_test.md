# Ultrasonic diagnostic

The application reports a tank fault for a missing echo or a distance outside
`tankMinReadableMm`..`tankMaxPlausibleMm` (currently 20..80 mm). A target 100 mm
away can therefore produce a valid distance but still fail the tank check.

The diagnostic uses the actual `triggerPin` and `echoPin` from `include/config.h`.
Verify those against the sensor's printed TRIG/ECHO labels. It leaves the pump
and stepper outputs off and displays raw distance and status on the LCD.

```powershell
pio run -e ultrasonic_test -t upload --upload-port COM7
pio device monitor --port COM7 --baud 115200
```

Use the board's actual port if different. Every 500 ms, serial output includes
the configured pins, echo input before/after measurement, pulse width in
microseconds, distance in millimeters, and status. The LCD works without opening
the serial monitor. This test uses a 30 ms timeout so bench targets farther than
the tank can be measured. The application now uses the same 30 ms timeout and
500 ms sampling interval. Timeout includes waiting for the echo to start, not
only its pulse width.

The main `uno` firmware also logs readings at 115200 baud when
`Config::tankDebugSerial` is true (currently enabled). It prints a MAIN startup
banner and adds `read_us`, the total measurement time, alongside `pulse_us`.
Logging is deferred while the pump or valve motor is running. Its valve and
diagnostics screens show specific no-echo, stuck-HIGH, range, or stale faults.
No tank range limits have been widened. Missing echoes can now block the loop
for up to 30 ms; this can delay actuator servicing by that much.

| Status | Meaning / next check |
|---|---|
| Tank range OK | A complete pulse produced a distance inside 20..80 mm. |
| Above max range | Echo received, but distance exceeds the configured tank range. Verify actual mounting distance and mm versus cm. |
| Below min range | Echo received below the configured minimum. Move the target farther away. |
| App timeout | Complete pulse was measured, but its duration exceeds the application's timeout. |
| No full echo | No complete pulse captured within 30 ms. Check power, common ground, TRIG/ECHO wiring, and target placement. This does not prove the sensor is broken. |
| ECHO HIGH idle | Input was already HIGH before triggering; no new trigger sent. Check wiring or a stuck/incomplete sensor response. |
| ECHO HIGH late | No complete pulse captured and ECHO is still HIGH after timeout. |

First aim at a flat target about 100..200 mm from the sensor face. A numeric
distance with Above max range is expected there. Then try measured distances
of 55 mm and 37.5 mm: they should be inside the application range, corresponding
to 0% and 50% tank level. Check the 20 mm full point separately because it is
at the nominal HC-SR04 minimum distance. Do not change tank calibration solely
to suppress an error before measuring the actual sensor-to-water distances.

For the standard 5V HC-SR04, connect VCC to 5V and GND to Arduino GND. Recheck
the actual pin labels on the sensor, not wire colors. If the input stays HIGH,
power-cycle the sensor and retest. Report several serial lines, or both LCD rows,
along with the target's measured distance.

Close the monitor and restore the application after testing:

```powershell
pio run -e uno -t upload --upload-port COM7
```

HC-SR04 specification: https://cdn.sparkfun.com/datasheets/Sensors/Proximity/HCSR04.pdf
