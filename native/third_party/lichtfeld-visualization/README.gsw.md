# LichtFeld viewport footprint adaptation

See [PORT_LEDGER.json](PORT_LEDGER.json) for file identities, copied symbols,
license notices, modifications and tests. The input is LichtFeld Studio's
[`utils.slang` at `3152c3f`](https://github.com/MrNeRF/LichtFeld-Studio/blob/3152c3f4709a92873ceb5a674ff937817b1f9a4e/src/rendering/rasterizer/vulkan/shader/src/slang/utils.slang),
whose Apache-2.0 notice remains in [`PreviewFootprint.glsl`](PreviewFootprint.glsl).
The full [Apache License 2.0](LICENSE-APACHE-2.0.txt) accompanies the adaptation.

This directory does not include LS's trainer, scene graph, tile compositor,
CUDA/Vulkan interop, Slang compiler, point-cloud budget or vendor binaries. The
[unmodified LS NVIDIA exception](LICENSE-EXCEPTION.ls.txt) is retained as source
provenance only, not relied on or extended to any other code. The GPL scope of
the original GSW desktop and the terms of the existing worker/third-party
modules remain unchanged.

The shader runs in GSW's existing OpenGL Gaussian viewport only when the user
opts in and the active training observation is compatible. Default, 2DGS,
Full Mip-Splatting, reference models and non-training stages keep their current
rendering paths. See [design and acceptance boundaries](../../../docs/LICHTFELD_VISUALIZATION.md).
