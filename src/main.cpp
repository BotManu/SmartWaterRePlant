#include <Arduino.h>
#include <DHT.h>
#include <LiquidCrystal.h>
#include <EEPROM.h>
#include <math.h>
#include <string.h>
#include "config.h"
#include "auto_logic.h"
#ifdef AUTO_LOGIC_CHECKS
#include "../test/auto_logic_checks.h"
#endif

namespace {
LiquidCrystal lcd(8, 9, 4, 5, 6, 7);
DHT dht(Config::dhtPin, Config::dhtType);
enum class Button : uint8_t { None, Right, Up, Down, Left, Select };
enum class Mode : uint8_t { Manual, Auto };
enum class Screen : uint8_t { Air, Soil, SoilDebug, Valve, Pump, Drain, Diagnostics, Count };
enum class Valve : uint8_t { Unknown, Opening, Open, Closing, Closed };
enum class AutoScreen : uint8_t { Reference, Info, Mode, Valve, Count };
enum class AutoStage : uint8_t { Intro, Analyze, Running };
AutoScreen autoScreen = AutoScreen::Info;
AutoStage autoStage = AutoStage::Intro;
uint8_t soilTarget = Config::defaultSoilTarget;
uint8_t editedTarget = Config::defaultSoilTarget;
bool editingTarget = false;
AutoLogic::Watering watering;
AutoLogic::SettledLevel levelFilter;
AutoLogic::CloseDelay closeDelay;
AutoLogic::FillTrend fillTrend;
uint32_t autoEnteredAt = 0;
enum class CloseReason : uint8_t { Full, Settled, ModeChange };
CloseReason closeReason = CloseReason::Full;
bool waitingAfterPump = false;
bool settledRise = false;
uint32_t lastGrowthAt = 0, monitorSeconds = 0;
bool fullWarning = false, fullHandled = false, filling = false, emptyHandled = false;
bool accounting = false, accountSettling = false, waterEstimateValid = true;
float doseStartPercent = 0, usedWaterMl = 0;
uint32_t accountStoppedAt = 0;
Mode mode = Mode::Manual;
Mode pendingMode = Mode::Manual;
Screen screen = Screen::Air;
Valve valve = Valve::Unknown;
bool choosingMode = false;
bool pumpRunning = false;
uint32_t pumpStarted = 0;
uint32_t pumpDurationMs = Config::pumpRunMs;
uint32_t valveStarted = 0;
uint32_t lastStepUs = 0;
uint8_t phase = 0;

struct Sensors {
  float temperature = NAN;
  float humidity = NAN;
  float distanceMm = NAN;
  float rawDistanceMm = NAN;
  float tankLevel = 0;
  int soilRaw = 0;
  int soilPercent = 0;
  int tankPercent = 0;
  bool airValid = false;
  bool soilValid = false;
  bool tankValid = false;
  unsigned long echoUs = 0;
  bool echoBefore = false;
  bool echoAfter = false;
  uint32_t airAt = 0;
  uint32_t soilAt = 0;
  uint32_t tankAt = 0;
  uint32_t tankSampleAt = 0;
} sensors;

bool elapsed(uint32_t now, uint32_t since, uint32_t interval) {
  return uint32_t(now - since) >= interval;
}

const __FlashStringHelper *tankStatus(uint32_t now) {
  if (sensors.tankSampleAt == 0) return F("Tank: waiting");
  if (pumpRunning || waitingAfterPump) return F("Tank: settling");
  if (sensors.tankValid && levelFilter.healthy(now, Config::tankHoldMs))
    return F("Tank steady");
  if (sensors.echoBefore) return F("Echo HIGH idle");
  if (!sensors.echoUs)
    return sensors.echoAfter ? F("Echo HIGH late") : F("Tank: no echo");
  if (!Config::tankReadingInRange(sensors.rawDistanceMm))
    return F("Tank: out range");
  return F("Tank: settling");
}

bool valveMoving() {
  return valve == Valve::Opening || valve == Valve::Closing;
}

void setPump(bool running) {
  if (running && !pumpRunning) {
    // A second manual pulse before settling belongs to the same measurement.
    if (!accounting) {
      accounting = sensors.tankValid;
      doseStartPercent = sensors.tankLevel;
    }
    accountSettling = false;
    levelFilter.invalidate();
    sensors.tankValid = false;
    fillTrend.clear();
  } else if (!running && pumpRunning) {
    watering.finished(millis());
    accountSettling = accounting;
    accountStoppedAt = millis();
    waitingAfterPump = true;
    levelFilter.invalidate();
  }
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
  levelFilter.invalidate();
  sensors.tankValid = false;
  valveStarted = now;
  lastStepUs = micros();
}

void updateActuators(uint32_t now) {
  if (pumpRunning && elapsed(now, pumpStarted, pumpDurationMs)) setPump(false);
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
    sensors.echoBefore = digitalRead(Config::echoPin) == HIGH;
    unsigned long duration = 0;
    const uint32_t readStartedUs = micros();
    if (!sensors.echoBefore) {
      digitalWrite(Config::triggerPin, LOW);
      delayMicroseconds(2);
      digitalWrite(Config::triggerPin, HIGH);
      delayMicroseconds(10);
      digitalWrite(Config::triggerPin, LOW);
      duration = pulseIn(Config::echoPin, HIGH, Config::echoTimeoutUs);
    }
    const uint32_t readTimeUs = micros() - readStartedUs;
    sensors.echoAfter = digitalRead(Config::echoPin) == HIGH;
    sensors.echoUs = duration;
    sensors.rawDistanceMm = duration ? duration * 0.343f / 2.0f : NAN;
    if (waitingAfterPump && elapsed(millis(), accountStoppedAt, Config::waterSettleMs))
      waitingAfterPump = false;
    const bool rawValid = duration != 0 && Config::tankReadingInRange(sensors.rawDistanceMm);
    sensors.tankSampleAt = millis();
    sensors.tankValid = levelFilter.observe(sensors.rawDistanceMm,
        rawValid && !pumpRunning && !waitingAfterPump && !valveMoving(),
        sensors.tankSampleAt, Config::tankSteadySpreadMm,
        Config::tankSteadyMs, Config::tankHoldMs);
    if (levelFilter.fresh) {
      const bool hadPreviousLevel = sensors.tankAt != 0;
      const float previousLevel = sensors.tankLevel;
      sensors.tankAt = levelFilter.acceptedAt;
      sensors.distanceMm = levelFilter.output;
      sensors.tankLevel = constrain(100.0f *
          (Config::tankEmptyMm - sensors.distanceMm) /
          (Config::tankEmptyMm - Config::tankFullMm), 0.0f, 100.0f);
      sensors.tankPercent = int(roundf(sensors.tankLevel));
      if (mode == Mode::Auto && hadPreviousLevel &&
          sensors.tankLevel >= previousLevel + Config::fillRisePercent) settledRise = true;
      fillTrend.add(sensors.tankLevel);
    } else {
      // Held values are not new evidence that the tank is filling.
      fillTrend.clear();
      // Turbulence is expected: keep the pre-dose baseline until water settles.
    }
    if (accounting && levelFilter.fresh && sensors.tankLevel > doseStartPercent + Config::fillRisePercent) {
      waterEstimateValid = false; // Inflow masks consumption; do not invent savings.
      accounting = accountSettling = false;
    }
    if (accountSettling && levelFilter.fresh && elapsed(millis(), accountStoppedAt, Config::waterSettleMs)) {
      usedWaterMl += max(0.0f, doseStartPercent - sensors.tankLevel) * Config::tankCapacityMl / 100.0f;
      accounting = accountSettling = false;
    }
    // Avoid serial transmission adding pauses during motor movement/pump runs.
    if (Config::tankDebugSerial && !valveMoving() && !pumpRunning) {
      Serial.print(F("tank ms=")); Serial.print(sensors.tankSampleAt);
      Serial.print(F(" trig=D")); Serial.print(Config::triggerPin);
      Serial.print(F(" echo=D")); Serial.print(Config::echoPin);
      Serial.print(F(" before=")); Serial.print(sensors.echoBefore);
      Serial.print(F(" after=")); Serial.print(sensors.echoAfter);
      Serial.print(F(" read_us=")); Serial.print(readTimeUs);
      Serial.print(F(" pulse_us=")); Serial.print(duration);
      Serial.print(F(" distance_mm="));
      if (duration) Serial.print(sensors.rawDistanceMm, 2);
      else Serial.print(F("NA"));
      Serial.print(F(" filtered_mm=")); Serial.print(sensors.distanceMm, 2);
      Serial.print(F(" accepted=")); Serial.print(levelFilter.fresh);
      Serial.print(F(" age_ms=")); Serial.print(uint32_t(millis() - sensors.tankAt));
      Serial.print(F(" status=")); Serial.println(tankStatus(millis()));
    }
  }
  if (elapsed(now, lastSoil, Config::soilReadMs)) {
    lastSoil = now;
    analogRead(Config::soilPin);
    sensors.soilRaw = analogRead(Config::soilPin);
    sensors.soilValid = sensors.soilRaw >= Config::soilMinValidAdc &&
                        sensors.soilRaw <= Config::soilMaxValidAdc;
    sensors.soilPercent = int(roundf(constrain(
        Config::soilPercentOffset + Config::soilPercentPerAdc * sensors.soilRaw,
        0.0f, 100.0f)));
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
  return sensors.tankValid && levelFilter.healthy(now, Config::tankHoldMs);
}

const __FlashStringHelper *diagnostic(uint32_t now) {
  if (!tankHealthy(now)) return tankStatus(now);
  if (mode == Mode::Auto && sensors.tankLevel > 90) return F("Tank full/limit");
  if (mode == Mode::Auto && sensors.tankLevel < 10) return F("Tank empty");
  if (sensors.distanceMm <= Config::tankFullMm) return F("Tank full/limit");
  if (sensors.distanceMm >= Config::tankEmptyMm) return F("Tank empty");
  if (!sensors.airValid || elapsed(now, sensors.airAt, Config::sensorStaleMs))
    return F("Air sensor ERR");
  if (!sensors.soilValid || elapsed(now, sensors.soilAt, Config::sensorStaleMs))
    return F("Soil sensor ERR");
  return nullptr;
}

void backlight(bool on) {
  // Weak pull-up for ON avoids driving D10 strongly HIGH on keypad-shield clones.
  if (on) pinMode(Config::backlightPin, INPUT_PULLUP);
  else { digitalWrite(Config::backlightPin, LOW); pinMode(Config::backlightPin, OUTPUT); }
}

void requestAutomaticClose(uint32_t now, CloseReason reason) {
  if (valve == Valve::Closed || valve == Valve::Closing || closeDelay.pending) return;
  setPump(false);
  closeReason = reason;
  closeDelay.request(now);
  fullWarning = true;
  if (valve == Valve::Opening) { valve = Valve::Unknown; releaseStepper(); }
}

void serviceAutomaticClose(uint32_t now) {
  if (!closeDelay.pending) return;
  backlight(((now - closeDelay.startedAt) / 250) % 2 == 0);
  if (!closeDelay.due(now, Config::autoCloseWarningMs)) return;
  closeDelay.pending = fullWarning = false;
  backlight(true);
  filling = false;
  startValve(false, now);
  if (mode == Mode::Auto && autoStage == AutoStage::Analyze) autoStage = AutoStage::Running;
}

void changeMode(Mode next, uint32_t now) {
  if (mode == next) return;
  setPump(false);
  // Do not leave an automatic refill open when handing control back to MANUAL.
  if (mode == Mode::Auto && (filling || fullWarning)) requestAutomaticClose(now, CloseReason::ModeChange);
  mode = next;
  filling = false;
  editingTarget = false;
  backlight(true);
  if (mode == Mode::Auto) {
    autoEnteredAt = now;
    autoStage = AutoStage::Intro;
    autoScreen = AutoScreen::Info;
    fullHandled = emptyHandled = false;
    editedTarget = soilTarget;
  }
}

void warnFull(uint32_t now) {
  setPump(false);
  fullHandled = true;
  requestAutomaticClose(now, CloseReason::Full);
}

void updateAuto(uint32_t now) {
  if (mode != Mode::Auto) return;
  if (autoStage == AutoStage::Intro) {
    if (!elapsed(now, autoEnteredAt, Config::autoIntroMs)) return;
    autoStage = AutoStage::Analyze;
  }
  if (fullWarning || pumpRunning) return;
  if (!tankHealthy(now)) {
    setPump(false);
    // Do not mistake splashing for full/empty or for the end of filling.
    lastGrowthAt = now;
    return;
  }
  if (autoStage == AutoStage::Analyze) {
    if (sensors.tankLevel > 90) {
      warnFull(now);
      if (!fullWarning) autoStage = AutoStage::Running;
      return;
    }
    autoStage = AutoStage::Running;
  }
  if (sensors.tankLevel <= 85) fullHandled = false;
  if (sensors.tankLevel >= 15) emptyHandled = false;
  if (sensors.tankLevel > 90 &&
      (!fullHandled || valve == Valve::Open || valve == Valve::Opening)) {
    warnFull(now);
    return;
  }
  const bool rising = settledRise || fillTrend.rising(Config::fillRisePercent);
  settledRise = false;
  if (!fullHandled && !filling && sensors.tankLevel < 10 && !emptyHandled) {
    filling = emptyHandled = true;
    lastGrowthAt = now;
    setPump(false);
    startValve(true, now);
  }
  if (!fullHandled && rising && !filling) {
    filling = true;
    lastGrowthAt = now;
    setPump(false);
  }
  if (filling) {
    if (rising || valve == Valve::Opening) lastGrowthAt = now;
    if (elapsed(now, lastGrowthAt, Config::fillStableMs)) {
      requestAutomaticClose(now, CloseReason::Settled);
      if (!fullWarning) filling = false;
    }
    return;
  }
  const bool soilHealthy = sensors.soilValid &&
      !elapsed(now, sensors.soilAt, Config::sensorStaleMs);
  if (!soilHealthy || sensors.tankLevel < 10) setPump(false);
  const bool allowed = soilHealthy && sensors.tankLevel >= 10 && !pumpRunning &&
      !accounting && !valveMoving() && valve != Valve::Open;
  if (watering.request(now, sensors.soilPercent, soilTarget, soilHealthy,
                       allowed, Config::autoSoakMs, Config::relativeMoistureMargins)) {
    pumpStarted = now;
    pumpDurationMs = Config::pumpRunMs;
    setPump(true);
  }
}

void handleButton(Button button, uint32_t now) {
  if (button == Button::None) return;
  if (closeDelay.pending) return; // Finish the announced automatic closure first.
  if (mode == Mode::Auto && !choosingMode && !fullWarning &&
      autoStage == AutoStage::Running && !filling && autoScreen == AutoScreen::Reference) {
    if (button == Button::Up || button == Button::Down) {
      if (!editingTarget) editedTarget = soilTarget;
      editingTarget = true;
      editedTarget = AutoLogic::nextTarget(editedTarget, button == Button::Up);
      return;
    }
    if (button == Button::Select && editingTarget) {
      soilTarget = editedTarget;
      EEPROM.update(0, soilTarget);
      EEPROM.update(1, uint8_t(~soilTarget));
      editingTarget = false;
      return;
    }
  }
  if (button == Button::Select) {
    if (mode == Mode::Manual && screen == Screen::Drain) setPump(false);
    if (!choosingMode) { pendingMode = mode; choosingMode = true; }
    else {
      changeMode(pendingMode, now);
      choosingMode = false;
      screen = Screen::Air;
    }
    return;
  }
  if (choosingMode) {
    if (button == Button::Left || button == Button::Right)
      pendingMode = pendingMode == Mode::Manual ? Mode::Auto : Mode::Manual;
    return;
  }
  if (mode == Mode::Auto) {
    if (fullWarning || filling || autoStage != AutoStage::Running) return;
    if (button == Button::Left || button == Button::Right) {
      const uint8_t count = static_cast<uint8_t>(AutoScreen::Count);
      autoScreen = static_cast<AutoScreen>((static_cast<uint8_t>(autoScreen) +
          (button == Button::Right ? 1 : count - 1)) % count);
      editingTarget = false; // Leaving without SELECT discards the draft.
    } else if (autoScreen == AutoScreen::Valve) {
      if (button == Button::Down) startValve(false, now);
      if (button == Button::Up && tankHealthy(now) && sensors.tankLevel <= 90) {
        setPump(false);
        startValve(true, now);
      }
    }
    return;
  }
  if (button == Button::Left || button == Button::Right) {
    if (screen == Screen::Drain) setPump(false);
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
      pumpDurationMs = Config::pumpRunMs;
      setPump(true);
    }
  } else if (screen == Screen::Drain) {
    if (button == Button::Down) setPump(false);
    // Like the ultrasonic diagnostic, draining is a timed manual override:
    // do not require a settled level or apply the calibrated empty cutoff.
    if (button == Button::Up && !pumpRunning) {
      pumpStarted = now;
      pumpDurationMs = Config::drainRunMs;
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

void renderAuto(LcdLine &top, LcdLine &bottom, uint32_t now) {
  if (autoStage == AutoStage::Intro) {
    top.print(F("A Soil target")); bottom.print(soilTarget); bottom.print('%');
    return;
  }
  if (autoStage == AutoStage::Analyze) {
    top.print(F("A Tank analysis")); bottom.print(tankStatus(now)); return;
  }
  if (filling) {
    top.print(sensors.tankLevel < 10 ? F("A TANK EMPTY") : F("A Filling tank"));
    if (tankHealthy(now)) {
      bottom.print(F("Level: ")); bottom.print(sensors.tankPercent); bottom.print('%');
    } else bottom.print(F("Wait for steady"));
    return;
  }
  switch (autoScreen) {
    case AutoScreen::Reference:
      top.print(F("A Soil target")); if (editingTarget) top.print('*');
      bottom.print(editingTarget ? editedTarget : soilTarget);
      bottom.print(F("% U/D SEL save"));
      break;
    case AutoScreen::Mode:
      top.print(F("A Mode: AUTO")); bottom.print(F("SEL change mode")); break;
    case AutoScreen::Valve:
      top.print(F("A Valve:")); top.print(valveText());
      if (tankHealthy(now)) {
        bottom.print(F("Tank:")); bottom.print(sensors.tankPercent); bottom.print(F("% U+ D-"));
      } else bottom.print(tankStatus(now));
      break;
    case AutoScreen::Info: {
      const uint8_t page = (now / Config::infoPageMs) % 8;
      const bool airOk = sensors.airValid && !elapsed(now, sensors.airAt, Config::sensorStaleMs);
      const float baseline = Config::traditionalWaterMlPerDay * (monitorSeconds / 86400.0f);
      const bool savingsKnown = !accounting && waterEstimateValid && baseline > 0;
      const float saved = baseline - usedWaterMl;
      switch (page) {
        case 0:
          top.print(F("A Air temp"));
          if (airOk) { bottom.print(sensors.temperature, 1); bottom.print(F(" C")); }
          else bottom.print(F("Air sensor ERR"));
          break;
        case 1:
          top.print(F("A Air humidity"));
          if (airOk) { bottom.print(sensors.humidity, 1); bottom.print('%'); }
          else bottom.print(F("Air sensor ERR"));
          break;
        case 2:
          top.print(F("A Soil moisture"));
          if (!sensors.soilValid) bottom.print(F("Soil sensor ERR"));
          else {
            bottom.print(sensors.soilPercent); bottom.print(F("% "));
            if (pumpRunning) bottom.print(F("Watering"));
            else if (watering.waiting) bottom.print(F("Soaking"));
            else if (sensors.soilPercent >= soilTarget) bottom.print(F("Target OK"));
            else bottom.print(F("Monitoring"));
          }
          break;
        case 3: {
          const auto fault = diagnostic(now);
          top.print(fault ? F("A System fault") : F("A System OK"));
          bottom.print(fault ? fault : F("Sensors OK"));
          break;
        }
        case 4:
          top.print(F("A Used water est"));
          if (accounting) bottom.print(F("Wait for steady"));
          else if (waterEstimateValid) { bottom.print(usedWaterMl, 0); bottom.print(F(" mL")); }
          else bottom.print(F("N/A: level fault"));
          break;
        case 5:
          top.print(F("A Saved water"));
          if (savingsKnown) { bottom.print(saved, 0); bottom.print(F(" mL est")); }
          else bottom.print(accounting ? F("Wait for steady") : Config::traditionalWaterMlPerDay > 0 ? F("N/A: level fault") : F("Set baseline/day"));
          break;
        case 6:
          top.print(F("A Efficiency"));
          if (savingsKnown) {
            bottom.print(constrain((saved / baseline) / 0.70f * 100.0f, 0.0f, 100.0f), 0);
            bottom.print(F("% of 70% goal"));
          } else bottom.print(accounting ? F("Wait for steady") : Config::traditionalWaterMlPerDay > 0 ? F("N/A: level fault") : F("Set baseline/day"));
          break;
        case 7:
          top.print(F("A Plant monitor"));
          bottom.print(monitorSeconds / 86400UL); bottom.print(F("d "));
          bottom.print((monitorSeconds / 3600UL) % 24); bottom.print(F("h "));
          bottom.print((monitorSeconds / 60UL) % 60); bottom.print(F("m "));
          bottom.print(monitorSeconds % 60); bottom.print('s');
          break;
      }
      break;
    }
    default: break;
  }
}

void render(uint32_t now) {
  static uint32_t lastDisplay = 0;
  static char previous[2][17] = {};
  if (!elapsed(now, lastDisplay, Config::displayRefreshMs)) return;
  lastDisplay = now;
  LcdLine top, bottom;
  if (closeDelay.pending) {
    top.print(mode == Mode::Auto ? F("A ") : F("M "));
    top.print(closeReason == CloseReason::Full ? F("TANK FULL") : F("VALVE CLOSING"));
    if (((now - closeDelay.startedAt) / 250) % 2 == 0) bottom.print(F("Please wait 3s"));
  } else if (choosingMode) {
    top.print(mode == Mode::Auto ? F("A Mode: ") : F("Mode: "));
    top.print(pendingMode == Mode::Manual ? F("MANUAL") : F("AUTO"));
    bottom.print(F("L/R choose SEL OK"));
  } else if (mode == Mode::Auto) {
    renderAuto(top, bottom, now);
  } else {
    switch (screen) {
      case Screen::Air:
        top.print(F("MANUAL 1/7 Air"));
        if (!sensors.airValid || elapsed(now, sensors.airAt, Config::sensorStaleMs))
          bottom.print(F("Air sensor ERR"));
        else if ((now / Config::airDisplayMs) % 2 == 0) {
          bottom.print(F("Temp: ")); bottom.print(sensors.temperature, 1); bottom.print(F(" C"));
        } else {
          bottom.print(F("Humidity: ")); bottom.print(sensors.humidity, 1); bottom.print('%');
        }
        break;
      case Screen::Soil:
        top.print(F("MANUAL 2/7 Soil"));
        if (!sensors.soilValid) bottom.print(F("Soil sensor ERR"));
        else { bottom.print(F("Moisture: ")); bottom.print(sensors.soilPercent); bottom.print('%'); }
        break;
      case Screen::SoilDebug:
        top.print(F("M 3/7 Soil debug"));
        if (!sensors.soilAt) bottom.print(F("ADC: waiting"));
        else {
          bottom.print(F("Raw ADC: ")); bottom.print(sensors.soilRaw);
        }
        break;
      case Screen::Valve:
        top.print(F("Valve: ")); top.print(valveText());
        if (tankHealthy(now)) {
          bottom.print(F("Tank:")); bottom.print(sensors.tankPercent);
          bottom.print(F("% U+ D-"));
        } else bottom.print(tankStatus(now));
        break;
      case Screen::Pump:
        top.print(F("Pump: ")); top.print(pumpRunning ? F("Running") : F("Off"));
        if (pumpRunning) bottom.print(F("DOWN: stop"));
        else if (!tankHealthy(now)) bottom.print(tankStatus(now));
        else bottom.print(sensors.distanceMm < Config::tankEmptyMm ?
                          F("UP:2s DOWN:stop") : F("Tank empty"));
        break;
      case Screen::Drain:
        top.print(F("Drain "));
        if (pumpRunning) {
          const uint32_t runMs = uint32_t(now - pumpStarted);
          top.print(runMs >= pumpDurationMs ? 0UL :
                    (pumpDurationMs - runMs + 999) / 1000);
          top.print(F("s D:stop"));
        } else top.print(F("U:10s D:stop"));
        if (!sensors.tankSampleAt) bottom.print(F("US: waiting"));
        else if (!sensors.echoUs || !isfinite(sensors.rawDistanceMm))
          bottom.print(F("US: no echo"));
        else {
          bottom.print(F("US: ")); bottom.print(sensors.rawDistanceMm, 1);
          bottom.print(F(" mm"));
        }
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
  const uint8_t savedTarget = EEPROM.read(0);
  if (AutoLogic::validTarget(savedTarget) && EEPROM.read(1) == uint8_t(~savedTarget))
    soilTarget = savedTarget;
  editedTarget = soilTarget;
  if (Config::tankDebugSerial) {
    Serial.begin(115200);
    Serial.println(F("SmartRePlant MAIN: tank debug, 30ms timeout / 500ms sampling"));
  }
  // Preload the inactive relay level before enabling the output.
  setPump(false);
  pinMode(Config::relayPin, OUTPUT);
  for (uint8_t pin : Config::stepperPins) { digitalWrite(pin, LOW); pinMode(pin, OUTPUT); }
  pinMode(Config::triggerPin, OUTPUT);
  digitalWrite(Config::triggerPin, LOW);
  pinMode(Config::echoPin, INPUT);
  pinMode(Config::buttonsPin, INPUT);
  pinMode(Config::soilPin, INPUT);
  backlight(true);
  lcd.begin(16, 2);
  dht.begin();
}

void loop() {
  static uint32_t lastMonitorTick = 0;
  const uint32_t tick = millis();
  const uint32_t seconds = uint32_t(tick - lastMonitorTick) / 1000;
  monitorSeconds += seconds;
  lastMonitorTick += seconds * 1000;
  updateActuators(millis());
  const Button button = buttonPressed(millis());
  readSensors(millis());
  // Refresh the clock after the bounded sensor reads.
  const uint32_t now = millis();
  handleButton(button, now);
  serviceAutomaticClose(millis());
  updateAuto(millis());
  updateActuators(millis());
  render(millis());
}
