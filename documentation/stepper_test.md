# Isolated stepper test

This diagnostic excludes LCD and sensor work and keeps the relay output off.
It does not move the motor at startup. The normal `uno` build is unchanged.
Use an unloaded motor for the rotation checks if possible.

From a PlatformIO terminal:

```powershell
pio run -e stepper_test -t upload --upload-port COM7
pio device monitor --port COM7 --baud 115200
```

Use the actual board port if it differs. Enter a command and press Enter:

- `1`, `2`, `3`, `4`: set just IN1, IN2, IN3, or IN4 HIGH for one second.
  Verify the corresponding driver indicator or measure the input against GND.
  Each single-coil test may twitch/hold the shaft; continuous rotation is not
  expected. Mismatched indicators identify a signal wiring problem.
- `o`: run the half-step sequence for five seconds at 4 ms per half step.
- `c`: run the sequence in reverse for five seconds.
- `s`: stop immediately and release the coils.

Expected physical signal wiring: IN1=D3, IN2=D11, IN3=A4, IN4=A5.
The winding sequence must also match the particular motor; its model is needed
before selecting a different sequence or drive voltage.

With power disconnected, check the motor connector and board power connections.
The motor supply must match the motor's voltage rating. Driver GND, supply
negative, and Arduino GND must be connected. Input LEDs alone do not prove that
the motor winding supply is present; measure the board supply during rotation.

Record which indicators respond, whether the motor is silent/buzzing/rotating,
the motor model/rating, and the supply voltage under load. If the isolated test
rotates but the application does not, investigate application timing or load.
If it still fails, check supply, input order, motor wiring, and driver outputs.

Close the serial monitor and restore the application afterward:

```powershell
pio run -e uno -t upload --upload-port COM7
```
