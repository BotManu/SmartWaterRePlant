#pragma once
#include "auto_logic.h"

namespace AutoChecks {
constexpr bool wateringCycle() {
  AutoLogic::Watering w;
  if (w.request(0, 50, 60, true, true, 300000)) return false;
  if (!w.request(1, 49, 60, true, true, 300000)) return false;
  if (w.request(1000, 0, 60, true, true, 300000)) return false;
  w.finished(2001);
  if (w.request(302000, 0, 60, true, true, 300000)) return false;
  if (w.request(302001, 58, 60, true, true, 300000)) return false;
  if (!w.request(302002, 57, 60, true, true, 300000)) return false;
  w.finished(304002);
  if (w.request(604002, 60, 60, true, true, 300000)) return false;
  if (w.active) return false;
  return !w.request(604003, 55, 60, true, true, 300000);
}
constexpr bool faultsAndRollover() {
  AutoLogic::Watering w;
  if (w.request(0, 0, 60, false, true, 300000)) return false;
  if (w.request(0, 0, 60, true, false, 300000)) return false;
  if (!w.request(0xFFFFFF00UL, 0, 60, true, true, 300000)) return false;
  w.finished(0xFFFFFFF0UL);
  if (w.request(100, 0, 60, true, true, 300000)) return false;
  return w.request(uint32_t(0xFFFFFFF0UL + 300000UL), 0, 60, true, true, 300000);
}
constexpr bool relativeMargins() {
  AutoLogic::Watering w;
  if (w.request(0, 54, 60, true, true, 300000, true)) return false;
  if (!w.request(0, 53, 60, true, true, 300000, true)) return false;
  w.finished(2000);
  if (w.request(302000, 59, 60, true, true, 300000, true)) return false;
  return w.request(302001, 58, 60, true, true, 300000, true);
}
constexpr bool filterOutlier() {
  AutoLogic::LevelFilter f;
  f.add(36, 3); f.add(36, 3); f.add(36, 3); f.add(36, 3);
  if (f.add(79, 3) != 36) return false;
  f.add(25, 3); f.add(25, 3); f.add(25, 3);
  if (f.add(25, 3) != 25) return false;
  f.clear();
  return f.add(55, 3) == 55;
}
constexpr bool trend() {
  AutoLogic::FillTrend t;
  for (uint8_t i = 0; i < 9; ++i) t.add(i);
  if (t.rising(2)) return false;
  t.add(9);
  if (!t.rising(2)) return false;
  for (uint8_t i = 0; i < 10; ++i) t.add(9);
  if (t.rising(2)) return false;
  for (uint8_t i = 0; i < 10; ++i) t.add(20 - i);
  return !t.rising(2);
}
constexpr bool sample(AutoLogic::TankFilter &f, float mm, uint32_t ms, bool valid = true) {
  return f.observe(mm, valid, ms, 3, 6, 3, 1500);
}
constexpr bool tankStartupAndSpikes() {
  AutoLogic::TankFilter f;
  if (sample(f, 79, 0)) return false; // Bad first echo must not set the level.
  if (sample(f, 36, 500) || sample(f, 36, 1000)) return false;
  if (!sample(f, 36, 1500) || f.output != 36) return false;
  if (!sample(f, 55, 2000) || f.fresh || f.output != 36) return false;
  if (!sample(f, 55, 2500) || f.fresh || f.output != 36) return false;
  // Two bad echoes cannot cause empty/full or consumption changes.
  if (!sample(f, 36, 2900) || !f.fresh || f.output != 36) return false;
  return f.acceptedAt == 2900;
}
constexpr bool tankDropoutAndRecovery() {
  AutoLogic::TankFilter f;
  sample(f, 36, 0); sample(f, 36, 500); sample(f, 36, 1000);
  if (!sample(f, 0, 1500, false) || f.fresh) return false;
  if (!sample(f, 0, 2000, false) || f.acceptedAt != 1000) return false;
  if (sample(f, 0, 2500, false)) return false;
  if (sample(f, 55, 3000) || sample(f, 55, 3500)) return false;
  return sample(f, 55, 4000) && f.fresh && f.output == 55;
}
constexpr bool tankRealChangeAndRamp() {
  AutoLogic::TankFilter f;
  sample(f, 55, 0); sample(f, 55, 500); sample(f, 55, 1000);
  sample(f, 22, 1500); sample(f, 22, 2000);
  if (!sample(f, 22, 2500) || f.output != 22) return false;
  sample(f, 55, 3000); sample(f, 55, 3500);
  if (!sample(f, 55, 4000) || f.output != 55) return false;
  for (uint8_t i = 1; i <= 15; ++i) {
    if (!sample(f, 55 - i, 4000 + i * 500UL)) return false;
  }
  return f.output >= 40 && f.output <= 43;
}
constexpr bool tankInconsistentAndWrap() {
  AutoLogic::TankFilter f;
  sample(f, 36, 0); sample(f, 36, 500); sample(f, 36, 1000);
  sample(f, 60, 1500); sample(f, 22, 2000);
  if (sample(f, 60, 2500)) return false; // Contradictory jumps expire held data.
  AutoLogic::TankFilter wrapped;
  sample(wrapped, 36, 0xFFFFFB00UL);
  sample(wrapped, 36, 0xFFFFFD00UL);
  sample(wrapped, 36, 0xFFFFFF00UL);
  if (!sample(wrapped, 0, 244, false)) return false;
  return !sample(wrapped, 0, 1244, false);
}
static_assert(tankStartupAndSpikes(), "Confirm startup and reject two-echo spikes");
static_assert(tankDropoutAndRecovery(), "Hold brief dropouts, expire, and revalidate recovery");
static_assert(tankRealChangeAndRamp(), "Follow sustained filling and draining");
static_assert(tankInconsistentAndWrap(), "Do not refresh age for outliers; handle timer wrap");
static_assert(wateringCycle(), "2s dose/5min wait and 10/2 point thresholds");
static_assert(faultsAndRollover(), "Fault inhibition and timer rollover");
static_assert(relativeMargins(), "Relative threshold option");
static_assert(filterOutlier(), "Reject spikes but follow sustained changes");
static_assert(trend(), "Ten-sample growth vs flat/falling levels");
static_assert(AutoLogic::nextTarget(60, true) == 70, "10-point increments");
static_assert(AutoLogic::nextTarget(70, true) == 75, "5-point increments");
static_assert(AutoLogic::nextTarget(70, false) == 60, "70 to 60");
static_assert(AutoLogic::nextTarget(75, false) == 70, "75 to 70");
static_assert(AutoLogic::nextTarget(20, false) == 20, "Lower bound");
static_assert(AutoLogic::nextTarget(95, true) == 95, "Upper bound");
static_assert(!AutoLogic::validTarget(65), "Reject invalid stored target");
}
