#pragma once
#include <Arduino.h>
#include <DHT.h>

namespace Config {
constexpr uint8_t buttonsPin = A0;
constexpr uint8_t soilPin = A1;
constexpr uint8_t dhtPin = A2;
constexpr uint8_t relayPin = A3;
constexpr uint8_t triggerPin = 12;
constexpr uint8_t echoPin = 2;
constexpr uint8_t stepperPins[] = {3, 13, A4, A5}; // IN1, IN2, IN3, IN4
constexpr uint8_t dhtType = DHT11;
constexpr bool relayActiveLow = true;
constexpr bool reverseStepper = false; // Toggle if UP turns counterclockwise.
constexpr uint32_t valveTravelMs = 5000;
constexpr uint32_t pumpRunMs = 2000;
constexpr uint32_t stepIntervalUs = 2000; // Half steps; tune to the actual motor.
constexpr uint32_t debounceMs = 40;
constexpr uint32_t tankReadMs = 500; // Match the verified ultrasonic diagnostic.
constexpr uint32_t soilReadMs = 250;
constexpr uint32_t airReadMs = 2500;
constexpr uint32_t sensorStaleMs = 10000;
constexpr uint32_t displayRefreshMs = 200;
constexpr uint32_t airDisplayMs = 2500;
constexpr uint8_t defaultSoilTarget = 60;
constexpr bool relativeMoistureMargins = false; // Otherwise percentage points.
constexpr uint32_t autoIntroMs = 5000;
constexpr uint32_t autoSoakMs = 5UL * 60UL * 1000UL;
constexpr uint32_t startupFullWarningMs = 3000;
constexpr uint32_t fullWarningMs = 5000;
constexpr uint32_t fillStableMs = 10000;
constexpr uint32_t waterSettleMs = 3000;
constexpr uint32_t infoPageMs = 3000;
constexpr float tankOutlierMm = 3.0f;
constexpr float tankJumpMm = 6.0f;
constexpr uint8_t tankConfirmReadings = 3;
constexpr uint32_t tankHoldMs = 1500; // Brief dropout tolerance, not ten-second stale data.
constexpr float fillRisePercent = 2.0f;
constexpr float tankCapacityMl = 1000.0f;
// User-provided traditional watering baseline; 0 disables savings estimates.
constexpr float traditionalWaterMlPerDay = 40.0f;
constexpr uint8_t backlightPin = 10;
// Covers waiting for the echo to begin AND measuring its pulse width.
constexpr unsigned long echoTimeoutUs = 30000;
constexpr bool tankDebugSerial = true; // Raw readings at 115200 baud while idle.
constexpr float tankEmptyMm = 55.0f;
constexpr float tankFullMm = 5.0f; // 15 mm more usable depth than the previous full point.
// HC-SR04 minimum range: the sensor must be raised/recalibrated to measure full.
constexpr float tankMinReadableMm = 20.0f;
constexpr float tankMaxPlausibleMm = 80.0f;
// Initial calibration estimates: replace with measured dry/wet readings.
constexpr int soilDryAdc = 800;
constexpr int soilWetAdc = 350;
constexpr int soilMinValidAdc = 1;
constexpr int soilMaxValidAdc = 1022;
// DHT11 operating limits; not plant-specific watering thresholds.
constexpr float minTemperature = 0.0f;
constexpr float maxTemperature = 50.0f;
constexpr float minHumidity = 20.0f;
constexpr float maxHumidity = 90.0f;
// Typical LCD keypad shield ADC thresholds. Calibrate for your shield.
constexpr int rightThreshold = 50;
constexpr int upThreshold = 195;
constexpr int downThreshold = 380;
constexpr int leftThreshold = 555;
constexpr int selectThreshold = 790;
static_assert(soilDryAdc != soilWetAdc, "Soil calibration points must differ");
static_assert(tankEmptyMm > tankFullMm, "Empty distance must exceed full distance");
static_assert(tankConfirmReadings >= 2, "Tank changes require multiple readings");
}
