#pragma once

#include "FPSCounter.hpp"

#include <chrono>
#include <cstddef>
#include <optional>

namespace gsw {

class FrameRateCounter final {
public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;
  static constexpr std::size_t kSmoothingWindow = sibr_fps::FPSCounter::kSmoothingWindow;

  void frameCompleted(TimePoint completionTime);
  // End a presentation burst without discarding measured render intervals.
  // The first completion after resumption is an anchor, not an idle sample.
  void pause();

  [[nodiscard]] std::size_t sampleCount() const;
  [[nodiscard]] bool hasFreshInterval() const { return mHasFreshInterval; }
  [[nodiscard]] double averageFrameMilliseconds() const;
  [[nodiscard]] double framesPerSecond() const;

private:
  sibr_fps::FPSCounter mCounter;
  std::optional<TimePoint> mLastCompletionTime;
  bool mHasFreshInterval = false;
};

} // namespace gsw
