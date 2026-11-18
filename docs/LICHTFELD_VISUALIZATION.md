# LichtFeld training visualization: first source-port slice

## Decision and interface

The first slice ports three actual shader methods from LichtFeld Studio
`3152c3f4709a92873ceb5a674ff937817b1f9a4e` into the native task viewport: the
asymmetric pinhole Jacobian, the affine orthographic Jacobian, and the
opacity-aware screen covariance extent limit. It is a **projection adapter**,
not the LS Vulkan renderer. [Per-file provenance and licensing](../native/third_party/lichtfeld-visualization/PORT_LEDGER.json).

The seam is `NativeViewport::drawGaussianCloud`'s program selection, after its
existing read-only preview admission. The adapter projects already transformed
eye-space Gaussian covariance. GSW owns the pose, projection, source units,
opacities, dimensions, buffer lease, frame selection and compositor. The new
shader does not read datasets or change the preview transport. It uses physical
viewport pixels, the OpenGL bottom-left principal point and negative forward Z.
Centre projection is left to the original clip-space transform. The affine
orthographic derivative has no depth multiplier. A camera matrix with skew
or an unsupported projective row does not enter this adapter. GSW's existing
filter, footprint floor/quad, clip checks, SH evaluation, sort/blend/depth
policy and original image background stay in use; this is not an LS image
parity, exact-depth, SH-transport or throughput claim.

**View → LichtFeld Training Preview Projection (Experimental)** is session-only
and defaults to off. It may be requested before training. Only a visible active
3DGS task in Gaussian mode can select the adapter; switching it off, showing the
reference, pausing the picture or ending the task leaves or returns to the
existing route. The experimental shader links lazily on the viewport's current
OpenGL context; its failure is independent of native program readiness and is
shown as native fallback. The new program is retired on context destruction and
is independently built on the next context. No shader compilation occurs in the
trainer. Owner-context loss, device lost and real GPU behavior remain separate
acceptance items.

| Input or view | Experimental route in this slice | Unchanged route / limit |
|---|---|---|
| 3DGS live file observation | Ported projection for supported cameras | Existing periodic producer and indexed sort; preview remains sampled/basic-colour where producer provides only DC |
| 3DGS live CUDA/OpenGL shared model | Same shader selection after an actual compatible v2 frame is consumed | Existing owner, stride, fences, rate and DC-only payload; no claim of new CUDA sharing or full SH/sorting |
| 2DGS live observation | Not a reframing into 3DGS | Existing two-scale perspective-correct surfel rendering and snapshot transport |
| Full Mip-Splatting | Declined deliberately | Native validated fused model + sidecar and original filter |
| Point clouds, meshes, saved/reference models | Not changed | Existing pipeline, depth/compositing and editing |

Non-goals for this slice: producer protocol/version and dataset/frame envelope,
independent frame-age timing, new buffer/synchronization policies, Vulkan bridge,
new trainer/optimizer, new unit conversion, reference model transforms, full
P0 verification/installation, and a default switch. Moving projection code
advances the port but does not complete any of those items.

## Verification and evidence boundaries

- `native_lichtfeld_footprint` uses the packaged GLSL in an actual GL context,
  checks a worked asymmetric camera, orthographic covariance, a bounded
  small-footprint control and an opacity-limited anisotropic footprint.
- `native_lichtfeld_preview` checks the real menu action and viewport pixels:
  a bounded red tail without moving the centre/camera, three-language immediate
  update, independently frozen preview, newest two-scale snapshot on unpause and
  unchanged reference. It retains raw frames when the smoke output directory is
  supplied. `native_training_isolation` and `native_processing_preview` retain
  the original integration checks.
- These bounded fixtures use synthetic observations on the remote Windows
  software OpenGL runner. They are not actual running optimizer tests. CUDA
  live model/iteration evidence, real simultaneous orbit and training, actual
  densification/pruning, full-state invariance/resume, device faults and package
  delivery remain `not_verified`. No public/managed update or local install is
  part of this first slice.

## 简体中文

“视图 → LichtFeld 训练预览投影（实验）”默认关闭，只在本次运行中生效。
第一段移植的是 LS 的非中心主点／正交投影与高斯覆盖范围限制，接在 GSW 的
现有训练观察帧上。预览会给出实际传输方式或回退原因。原工程、导出、相机、
数据单位与训练状态不修改。2DGS 继续使用原生双尺度表面元显示，Full Mip-Splatting
继续使用现有正确路线。不是全套 LS Vulkan 渲染器，也没有改变训练预览传输。
真实 GPU 持续训练、增密／剪枝、完整续训、性能与安装仍是独立未验证项。

## 日本語

「ビュー → LichtFeld 学習プレビュー投影（実験的）」は既定で無効であり、
現在の起動中だけ有効です。偏心主点・平行投影のヤコビアンとスプラットの
描画範囲制限を、既存の 3DGS 学習プレビューに接続します。プロジェクト、
書き出し、カメラ、単位、学習状態は変更しません。2DGS の二尺度サーフェルと
完全な Mip-Splatting は既存の描画経路を使います。LS の Vulkan 描画全体や
学習プレビューの転送方式を移植したものではありません。実 GPU 学習、
高密度化・枝刈り、完全状態からの再開、性能、インストールは別の未検証項目です。
