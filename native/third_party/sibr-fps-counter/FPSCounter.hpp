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

// Modified by GSW (2026-11-13) from src/core/view/FPSCounter.hpp.
// The 2024 upstream LICENSE.md applies to this core file; the older
// research/evaluation wording above is retained as part of the original notice.
// See README.gsw.md for the pinned source and changes.

#pragma once

#include <array>
#include <cstddef>

namespace gsw::sibr_fps {

class FPSCounter final {
public:
  static constexpr std::size_t kSmoothingWindow = 60;

  // The caller supplies a positive, finite interval in seconds.
  void update(double deltaTime) noexcept;
  [[nodiscard]] std::size_t sampleCount() const noexcept { return _sampleCount; }
  [[nodiscard]] double averageFrameSeconds() const noexcept;
  [[nodiscard]] double framesPerSecond() const noexcept;

private:
  std::array<double, kSmoothingWindow> _frameTimes{};
  std::size_t _frameIndex = 0;
  double _frameTimeSum = 0.0;
  std::size_t _sampleCount = 0;
};

} // namespace gsw::sibr_fps
