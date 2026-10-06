#include <Arduino.h>
#include "config.h"

namespace {
constexpr uint8_t sequence[] = {0x1, 0x3, 0x2, 0x6, 0x4, 0xC, 0x8, 0x9};
constexpr uint32_t intervalUs = 4000; // Slow isolated test; no sensor/LCD delays.
uint32_t startedAt = 0;
uint32_t lastStep = 0;
uint32_t durationMs = 0;
uint8_t phase = 0;
int8_t direction = 0;
bool active = false;

void outputs(uint8_t mask) {
  for (uint8_t i = 0; i < 4; ++i)
    digitalWrite(Config::stepperPins[i], (mask >> i) & 1);
}

void stop() {
  outputs(0);
  active = false;
  direction = 0;
  Serial.println(F("Stopped; all IN outputs LOW"));
}

void command(char key) {
  if (key == '\r' || key == '\n') return;
  if (key == 's') { stop(); return; }
  if (key >= '1' && key <= '4') {
    outputs(uint8_t(1U << (key - '1')));
    direction = 0;
    durationMs = 1000;
    Serial.print(F("IN")); Serial.print(key); Serial.println(F(" HIGH for 1s"));
  } else if (key == 'o' || key == 'c') {
    direction = key == 'o' ? 1 : -1;
    if (Config::reverseStepper) direction = -direction;
    phase = 0;
    outputs(sequence[phase]);
    durationMs = Config::valveTravelMs;
    Serial.println(key == 'o' ? F("Forward sequence for 5s") : F("Reverse sequence for 5s"));
  } else {
    Serial.println(F("1..4: individual input; o/c: rotate; s: stop"));
    return;
  }
  startedAt = millis();
  lastStep = micros();
  active = true;
}
}

void setup() {
  // Keep the water pump off throughout this diagnostic.
  digitalWrite(Config::relayPin, Config::relayActiveLow ? HIGH : LOW);
  pinMode(Config::relayPin, OUTPUT);
  for (uint8_t pin : Config::stepperPins) {
    digitalWrite(pin, LOW);
    pinMode(pin, OUTPUT);
  }
  Serial.begin(115200);
  Serial.println(F("Stepper test: IN1=D3 IN2=D11 IN3=A4 IN4=A5"));
  Serial.println(F("1..4: individual input; o/c: rotate; s: stop"));
}

void loop() {
  if (active && uint32_t(millis() - startedAt) >= durationMs) stop();
  if (Serial.available()) command(char(Serial.read()));
  if (!active || direction == 0) return;
  const uint32_t now = micros();
  if (uint32_t(now - lastStep) < intervalUs) return;
  lastStep = now;
  phase = (phase + (direction > 0 ? 1 : 7)) & 7;
  outputs(sequence[phase]);
}
