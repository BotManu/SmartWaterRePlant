# AUTO mode

The firmware still starts in MANUAL. SELECT opens the mode selector, LEFT/RIGHT
chooses AUTO, and SELECT confirms it. AUTO first displays the stored soil target
for five seconds, then analyzes the tank. With a level above 90%, it flashes the
backlight for three seconds before commanding the valve closed. Later full-tank
events show `A TANK FULL` and flash for five seconds before closing.

The default reference is 60%. The user's confirmed interpretation is percentage
points: with a 60% reference, initial watering starts below 50%; follow-up watering
starts below 58%. Threshold equality does not trigger a dose.

## Watering cycle

- When soil moisture is more than ten points below the target, run the pump for
  two seconds.
- Starting when the pump stops, wait five minutes for absorption. Continue
  sampling soil throughout this period. If it is still more than two points
  below the target after that wait, give another two-second dose and wait again.
- Moisture is confirmed OK at or above the target. A value within two points
  below the target is monitored without another dose. After recovery, a new
  cycle again requires a drop of more than ten points.
- Invalid/stale soil or tank readings inhibit automatic watering. A tank below
  10%, an ongoing refill, valve movement, or an open valve also prevents a dose.
  DHT faults are displayed but do not replace the soil control input.
- Switching modes stops a running pump. An interrupted automatic dose still
  starts the five-minute wait. That wait survives mode changes within the same
  boot session, so switching modes cannot repeatedly trigger immediate doses.

## Screens and buttons

Every AUTO screen starts with `A`. LEFT/RIGHT cycles through:

1. Reference: UP/DOWN selects 20, 30, 40, 50, 60, 70, 75, 80, 85, 90, or 95%.
   An asterisk marks an unconfirmed edit. SELECT commits it to EEPROM; leaving
   with LEFT/RIGHT discards it. SELECT without an edit opens mode selection.
2. Information: every three seconds rotates through air temperature, air
   humidity, soil moisture/watering status, system OK/fault, used water, saved
   water, efficiency, and plant monitor time.
3. Mode: SELECT opens mode selection; LEFT/RIGHT chooses and SELECT confirms.
4. Valve: UP opens, DOWN closes, and the bottom row displays filtered tank level.
   AUTO rejects opening above 90% or with invalid tank readings.

Filling and full-tank popups temporarily override normal pages. SELECT still
allows a mode change; a full warning retains display priority until it finishes.
Leaving AUTO during a refill/full warning commands closure. Full-tank closure
has priority over the AUTO manual valve controls.

## Filtering and filling

Both modes validate readings before the five-sample median/mean filter:

- Startup and recovery require three consecutive readings consistent within
  3 mm of their running mean.
- A jump more than 6 mm from the filtered distance also requires three consistent
  readings. One or two spikes do not change the reported level. A confirmed jump
  resets the old averaging window so a real rapid fill/drain can be followed.
- Smaller changes enter the five-sample window; samples more than 3 mm from its
  median are removed, then the remaining samples are averaged.
- Missing echoes, out-of-range echoes, and unconfirmed jumps hold the last trusted
  value for less than 1.5 seconds. They never refresh its age. At 1.5 seconds the
  tank becomes invalid and the existing pump/valve fault behavior applies.
- Held values do not count as new filling evidence or finalize used-water
  accounting. The ten-sample growth history restarts after a rejected sample.

Calibration is 55 mm empty and 5 mm full, allowing 15 mm more water depth.
The new full point is below the sensor's valid 20 mm minimum: at the current
mounting, AUTO will encounter a sensor fault above about 70% before it can reach
the >90% full warning. Reposition the sensor and recalibrate both endpoints as
described in `manual_mode.md` before relying on automatic filling.
At 500 ms sampling, confirming
a change takes about 1..1.5 seconds from its onset; ordinary averaging can add
lag. Threshold actions use the filtered value. The serial log includes raw and
filtered distances, `accepted=0/1`, reading age, and `Tank: held`/`Tank: checking`.
Tune `tankOutlierMm`, `tankJumpMm`, `tankConfirmReadings`, and `tankHoldMs` in
`config.h` if the actual fill rate requires different tradeoffs.

