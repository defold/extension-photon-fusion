// Copyright Exit Games GmbH. All Rights Reserved.

#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace FusionCore {

  struct EMAReport {
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    double TotalAvg;
    double TotalAvgPerSecond;
    double CurrentAvgPerSecond;
    double Min;
    double Max;
    TimePoint LastUpdatedTime{};
  };

  struct EMA {
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    private:
    double HalfLife{0.5};
    double Avg{0};
    double AvgPerSecond{0};
    double Min{std::numeric_limits<double>::max()};
    double Max{0};
    TimePoint LastTime{};
    bool HasSample{false};

    public:
    explicit EMA(double halfLife) : HalfLife(halfLife) {}

    [[nodiscard]] EMAReport Report() const {
      double current = 0;
      if (HasSample) {
        double dt = std::chrono::duration<double>(Clock::now() - LastTime).count();
        if (dt < HalfLife) {
          double alpha = 1.0 - std::exp(-dt / HalfLife);
          current = (1.0 - alpha) * AvgPerSecond;
        }
      }

      return {.TotalAvg = Avg, .TotalAvgPerSecond = AvgPerSecond, .CurrentAvgPerSecond = current, .Min = Min, .Max = Max, .LastUpdatedTime = LastTime};
    }

    void Update(double value) {
      auto now = Clock::now();

      if (!HasSample) {
        Avg = value;
        AvgPerSecond = 0;
        HasSample = true;
      } else {
        double dt = std::chrono::duration<double>(now - LastTime).count();
        double alpha = 1.0 - std::exp(-dt / HalfLife);
        Avg = alpha * value + (1.0 - alpha) * Avg;
        AvgPerSecond = dt > 0
          ? alpha * (value / dt) + (1.0 - alpha) * AvgPerSecond
          : AvgPerSecond;
      }

      LastTime = now;
      Min = std::min(Min, value);
      Max = std::max(Max, value);
    }

    void Reset() {
      Avg = 0;
      AvgPerSecond = 0;
      Min = std::numeric_limits<double>::max();
      Max = 0;
      HasSample = false;
    }
  };

}

#include "SharedModeCompat.h"
