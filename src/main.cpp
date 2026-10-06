#include <Arduino.h>
#include <DHT.h>
#include <LiquidCrystal.h>
#include <math.h>
#include <string.h>
#include "config.h"

namespace {
LiquidCrystal lcd(8, 9, 4, 5, 6, 7);
DHT dht(Config::dhtPin, Config::dhtType);
enum class Button : uint8_t { None, Right, Up, Down, Left, Select };
enum class Mode : uint8_t { Manual, Auto };
enum class Screen : uint8_t { Air, Soil, Valve, Pump, Diagnostics, Count };
enum class Valve : uint8_t { Unknown, Opening, Open, Closing, Closed };
Mode mode = Mode::Manual;
Mode pendingMode = Mode::Manual;
Screen screen = Screen::Air;
Valve valve = Valve::Unknown;
bool choosingMode = false;
bool pumpRunning = false;
uint32_t pumpStarted = 0;
uint32_t valveStarted = 0;
uint32_t lastStepUs = 0;
uint8_t phase = 0;

struct Sensors {
  float temperature = NAN;
  float humidity = NAN;
  float distanceMm = NAN;
  int soilRaw = 0;
  int soilPercent = 0;
  int tankPercent = 0;
  bool airValid = false;
  bool soilValid = false;
  bool tankValid = false;
  uint32_t airAt = 0;
  uint32_t soilAt = 0;
  uint32_t tankAt = 0;
} sensors;

bool elapsed(uint32_t now, uint32_t since, uint32_t interval) {
  return uint32_t(now - since) >= interval;
}

bool valveMoving() {
  return valve == Valve::Opening || valve == Valve::Closing;
}

void setPump(bool running) {
  digitalWrite(Config::relayPin, running == Config::relayActiveLow ? LOW : HIGH);
  pumpRunning = running;
}

void releaseStepper() {
  for (uint8_t pin : Config::stepperPins) digitalWrite(pin, LOW);
}

void startValve(bool opening, uint32_t now) {
  const Valve destination = opening ? Valve::Open : Valve::Closed;
  const Valve movement = opening ? Valve::Opening : Valve::Closing;
  if (valve == destination || valve == movement) return;
  valve = movement;
  valveStarted = now;
  lastStepUs = micros();
}

void updateActuators(uint32_t now) {
  if (pumpRunning && elapsed(now, pumpStarted, Config::pumpRunMs)) setPump(false);
  if (!valveMoving()) return;
  if (elapsed(now, valveStarted, Config::valveTravelMs)) {
    valve = valve == Valve::Opening ? Valve::Open : Valve::Closed;
    releaseStepper();
    return;
  }
  const uint32_t nowUs = micros();
  if (!elapsed(nowUs, lastStepUs, Config::stepIntervalUs)) return;
  lastStepUs = nowUs;
  // Half-step sequence in physical IN1, IN2, IN3, IN4 order.
  static const uint8_t sequence[] = {0x1, 0x3, 0x2, 0x6, 0x4, 0xC, 0x8, 0x9};
  bool forward = valve == Valve::Opening;
  if (Config::reverseStepper) forward = !forward;
  phase = (phase + (forward ? 1 : 7)) & 7;
  for (uint8_t i = 0; i < 4; ++i)
    digitalWrite(Config::stepperPins[i], (sequence[phase] >> i) & 1);
}

Button readButton() {
  // Discard the first conversion after switching from the soil input.
  analogRead(Config::buttonsPin);
  const int value = analogRead(Config::buttonsPin);
  if (value < Config::rightThreshold) return Button::Right;
  if (value < Config::upThreshold) return Button::Up;
  if (value < Config::downThreshold) return Button::Down;
  if (value < Config::leftThreshold) return Button::Left;
  if (value < Config::selectThreshold) return Button::Select;
  return Button::None;
}

Button buttonPressed(uint32_t now) {
  static Button candidate = Button::None;
  static Button stable = Button::None;
  static uint32_t changedAt = 0;
  static bool armed = true;
  const Button reading = readButton();
  if (reading != candidate) { candidate = reading; changedAt = now; }
  if (candidate != stable && elapsed(now, changedAt, Config::debounceMs)) {
    stable = candidate;
    if (stable == Button::None) armed = true;
    else if (armed) {
      armed = false; // One action per press, including a held pump button.
      return stable;
    }
  }
  return Button::None;
}

void readSensors(uint32_t now) {
  static uint32_t lastTank = 0, lastSoil = 0, lastAir = 0;
  if (elapsed(now, lastTank, Config::tankReadMs)) {
    lastTank = now;
    digitalWrite(Config::triggerPin, LOW);
    delayMicroseconds(2);
    digitalWrite(Config::triggerPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(Config::triggerPin, LOW);
    const unsigned long duration = pulseIn(Config::echoPin, HIGH, Config::echoTimeoutUs);
    sensors.distanceMm = duration ? duration * 0.343f / 2.0f : NAN;
    sensors.tankValid = duration != 0 &&
        sensors.distanceMm >= Config::tankMinReadableMm &&
        sensors.distanceMm <= Config::tankMaxPlausibleMm;
    sensors.tankAt = millis();
    if (sensors.tankValid)
      sensors.tankPercent = constrain(int(roundf(100.0f *
          (Config::tankEmptyMm - sensors.distanceMm) /
          (Config::tankEmptyMm - Config::tankFullMm))), 0, 100);
  }
  if (elapsed(now, lastSoil, Config::soilReadMs)) {
    lastSoil = now;
    analogRead(Config::soilPin);
    sensors.soilRaw = analogRead(Config::soilPin);
    sensors.soilValid = sensors.soilRaw >= Config::soilMinValidAdc &&
                        sensors.soilRaw <= Config::soilMaxValidAdc;
    sensors.soilPercent = constrain(map(sensors.soilRaw, Config::soilDryAdc,
                                      Config::soilWetAdc, 0, 100), 0L, 100L);
    sensors.soilAt = millis();
  }
  // The DHT library blocks briefly. Defer it while timed actuators are running.
  if (!valveMoving() && !pumpRunning && elapsed(now, lastAir, Config::airReadMs)) {
    lastAir = now;
    sensors.humidity = dht.readHumidity();
    sensors.temperature = dht.readTemperature();
    sensors.airValid = isfinite(sensors.temperature) && isfinite(sensors.humidity) &&
        sensors.temperature >= Config::minTemperature &&
        sensors.temperature <= Config::maxTemperature &&
        sensors.humidity >= Config::minHumidity && sensors.humidity <= Config::maxHumidity;
    sensors.airAt = millis();
  }
}

bool tankHealthy(uint32_t now) {
  return sensors.tankValid && !elapsed(now, sensors.tankAt, Config::sensorStaleMs);
}

const __FlashStringHelper *diagnostic(uint32_t now) {
  if (!tankHealthy(now)) return F("Tank sensor ERR");
  if (sensors.distanceMm <= Config::tankFullMm) return F("Tank full/limit");
  if (sensors.distanceMm >= Config::tankEmptyMm) return F("Tank empty");
  if (!sensors.airValid || elapsed(now, sensors.airAt, Config::sensorStaleMs))
    return F("Air sensor ERR");
  if (!sensors.soilValid || elapsed(now, sensors.soilAt, Config::sensorStaleMs))
    return F("Soil sensor ERR");
  return nullptr;
}

void applyTankProtection(uint32_t now) {
  // Tank protection applies to the pump. Manual valve movement is commanded
  // directly by UP/DOWN; an ultrasonic fault must not cancel or reverse it.
  if (!tankHealthy(now) || sensors.distanceMm >= Config::tankEmptyMm) setPump(false);
}

void handleButton(Button button, uint32_t now) {
  if (button == Button::None) return;
  if (button == Button::Select) {
    if (!choosingMode) { pendingMode = mode; choosingMode = true; }
    else {
      mode = pendingMode;
      choosingMode = false;
      screen = Screen::Air;
      if (mode == Mode::Auto) {
        setPump(false);
        // AUTO has no watering algorithm yet. Close a known open inlet.
        if (valve == Valve::Opening || valve == Valve::Open) startValve(false, now);
      }
    }
    return;
  }
  if (choosingMode) {
    if (button == Button::Left || button == Button::Right)
      pendingMode = pendingMode == Mode::Manual ? Mode::Auto : Mode::Manual;
    return;
  }
  if (mode != Mode::Manual) return;
  if (button == Button::Left || button == Button::Right) {
    const uint8_t count = static_cast<uint8_t>(Screen::Count);
    screen = static_cast<Screen>((static_cast<uint8_t>(screen) +
        (button == Button::Right ? 1 : count - 1)) % count);
  } else if (screen == Screen::Valve) {
    if (button == Button::Down) startValve(false, now);
    if (button == Button::Up) startValve(true, now);
  } else if (screen == Screen::Pump) {
    if (button == Button::Down) setPump(false);
    if (button == Button::Up && !pumpRunning && tankHealthy(now) &&
        sensors.distanceMm < Config::tankEmptyMm) {
      pumpStarted = now;
      setPump(true);
    }
  }
}

const __FlashStringHelper *valveText() {
  switch (valve) {
    case Valve::Opening: return F("Opening");
    case Valve::Open: return F("Open");
    case Valve::Closing: return F("Closing");
    case Valve::Closed: return F("Closed");
    default: return F("Unknown");
  }
}

// Fixed buffers avoid heap fragmentation; write only changed rows, without clear().
class LcdLine : public Print {
 public:
  char text[17];
  uint8_t length = 0;
  LcdLine() { memset(text, ' ', 16); text[16] = '\0'; }
  size_t write(uint8_t value) override {
    if (length < 16) text[length++] = char(value);
    return 1;
  }
};

void render(uint32_t now) {
  static uint32_t lastDisplay = 0;
  static char previous[2][17] = {};
  if (!elapsed(now, lastDisplay, Config::displayRefreshMs)) return;
  lastDisplay = now;
  LcdLine top, bottom;
  if (choosingMode) {
    top.print(F("Mode: "));
    top.print(pendingMode == Mode::Manual ? F("MANUAL") : F("AUTO"));
    bottom.print(F("L/R choose SEL OK"));
  } else if (mode == Mode::Auto) {
    top.print(F("AUTO unavailable")); bottom.print(F("SEL change mode"));
  } else {
    switch (screen) {
      case Screen::Air:
        top.print(F("MANUAL 1/5 Air"));
        if (!sensors.airValid || elapsed(now, sensors.airAt, Config::sensorStaleMs))
          bottom.print(F("Air sensor ERR"));
        else if ((now / Config::airDisplayMs) % 2 == 0) {
          bottom.print(F("Temp: ")); bottom.print(sensors.temperature, 1); bottom.print(F(" C"));
        } else {
          bottom.print(F("Humidity: ")); bottom.print(sensors.humidity, 1); bottom.print('%');
        }
        break;
      case Screen::Soil:
        top.print(F("MANUAL 2/5 Soil"));
        if (!sensors.soilValid) bottom.print(F("Soil sensor ERR"));
        else { bottom.print(F("Moisture: ")); bottom.print(sensors.soilPercent); bottom.print('%'); }
        break;
      case Screen::Valve:
        top.print(F("Valve: ")); top.print(valveText());
        bottom.print(F("Tank:"));
        if (tankHealthy(now)) { bottom.print(sensors.tankPercent); bottom.print('%'); }
        else bottom.print(F("ERR"));
        bottom.print(F(" U+ D-"));
        break;
      case Screen::Pump:
        top.print(F("Pump: ")); top.print(pumpRunning ? F("Running") : F("Off"));
        bottom.print(tankHealthy(now) && sensors.distanceMm < Config::tankEmptyMm ?
                     F("UP:2s DOWN:stop") : F("No water/ERR"));
        break;
      case Screen::Diagnostics: {
        const auto fault = diagnostic(now);
        top.print(fault ? F("System: NOT SAFE") : F("System: SAFE"));
        bottom.print(fault ? fault : F("Sensors OK"));
        break;
      }
      default: break;
    }
  }
  const char *lines[] = {top.text, bottom.text};
  for (uint8_t row = 0; row < 2; ++row) {
    if (strcmp(previous[row], lines[row]) != 0) {
      lcd.setCursor(0, row); lcd.print(lines[row]); strcpy(previous[row], lines[row]);
    }
  }
}
}

void setup() {
  // Preload the inactive relay level before enabling the output.
  setPump(false);
  pinMode(Config::relayPin, OUTPUT);
  for (uint8_t pin : Config::stepperPins) { digitalWrite(pin, LOW); pinMode(pin, OUTPUT); }
  pinMode(Config::triggerPin, OUTPUT);
  digitalWrite(Config::triggerPin, LOW);
  pinMode(Config::echoPin, INPUT);
  pinMode(Config::buttonsPin, INPUT);
  pinMode(Config::soilPin, INPUT);
  // D10 stays untouched: LCD shield backlight circuits vary.
  lcd.begin(16, 2);
  dht.begin();
}

void loop() {
  updateActuators(millis());
  const Button button = buttonPressed(millis());
  readSensors(millis());
  // Refresh the clock after the bounded sensor reads.
  const uint32_t now = millis();
  applyTankProtection(now);
  handleButton(button, now);
  updateActuators(millis());
  render(millis());
}