AUTO uses ten filtered level readings to detect filling: compare the averages
of the older five and newer five. A rise of at least two percentage points opens
the filling popup. Below 10%, AUTO also opens the inlet valve and shows the empty
tank/filling popup. The pump is off during filling.

Filling ends after ten seconds without detected growth (excluding opening travel).
The firmware closes the valve and returns to the previous page. These thresholds
are configurable because the requirement does not specify noise tolerance or
the duration of a stable level. An empty refill attempt is not retried repeatedly
while the tank remains below 10%; it rearms after reaching 15%, or on re-entering
AUTO. Full warnings rearm below 85%; reopening the valve still permits another
full warning above 90%. A tank sensor fault closes a known open/opening inlet.

The full threshold is strictly greater than 90%, and the empty threshold strictly
less than 10%. Full-tank flashing is followed by five seconds of valve travel;
verify there is enough physical margin at the actual fill rate.

## Water statistics

Used water is an estimate from the filtered tank-level drop across pump operation,
measured again three seconds after stopping to allow filter settling. A 100-point
drop corresponds to 1000 mL, assuming volume is proportional to level. Consecutive
manual pulses before settling are measured together. Noise and tank geometry limit
accuracy; this is not a flow-meter measurement. A level fault or detected inflow
during the measurement marks water statistics unavailable until reset rather than
reporting unsupported savings.

The user-provided traditional watering baseline is **40 mL/day**, configured as
`traditionalWaterMlPerDay` in `include/config.h`. Setting it to zero disables
the comparison and displays `Set baseline/day` for savings and efficiency.

- Traditional usage = baseline mL/day multiplied by elapsed days since boot.
- Saved water = traditional usage minus estimated water used (can be negative).
- Efficiency = saved fraction divided by 0.70, displayed as 0..100% of the goal.
- Plant monitor time and water statistics include both modes and reset at boot.
  Only the soil target persists, using EEPROM bytes 0 and 1.

The backlight uses D10: LOW output turns it off; a weak input pull-up turns it on
without driving the pin strongly HIGH. Verify flashing with the actual keypad
shield; its transistor/backlight circuit must support this control.

## Validation

`pio run -e uno -e auto_checks` builds the firmware and executes compile-time
behavioral assertions on the production watering/filter logic: threshold equality,
repeat dosing, cooldown, target recovery, sensor inhibition, timer rollover,
setpoint bounds, outlier rejection, and rising/flat/falling ten-sample windows.
Additional assertions check bad startup echoes, two-echo spikes, brief dropouts,
expiry and recovery, sustained filling/draining, inconsistent jumps, and rollover
of the trusted-reading timestamp.
These are logic checks, not physical sensor/actuator tests.

Bench checks:

1. Enter AUTO and verify the five-second target screen, then navigation among all
   four pages. Confirm target edits persist after reset and unconfirmed edits do not.
2. With target 60%, test initial readings of 50% (no dose) and 49% (two-second dose).
   Confirm no repeat for five minutes after stopping. At expiry test 58% (no dose),
   57% (another dose), and 60% (recovered).
3. Test entry above 90%: three seconds flashing then closure. Test a later rise
   above 90%: five seconds warning then closure. Confirm manual UP cannot defeat it.
4. Below 10%, verify the pump stops, valve opens, popup tracks filling, and stable
   level ends the popup after ten seconds. Pour water at an intermediate level
   and verify the ten-sample trend triggers the same popup.
5. Inject a single distance spike, then a sustained level change. The spike should
   be rejected and the sustained change should be followed after the filter catches up.
6. Disconnect ECHO or the soil sensor during a dose. Confirm pump shutdown and
   faults; reconnect and verify recovery without bypassing the five-minute cooldown.
7. Change modes during a dose, refill, warning, and absorption wait. Confirm pump
   shutdown, automatic inlet closure where applicable, and preserved cooldown.
8. Compare estimated consumption against a measured volume; verify water metrics
   use the configured 40 mL/day baseline and the 70% maximum-savings goal.
