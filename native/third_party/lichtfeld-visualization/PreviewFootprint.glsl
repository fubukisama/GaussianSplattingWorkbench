// Copyright 2024 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GSW adaptation (2026-11-18): LichtFeld Studio 3152c3f, utils.slang.
// Preserve the upstream notice for the adapted portions. This forward-only
// port uses GLSL 330, eye-space covariance and bottom-left OpenGL pixels;
// GSW's model centre, low-pass filter, cutoff and compositor remain separate.
// See PORT_LEDGER.json. No upstream CUDA, Vulkan or Slang runtime is included.

vec3 gswLichtFeldProjectCovariance(vec3 eye, mat3 eyeCovariance,
                                 mat4 projection, vec2 imageSize,
                                 bool orthographic) {
  vec2 focal = 0.5 * imageSize * vec2(projection[0][0], projection[1][1]);
  // LS compute_jacobian_ortho uses an affine derivative. For pinhole,
  // reflect LS's +Z depth and +Y down to OpenGL −Z forward/+Y up.
  vec3 jacobianX = vec3(focal.x, 0.0, 0.0);
  vec3 jacobianY = vec3(0.0, focal.y, 0.0);
  if (!orthographic) {
    float depth = -eye.z;
    // Recover the bottom-left principal point. The centre itself is still
    // projected by GSW's clip-space matrix; only the derivative is clamped.
    vec2 principal = 0.5 * imageSize * (vec2(1.0) - projection[2].xy);
    vec2 tanHalfFov = 0.5 * imageSize / focal;
    vec2 positiveLimit = (imageSize - principal) / focal + 0.3 * tanHalfFov;
    vec2 negativeLimit = principal / focal + 0.3 * tanHalfFov;
    vec2 clampedRay = min(positiveLimit, max(-negativeLimit, eye.xy / depth));
    jacobianX = vec3(focal.x / depth, 0.0, focal.x * clampedRay.x / depth);
    jacobianY = vec3(0.0, focal.y / depth, focal.y * clampedRay.y / depth);
  }
  return vec3(dot(jacobianX, eyeCovariance * jacobianX),
              dot(jacobianX, eyeCovariance * jacobianY),
              dot(jacobianY, eyeCovariance * jacobianY));
}

vec3 gswLichtFeldClampFootprint(vec3 cov, float opacity) {
  // Port of LS clamp_covariance_2d_extent. These constants define the
  // upstream extent calculation, not the GSW fragment opacity threshold.
  const float maxPixelRadius = 512.0;
  const float alphaThreshold = 0.5 / 255.0;
  const float maxPowerThreshold = 4.0;
  float opacitySafe = max(opacity, alphaThreshold + 1e-8);
  float powerThreshold = max(maxPowerThreshold, log(opacitySafe / alphaThreshold));
  float stddev = sqrt(2.0 * powerThreshold);
  float maxSigma = (maxPixelRadius + 0.5) / max(stddev, 1e-6);
  float maxVariance = maxSigma * maxSigma;

  float eigenAvg = 0.5 * (cov.x + cov.z);
  // Algebraically the same discriminant as eigenAvg² − det; evaluating
  // the difference of the entries first avoids cancellation for round splats.
  float eigenDelta = length(vec2(0.5 * (cov.x - cov.z), cov.y));
  float eigen1 = eigenAvg + eigenDelta;
  float eigen2 = max(eigenAvg - eigenDelta, 0.0);
  float clamped1 = min(eigen1, maxVariance);
  float clamped2 = min(eigen2, maxVariance);
  if (clamped1 >= eigen1 && clamped2 >= eigen2) return cov;

  // Use a safe axis as the eigenvector approaches a diagonal covariance.
  vec2 axis1 = abs(cov.y) > 1e-6 ? normalize(vec2(cov.y, eigen1 - cov.x))
      : (cov.x >= cov.z ? vec2(1.0, 0.0) : vec2(0.0, 1.0));
  vec2 axis2 = vec2(axis1.y, -axis1.x);
  return vec3(clamped1 * axis1.x * axis1.x + clamped2 * axis2.x * axis2.x,
              clamped1 * axis1.x * axis1.y + clamped2 * axis2.x * axis2.y,
              clamped1 * axis1.y * axis1.y + clamped2 * axis2.y * axis2.y);
}
