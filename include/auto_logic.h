#pragma once
#include <stdint.h>

namespace AutoLogic {
constexpr bool elapsed(uint32_t now, uint32_t since, uint32_t interval) {
  return uint32_t(now - since) >= interval;
}
constexpr uint8_t nextTarget(uint8_t value, bool up) {
  return up ? (value >= 95 ? 95 : value < 70 ? value + 10 : value + 5)
            : (value <= 20 ? 20 : value <= 70 ? value - 10 : value - 5);
}
constexpr bool validTarget(uint8_t v) {
  return v >= 20 && v <= 95 && (v <= 70 ? v % 10 == 0 : v % 5 == 0);
}

struct Watering {
  bool active = false;
  bool dosing = false;
  bool waiting = false;
  uint32_t stoppedAt = 0;
  constexpr bool request(uint32_t now, int soil, int target, bool valid,
                         bool allowed, uint32_t soakMs, bool relative = false) {
    if (dosing || !valid) return false;
    if (soil >= target) active = false;
    if (waiting) {
      if (!elapsed(now, stoppedAt, soakMs)) return false;
      waiting = false;
    }
    const int margin = active ? 2 : 10;
    const bool dry = relative ? soil * 100 < target * (100 - margin)
                              : soil < target - margin;
    if (!allowed || !dry) return false;
    active = dosing = true;
    return true;
  }
  constexpr void finished(uint32_t now) {
    if (!dosing) return;
    dosing = false;
    waiting = true;
    stoppedAt = now;
  }
};

// Median-based outlier rejection, followed by the mean of retained samples.
struct LevelFilter {
  float values[5] = {};
  uint8_t count = 0, cursor = 0;
  constexpr void clear() { count = cursor = 0; }
  constexpr float add(float value, float toleranceMm) {
    values[cursor] = value;
    cursor = (cursor + 1) % 5;
    if (count < 5) ++count;
    float sorted[5] = {};
    for (uint8_t i = 0; i < count; ++i) sorted[i] = values[i];
    for (uint8_t i = 1; i < count; ++i) {
      const float item = sorted[i];
      uint8_t j = i;
      while (j && sorted[j - 1] > item) { sorted[j] = sorted[j - 1]; --j; }
      sorted[j] = item;
    }
    const float median = sorted[count / 2];
    float sum = 0;
    uint8_t kept = 0;
    for (uint8_t i = 0; i < count; ++i) {
      const float d = sorted[i] - median;
      if (d >= -toleranceMm && d <= toleranceMm) { sum += sorted[i]; ++kept; }
    }
    return sum / kept;
  }
};

// Validate startup and large changes before they enter the five-sample filter.
// Rejected samples never refresh the trusted reading's timestamp.
struct TankFilter {
  LevelFilter window;
  float output = 0, candidate = 0;
  uint8_t confirmations = 0;
  uint32_t acceptedAt = 0;
  bool ready = false, fresh = false;
  constexpr bool healthy(uint32_t now, uint32_t holdMs) const {
    return ready && !elapsed(now, acceptedAt, holdMs);
  }
  constexpr bool observe(float value, bool valid, uint32_t now,
                         float toleranceMm, float jumpMm,
                         uint8_t required, uint32_t holdMs) {
    fresh = false;
    if (ready && !healthy(now, holdMs)) {
      ready = false;
      window.clear();
    }
    if (!valid) {
      confirmations = 0;
      return healthy(now, holdMs);
    }
    const float difference = value - output;
    if (!ready || difference > jumpMm || difference < -jumpMm) {
      const float candidateDifference = value - candidate;
      if (!confirmations || candidateDifference > toleranceMm ||
          candidateDifference < -toleranceMm) {
        candidate = value;
        confirmations = 1;
      } else {
        candidate = (candidate * confirmations + value) / (confirmations + 1);
        ++confirmations;
      }
      if (confirmations < required) return healthy(now, holdMs);
      // A confirmed step must not remain stuck behind the old median window.
      window.clear();
      output = window.add(candidate, toleranceMm);
    } else {
      output = window.add(value, toleranceMm);
    }
    confirmations = 0;
    ready = fresh = true;
    acceptedAt = now;
    return true;
  }
};

// Require a full window of in-range RAW readings to be steady, not just a
// smooth filtered output. Invalid readings/movement restart qualification.
struct SettledLevel {
  float values[5] = {};
  float output = 0;
  uint8_t count = 0, cursor = 0;
  uint32_t acceptedAt = 0, stableSince = 0;
  bool fresh = false, ready = false, qualifying = false;
  constexpr void invalidate() {
    count = cursor = 0;
    fresh = ready = qualifying = false;
  }
  constexpr bool healthy(uint32_t now, uint32_t maxAge) const {
    return ready && !elapsed(now, acceptedAt, maxAge);
  }
  constexpr bool observe(float value, bool valid, uint32_t now,
                         float maxSpread, uint32_t steadyMs, uint32_t maxGap) {
    fresh = false;
    if (ready && !healthy(now, maxGap)) invalidate();
    if (!valid) { invalidate(); return false; }
    values[cursor] = value;
    cursor = (cursor + 1) % 5;
    if (count < 5) ++count;
    float low = values[0], high = values[0], sum = 0;
    for (uint8_t i = 0; i < count; ++i) {
      if (values[i] < low) low = values[i];
      if (values[i] > high) high = values[i];
      sum += values[i];
    }
    if (high - low > maxSpread) {
      ready = qualifying = false;
      return false;
    }
    if (!qualifying) { qualifying = true; stableSince = now; }
    if (count < 5 || !elapsed(now, stableSince, steadyMs)) {
      ready = false;
      return false;
    }
    output = sum / count;
    acceptedAt = now;
    return fresh = ready = true;
  }
};

struct CloseDelay {
  bool pending = false;
  uint32_t startedAt = 0;
  constexpr void request(uint32_t now) {
    if (!pending) { pending = true; startedAt = now; }
  }
  constexpr bool due(uint32_t now, uint32_t delayMs) const {
    return pending && elapsed(now, startedAt, delayMs);
  }
};

struct FillTrend {
  float values[10] = {};
  uint8_t count = 0, cursor = 0;
  constexpr void clear() { count = cursor = 0; }
  constexpr void add(float percent) {
    values[cursor] = percent;
    cursor = (cursor + 1) % 10;
    if (count < 10) ++count;
  }
  constexpr bool rising(float minimumRise) const {
    if (count != 10) return false;
    float early = 0, late = 0;
    for (uint8_t i = 0; i < 5; ++i) {
      early += values[(cursor + i) % 10];
      late += values[(cursor + i + 5) % 10];
    }
    return (late - early) / 5 >= minimumRise;
  }
};
}
