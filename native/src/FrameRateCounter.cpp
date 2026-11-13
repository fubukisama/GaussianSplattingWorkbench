#include "FrameRateCounter.h"

#include <cmath>

namespace gsw {

void FrameRateCounter::pause() {
  mLastCompletionTime.reset();
  mHasFreshInterval = false;
}

void FrameRateCounter::frameCompleted(const TimePoint completionTime) {
  if (!mLastCompletionTime.has_value()) {
    mLastCompletionTime = completionTime;
    return;
  }

  if (completionTime <= *mLastCompletionTime) return;
  const double seconds =
      std::chrono::duration<double>(completionTime - *mLastCompletionTime).count();
  if (!std::isfinite(seconds) || seconds <= 0.0) return;
  mLastCompletionTime = completionTime;
  mCounter.update(seconds);
  mHasFreshInterval = true;
}

std::size_t FrameRateCounter::sampleCount() const { return mCounter.sampleCount(); }

double FrameRateCounter::averageFrameMilliseconds() const {
  return 1000.0 * mCounter.averageFrameSeconds();
}

double FrameRateCounter::framesPerSecond() const {
  return mCounter.framesPerSecond();
}

} // namespace gsw
