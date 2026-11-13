/*
 * Copyright (C) 2020, Inria
 * GRAPHDECO research group, https://team.inria.fr/graphdeco
 * All rights reserved.
 *
 * This software is free for non-commercial, research and evaluation use
 * under the terms of the LICENSE.md file.
 *
 * For inquiries contact sibr@inria.fr and/or George.Drettakis@inria.fr
 */

// Modified by GSW (2026-11-13) from src/core/view/FPSCounter.cpp.
// The 2024 upstream LICENSE.md applies to this core file; the older
// research/evaluation wording above is retained as part of the original notice.
// The external-timing ring update and the arithmetic from render() are kept;
// Qt owns the clock, display and visibility. See README.gsw.md.

#include "FPSCounter.hpp"

namespace gsw::sibr_fps {

void FPSCounter::update(double deltaTime) noexcept {
  _frameTimeSum -= _frameTimes[_frameIndex];
  _frameTimeSum += deltaTime;
  _frameTimes[_frameIndex] = deltaTime;
  _frameIndex = (_frameIndex + 1) % kSmoothingWindow;
  if (_sampleCount < kSmoothingWindow) ++_sampleCount;
}

double FPSCounter::averageFrameSeconds() const noexcept {
  return _sampleCount ? _frameTimeSum / static_cast<double>(_sampleCount) : 0.0;
}

double FPSCounter::framesPerSecond() const noexcept {
  const double frameTime = averageFrameSeconds();
  return frameTime > 0.0 ? 1.0 / frameTime : 0.0;
}

} // namespace gsw::sibr_fps
