# AUTO mode

The firmware still starts in MANUAL. SELECT opens the mode selector, LEFT/RIGHT
chooses AUTO, and SELECT confirms it. AUTO first displays the stored soil target
for five seconds, then analyzes the tank. With a level above 90%, it flashes the
backlight for three seconds before commanding the valve closed. Later full-tank
events use the same three-second warning. Every automatic closure (full tank,
filling complete, or leaving AUTO while filling) flashes first for three seconds.
Direct UP/DOWN valve commands remain immediate.

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

Automatic closure warnings override all pages and temporarily ignore buttons.
Mode changes during filling schedule the warning before closure; the warning
continues even after switching to MANUAL.

## Settled tank readings and filling

Calibration: **61.5 mm empty, 42.4 mm full, approximately 500 mL**.
Level = `(61.5 - distance_mm) / 19.1 * 100`; 51.95 mm is 50% (about 250 mL).

Raw readings inside 37.4..66.5 mm qualify (5 mm tolerance beyond each endpoint). Five consecutive samples must
span at most 0.8 mm, with the stable-window condition lasting at least two seconds.
The mean of these five readings becomes the accepted level. Splashing, missing
echoes, out-of-range readings, or valve movement revoke qualification. The UI
shows settling/out-of-range rather than treating a chaotic echo as full or empty.
After a noisy window, requalification can take about four seconds at 500 ms sampling.

During a pump run, level samples are excluded. The authorized two-second dose
finishes on its timer; it is not interrupted by expected turbulence. After the
pump stops, wait three seconds, then collect a new stable window. No further
pump run is allowed until a steady in-range level is available. Water accounting
retains the pre-pump baseline and waits for this settled post-pump reading.

AUTO retains the ten-sample growth detector, but only accepted steady levels
enter it. Below 10%, AUTO opens the valve and shows filling. When readings become
chaotic, AUTO waits: it does not declare full, empty, or end-of-fill from them.
The no-growth timer restarts while the level is unqualified. After ten seconds
of qualified readings without growth, a three-second flashing warning precedes
closure. A qualified level above 90% uses the same warning before closing.

Continuous turbulence can therefore postpone automatic closure; manual closure
remains available by switching to MANUAL. An open valve must be supervised until
real-world filling and settling behavior has been checked. The warning adds
three seconds before the existing five-second closing movement.

Empty refill attempts rearm after 15% or re-entering AUTO. Full warnings rearm
below 85%, or when the valve is reopened. Tunable settings are `tankSteadySpreadMm`,
`tankSteadyMs`, `waterSettleMs`, and `autoCloseWarningMs` in config.h.

## Water statistics

Used water is an estimate from the filtered tank-level drop across pump operation,
measured after the post-pump pause and stable-window qualification. A 100-point
drop corresponds to 500 mL, assuming volume is proportional to level. Consecutive
manual pulses before settling are measured together. Noise and tank geometry limit
accuracy; this is not a flow-meter measurement. Turbulence postpones the measurement. A confirmed rise instead of a drop
marks water statistics unavailable because inflow prevents a consumption estimate.

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
   above 90%: three seconds warning then closure. Confirm manual UP cannot defeat it.
4. Below 10%, verify the pump stops, valve opens, popup tracks filling, and stable
   level ends the popup after ten seconds. Pour water at an intermediate level
   and verify the ten-sample trend triggers the same popup.
5. Inject a single distance spike, then a sustained level change. The spike should
   be rejected and the sustained change should be followed after the filter catches up.
6. Feed chaotic echoes during a dose: it must finish on its two-second timer.
   Confirm no new dose until three seconds have passed and a steady window returns.
   Verify the five-minute AUTO cooldown also remains enforced.
7. Change modes during a dose, refill, warning, and absorption wait. Confirm pump
   shutdown, automatic inlet closure where applicable, and preserved cooldown.
8. Compare estimated consumption against a measured volume; verify water metrics
   use the configured 40 mL/day baseline and the 70% maximum-savings goal.

Current steady-window and closure-delay assertions are included in auto_checks.

Endpoint tolerance: accept steady readings from 37.4 to 66.5 mm, inclusive.
Calibration remains 42.4 mm = 100% and 61.5 mm = 0%, representing 500 mL.
Values beyond the calibrated endpoints clamp to 100%/0%; the tolerance does not
change the 0.8 mm steadiness limit or AUTO thresholds.
