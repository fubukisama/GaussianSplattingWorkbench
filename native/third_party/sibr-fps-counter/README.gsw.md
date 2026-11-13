# SIBR FPSCounter adaptation

Source: [Inria SIBR Core](https://gitlab.inria.fr/sibr/sibr_core), official
`develop` commit
[`0103f7fd75deb0f392c339c74a4ec1cf728e31d1`](https://gitlab.inria.fr/sibr/sibr_core/-/commit/0103f7fd75deb0f392c339c74a4ec1cf728e31d1).
Digests are of the **original Git blob bytes**, including upstream line endings:

| Upstream file | Git blob | SHA-256 |
| --- | --- | --- |
| `src/core/view/FPSCounter.hpp` | `c45356c66ad958bec7460490611acdbaea8f12eb` | `5ac6e31883b7e20548ebec464ba30484ba5805708fbca55af9394d3502eddf09` |
| `src/core/view/FPSCounter.cpp` | `3764726b451c73fd412dfbb055f6254c1eac5654` | `416e760ce2f82086f187cd23a1122a799490c463243697e18d9d6554102d0799` |
| `LICENSE.md` | `0ae59dae71d0f61caa31d7465d2472c0b3186df4` | `945a678080bfa6f946e2e424529c5372bbad144fdbf235aa6b76489f5032c6ac` |

## License boundary

Copyright (C) 2020, Inria, GRAPHDECO research group; the selected root license
carries the 2024 Inria / Universite Cote d'Azur notice. The original file
headers refer to the repository's LICENSE.md and are retained, including
their earlier research/evaluation wording. The rights holders' January 2024
[license change](https://gitlab.inria.fr/sibr/sibr_core/-/commit/83efea2715ce593872a97395b158928883d50675)
replaced the research license for SIBR Core with Apache-2.0, except for
separately licensed projects in `src/projects`. Neither copied file is in
that exception or has a separate license. No upstream NOTICE or nested
license file is present in the selected tree. The accompanying `LICENSE.md`
is the exact selected upstream license; distribute it and the attribution
and modification notices with source and binary copies. This does not
apply Apache-2.0 to the older 0.9.6 tag or to Gaussian-Splatting training
code or its bundled SIBR snapshot.

## Adaptation

- Retained the 60-slot ring state and the four upstream external-timing
  `FPSCounter::update(float)` statements (subtract old interval, add new
  interval, replace slot, advance modulo the window). Only the capacity
  name and type/formatting differ in those statements.
- Extracted the mean interval and reciprocal from `render()`. Fixed-size
  storage removes allocation, double precision retains GSW's interval
  precision, and startup divides by the count of valid intervals rather
  than zero-filling the denominator. A full window has the upstream
  behavior within floating-point precision.
- Removed the internal clock overload, ImGui, resources, position and
  panel flags. Qt supplies the visibility switch and presentation states.
  `gsw::FrameRateCounter` is only a steady-clock anchor and unit adapter:
  `NativeViewport::frameSwapped` -> `frameCompleted` -> this `update`.
  The two implementations are not maintained in parallel.
