#include <Arduino.h>
#include <LiquidCrystal.h>
#include "config.h"

namespace {
LiquidCrystal lcd(8, 9, 4, 5, 6, 7);
// Match the timing used to verify the sensor on the bench.
constexpr unsigned long diagnosticTimeoutUs = 30000;
uint32_t lastRead = 0;
constexpr uint32_t drainRunMs = 10000;
bool draining = false;
uint32_t drainStartedAt = 0;

void setDrain(bool running) {
  digitalWrite(Config::relayPin, running == Config::relayActiveLow ? LOW : HIGH);
  draining = running;
}

void updateDrain() {
  if (draining && uint32_t(millis() - drainStartedAt) >= drainRunMs) {
    setDrain(false);
    Serial.println(F("Drain complete: pump OFF. Send d for another 10s."));
  }
}

void handleCommand(char command) {
  if (command == 'd' || command == 'D') {
    if (draining) {
      Serial.println(F("Already draining; command ignored (no extension/queue)."));
      return;
    }
    drainStartedAt = millis();
    setDrain(true);
    Serial.println(F("Draining for 10s. Send s to stop early."));
  } else if (command == 's' || command == 'S') {
    setDrain(false);
    Serial.println(F("Pump OFF."));
  } else if (command != '\r' && command != '\n' && command != ' ') {
    Serial.println(F("d: drain 10s; s: stop. Observe water level while draining."));
  }
}

void blankRow(uint8_t row) {
  lcd.setCursor(0, row);
  lcd.print(F("                "));
  lcd.setCursor(0, row);
}

void measure() {
  const bool echoBefore = digitalRead(Config::echoPin) == HIGH;
  unsigned long duration = 0;
  if (!echoBefore) {
    digitalWrite(Config::triggerPin, LOW);
    delayMicroseconds(2);
    digitalWrite(Config::triggerPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(Config::triggerPin, LOW);
    duration = pulseIn(Config::echoPin, HIGH, diagnosticTimeoutUs);
  }
  const bool echoAfter = digitalRead(Config::echoPin) == HIGH;
  updateDrain(); // Service the deadline immediately after the bounded echo read.
  const float distance = duration * 0.343f / 2.0f;
  const __FlashStringHelper *status;
  if (echoBefore) status = F("ECHO HIGH idle");
  else if (!duration) status = echoAfter ? F("ECHO HIGH late") : F("No full echo");
  else if (duration >= Config::echoTimeoutUs) status = F("App timeout");
  else if (distance < Config::tankMinReadableMm) status = F("Below min range");
  else if (distance > Config::tankMaxPlausibleMm) status = F("Above max range");
  else status = F("Tank range OK");

  blankRow(0);
  if (duration) {
    lcd.print(distance, 1);
    lcd.print(F(" mm"));
    if (draining) {
      lcd.print(F(" D"));
      const uint32_t elapsedMs = millis() - drainStartedAt;
      lcd.print(elapsedMs >= drainRunMs ? 0UL : (drainRunMs - elapsedMs + 999) / 1000);
      lcd.print('s');
    }
  } else {
    lcd.print(F("T")); lcd.print(Config::triggerPin);
    lcd.print(F(" E")); lcd.print(Config::echoPin);
    lcd.print(F(" No distance"));
  }
  blankRow(1);
  lcd.print(status);

  Serial.print(F("ms=")); Serial.print(millis());
  Serial.print(F(" trig=D")); Serial.print(Config::triggerPin);
  Serial.print(F(" echo=D")); Serial.print(Config::echoPin);
  Serial.print(F(" before=")); Serial.print(echoBefore);
  Serial.print(F(" after=")); Serial.print(echoAfter);
  Serial.print(F(" pulse_us=")); Serial.print(duration);
  Serial.print(F(" distance_mm="));
  if (duration) Serial.print(distance, 2);
  else Serial.print(F("NA"));
  Serial.print(F(" pump=")); Serial.print(draining ? F("DRAINING") : F("OFF"));
  Serial.print(F(" status=")); Serial.println(status);
}
}

void setup() {
  digitalWrite(Config::relayPin, Config::relayActiveLow ? HIGH : LOW);
  pinMode(Config::relayPin, OUTPUT);
  for (uint8_t pin : Config::stepperPins) {
    digitalWrite(pin, LOW);
    pinMode(pin, OUTPUT);
  }
  digitalWrite(Config::triggerPin, LOW);
  pinMode(Config::triggerPin, OUTPUT);
  pinMode(Config::echoPin, INPUT);
  Serial.begin(115200);
  lcd.begin(16, 2);
  Serial.println(F("HC-SR04 diagnostic: raw readings every 500ms; timeout 30000us"));
  Serial.println(F("d: drain 10s; s: stop. No calibrated empty cutoff in this test."));
}

void loop() {
  while (Serial.available()) handleCommand(char(Serial.read()));
  updateDrain();
  const uint32_t now = millis();
  if (uint32_t(now - lastRead) < 500) return;
  lastRead = now;
  measure();
  updateDrain();
}
