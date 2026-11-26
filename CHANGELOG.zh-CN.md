<div lang="zh-CN">

# 更新日志

[English](CHANGELOG.md) · [简体中文](CHANGELOG.zh-CN.md) · [日本語](CHANGELOG.ja.md)

按时间倒序记录 Gaussian Splatting Workbench 面向用户的变化。本次补记覆盖
2026-10-23 至 30 日，是经过整理的摘要，不声称还原全部更早历史。日期采用日本时间。
[完整源码提交记录](https://github.com/fubukisama/GaussianSplattingWorkbench/commits/main/)。

**状态说明：** **已实现**表示链接对应的源码修改已存在，不等于完成全部验证、
公开发布 Release，或已经安装到某台电脑；后三者分别需要对应版本的证据。
公开的 Windows 预览版见[下载指南](README_RELEASE.md)，托管安装见
[更新／回退指南](docs/NATIVE_UPDATES.md)。
CI 附件和本地预览版交付不会自动成为公开 Release。

以后新增、修改或修复面向用户的功能，必须在同一变更中同步更新三语日志，写明影响能力、
限制及验证或交付状态。“已实现、已验证、已发布、已安装”分别记录，不相互替代。

## 2026-11-26

- **新增：相机标签与标定照片比较。** 可在相机牌显示照片名称和引线；拥挤时优先显示
  选中项。也可从标定相机位姿叠加核实过的照片，选择模型前方／后方、不透明度及锁定
  当前照片。关闭叠加可返回原视图。3DGS、2DGS 和有明确归属的训练预览共用此功能，
  不修改模型或训练。
- **照片兼容性：**仅比较能核对针孔标定的图像。注册／去畸变图与「查看原图」
  分开管理，无法核实或不匹配时不会拿其他照片替代。

## 2026-11-25

- **修复：列表和表格的隔行底色。** 媒体导入列表和任务记录的普通行在深浅主题下
  使用统一底色。选中、悬停、键盘焦点和任务状态的提示保持不变。[源码](https://github.com/fubukisama/GaussianSplattingWorkbench/commit/fecf2b5e697c42dccadc59d5fee0194efbd87349)

- **修复：Python 3.8 下无法查看原图。** 3DGS／2DGS 共用的照片核对兼容现有训练环境，
  无需升级 Python 或重新训练。原图与路径仍按原有规则核验。

- **修复：滚动条贴住表单控件。** 原生表单、列表、表格和面板在内容与横向／纵向
  滚动条之间统一留空，100% 界面缩放下为 8 个逻辑像素，随缩放调整。滑块原有
  宽度与操作保留，留白不占用按钮。Windows 验收与本地交付另行记录。[源码](https://github.com/fubukisama/GaussianSplattingWorkbench/commit/5e56686832be65ae1f655985640c678e77473179)

## 2026-11-24

- **修复：任务预览可查看原图。** 3DGS／2DGS 在训练中，以及暂停、取消后保留的预览，都可从该任务自己的数据集查看对应原图。不能核实原图时不会用同名训练图代替，也不用重训或改动照片。

- **修复：工程栏选择。** 卸载最后一个模型后，单击某一行不会误选整棵工程树；点击空白可取消选择，Ctrl+A 只选模型行。原始模型文件不变。[源码](https://github.com/fubukisama/GaussianSplattingWorkbench/commit/516af6cee79879f61b91b3f422453636bbdd2df2)

## 2026-11-23

- **相机牌与实拍照片。** 视口用小型蓝色相机牌显示位置和朝向。实拍缩略图、
  完整视锥和按记录顺序连线可分别切换；可从相机牌查看对应照片，也可从照片
  定位相机。仅从绑定数据集读取可核实的图像，缺失、变化或无可靠关联时保留
  蓝色相机牌。相机标定、模型变换与训练不变；编译、运行验收、发布和安装分别记录。

- **Windows 托管预览：**新增独立启动包，首次在空目录中从固定启动器启动；开启自动下载后，正常退出时应用已校验的新版。经典向导版只检查预览更新，仍从官方发布页手动运行 Setup，不会在退出时自动启动下载来的安装程序。正式签名通道不变。[更新说明](docs/NATIVE_UPDATES.md#github-preview)

- **修复：** 独立 COLMAP 重建及 3DGS／2DGS 的稀疏准备阶段中，迟到的旧快照不再替换较新的点云预览。新快照允许正常减少点数，验收后的最终点云优先显示。不改变重建和训练输出。
  [修复源码](https://github.com/fubukisama/GaussianSplattingWorkbench/commit/4d5d8196160bdd7c54b0efdd0f7678e25c8e306e)

## 2026-11-20

- **网格设置与视口详情：**勾号、下拉箭头和数值增减箭头改用矢量图标，保留原控件与键盘操作。
  视口详情使用当前语言的界面正文字体，原始任务日志仍为等宽字体。不改变模型坐标或网格数值。
  [源码](https://github.com/fubukisama/GaussianSplattingWorkbench/commit/b444467292723a0fe45e7692502ede69cd4c1cc7)。远端验收和桌面交付另行记录。

- **Windows 预览版更新：**向导安装和未接入正式信任源的托管安装均可检查本项目不可变的 GitHub 发布。开启自动下载后，已校验的新版在正常退出时安装；取消退出、包损坏或被 Windows 策略阻止时保留旧版。不要求商业代码签名证书，正式签名通道不变。[更新说明](docs/NATIVE_UPDATES.md#github-preview)

- **稀疏重建预览：**关联数据集时即显示场景工具栏，首张点云快照不会把视口和属性栏向下挤；尚不可用的编辑操作仍禁用。连续稀疏快照沿用第一张有效点云的网格基准，点云真实范围照常更新；仍可导航和切换到世界零平面。本条为源码变更，不代表已发布或安装。
  [修复源码](https://github.com/fubukisama/GaussianSplattingWorkbench/commit/6de4af73b99a8132f31eb333bd8db5e54bd0478c) · [回归诊断源码](https://github.com/fubukisama/GaussianSplattingWorkbench/commit/fbecef12c4cf958a2660c1bf6cc9bd9ff8a918b6)

- **新增（已实现；远端验收待完成）：**3DGS／2DGS 训练和适用网格／深度／贴图流程
  共用只读重建质检。分开核对原始输入、所查分组注册图和已验证训练清单，可筛未注册
  照片、查看原图及切换可核验分组。有有效位姿时只在独立稀疏预览中定位并可返回，
  不跳到无来源关系的主模型。缺失、歧义、过期来源明确提示；不启动生成、不写工程或
  原数据，不改准入门槛、预设、续训及规范相机网格限制。含三语即时切换及有界异步读取。
  逐图误差、未支持的畸变相机定位仍不可用。见[范围与验收](docs/RECONSTRUCTION_QUALITY.md#read-only-image-review-2026-11-20)。
  此变更未公开发布，也未替换本机软件。
  后续保护修正空目录的取消传播，歧义身份不冒报未注册，重查预览根目录链接，并将按需
  解码限制为单图可取消队列；仍需对应源码的远端重验。
  质检相机快捷入口不能以第一个相机代替所选行；异步定位保留点击时的视角。窗口回归
  同时核对原图真实像素、150% 窄窗口和过期上下文隔离。
  独立预览明确标注显示中的稀疏点子集、相机数和只读状态。
  修正真实窗口筛选失败：筛选项和分组项显式保存字符串身份，不依赖有歧义的 Qt 重载。
  关闭或隐藏质检后不再触发延迟初检或轮询；验收夹具使用显式构建临时目录。
  相机回归按 COLMAP 世界到相机约定计算期望，并记录中心、朝向、上方向数值；0.0001 容差不变。
  锁定相机的 SfM 准入标为不适用；旧模型有评估划分或自定义图像目录而缺少可信训练清单时，不推断训练成员。
  [筛选修正源码](https://github.com/fubukisama/GaussianSplattingWorkbench/commit/68bc3b7a263e6f260f454eb0c1d2a5dc8e7bf0dc)。
  [逐行导航保护](https://github.com/fubukisama/GaussianSplattingWorkbench/commit/ae81c54508b7d17f1ae86831c72428805b95df65)。
  [实现源码](https://github.com/fubukisama/GaussianSplattingWorkbench/commit/87116348a54d719f2556cbddbd1913aa759313b6)。
  [只读保护与所选网格来源修正](https://github.com/fubukisama/GaussianSplattingWorkbench/commit/ca6dd21c200c8395bc81b1065845b3ad39f00ebf)。

## 2026-11-18

- **发布：**[11 月 18 日原生预览版](https://github.com/fubukisama/GaussianSplattingWorkbench/releases/tag/v0.3.9-native-preview.20261118)
  的 Windows 安装包包含实时训练观察和可选的 LichtFeld 投影。[11 月 17 日版](https://github.com/fubukisama/GaussianSplattingWorkbench/releases/tag/v0.3.9-native-preview)
  仍可下载。两版都只提供手动下载，不开放公共自动更新。
  [源码](https://github.com/fubukisama/GaussianSplattingWorkbench/commit/390847cdc1238db7a1025eb417672e9c4dac95bf)

- **修复：**Windows 原生包补齐了 3DGS／2DGS 实时训练预览所需的后端模块。
  构建前新增来自实际打包目录的独立导入检查，原有安装包验收继续执行。
  [源码](https://github.com/fubukisama/GaussianSplattingWorkbench/commit/9c6b00ce898c97761359c48e52a1cb436cfddfb3)

- **修复：**Windows 上重连运行中的批任务，或向它发出暂停、取消请求时，若读取批任务租约
  遇到短暂的文件共享冲突，会有限重读并重新校验进程与会话身份。租约仍不可读或已关闭时，
  不发出命令。原有超时不变；完整 CI 和交付另行记录。
  [源码](https://github.com/fubukisama/GaussianSplattingWorkbench/commit/3e230eff66db5d535d1a95da5128e82767f2afde)

- **发布：**[0.3.9 Windows x64 原生预览版](https://github.com/fubukisama/GaussianSplattingWorkbench/releases/tag/v0.3.9-native-preview)
  提供 Setup 手动下载和 SHA-256 校验文件。这是通过验收的 11 月 17 日构建，不含下方较新的
  训练预览改动。安装包未签名，未开放公共自动更新通道。
  [源码](https://github.com/fubukisama/GaussianSplattingWorkbench/commit/726f32a42ea807e2df32255ce235706e23f4e139)

- **训练预览：**3DGS／2DGS 实时观察绑定任务、输入数据和训练坐标。原生视口校验
  快照哈希，恢复观察时接最新帧；2DGS 保留双尺度。独立 GPU 样本验证了自由导航、
  模型更新、实际增密／删点和完整状态恢复。不代表完整 LS 渲染器、CUDA／OpenGL
  共享缓冲或发布验收通过。
  [源码](https://github.com/fubukisama/GaussianSplattingWorkbench/commit/645872411c0ec7369f9c6c0990665c7fc7730208)

- **实验功能：**[LichtFeld 训练预览投影](docs/LICHTFELD_VISUALIZATION.md)
  为兼容的 3DGS 训练观察提供可选的移植投影与覆盖范围限制。默认关闭，退出不保留。
  不修改训练、原模型或导出；2DGS 和 Full Mip-Splatting 保留原生路线。这是第一段源码
  移植，尚不是完整 LS 渲染器或已通过 GPU 实训验收的安装版。
  [源码](https://github.com/fubukisama/GaussianSplattingWorkbench/commit/c0560be52a28187867c9cbbb1602c150ff6e70da)

## 2026-11-17

- **更改：**[原生软件更新](docs/NATIVE_UPDATES.md)在启动约 10 秒后静默检查，
  此后每 4 小时检查。开启自动下载后，运行期间只下载和验签，正常退出才安装。
  不强制重启，不改训练任务的退出保护；固定启动器和签名校验不变。
  本次变更不启用公开更新源。[源码](https://github.com/fubukisama/GaussianSplattingWorkbench/commit/726f32a42ea807e2df32255ce235706e23f4e139)

## 2026-11-16

- **已实现：**[受管版首次安装](docs/native_auto_update/FIRST_INSTALL.md)先核验引导包，
  再通过现有签名更新流程安装到新目录。拒绝已有目录，不迁移旧版。本次源码变更
  不发布公开安装器，也不启用生产更新源。[源码](https://github.com/fubukisama/GaussianSplattingWorkbench/commit/499e9ae963eb586612ca70af247d81e1e560681e)

- **首装下载：**复用已验证的元数据 HTTPS 客户端，首次安装不再依赖 Schannel 的隐式外发凭据。
  保留签名安装包校验，已有安装的更新路径不变。
  [源码](https://github.com/fubukisama/GaussianSplattingWorkbench/commit/5d52c290eb3536ace94ebeb1ef5b944b9707a048)

## 2026-11-13

- **修复：**开启「视图 → 显示帧率」后，点云、网格、3DGS 和 2DGS 视口停止操作也继续绘制，
  显示实测 FPS；关闭后恢复按需绘制。隐藏、最小化或暂停预览时不持续刷新，训练预览的刷新
  限制不变。[源码](https://github.com/fubukisama/GaussianSplattingWorkbench/commit/3f146c26bcbf7864bc02e0605789ee3d849d5239)。已实现，远端验收与安装包校验另行记录。

状态：**已实现**，远端验收另行记录；本次不公开发布或更新本机安装。

- **改进：**原生视口直接采用 SIBR Core 的 60 帧间隔计数核心，活动时显示
  实时帧率与平均呈现帧间隔，例如 `125.00 FPS (8.00 ms)`。窄视口优先
  显示 FPS，详情保留间隔。点云、网格、3DGS 与 2DGS 预览共用统计。
  间隔不是 GPU 执行时间、屏幕刷新率或训练速度。
- **新增：**「视图 → 显示帧率」默认开启并记住选择，只控制显示。保留空闲、
  等待及预览暂停的状态，不启动持续重绘。
- **兼容性：**不更换渲染器，不修改训练、工程格式或更新器。固定版本的
  Apache-2.0 SIBR 源码及声明随对应源码和二进制分发。[源码](https://github.com/fubukisama/GaussianSplattingWorkbench/commit/050672c6884ccf74502bb9266546be2b501fa9cf)。

- **修复：**150% 界面缩放下，视口状态区不再给不在同一行的导航控件预留宽度。
  窄视口保留完整 FPS；放不下时只留详情按钮，平均帧间隔仍可查阅。Windows
  构建探针也已补齐 SIBR 头文件依赖。[源码](https://github.com/fubukisama/GaussianSplattingWorkbench/commit/92ac8101cf178ba913c9bfe92f40f0fe447b1121)。

## 2026-10-30

状态：**已实现**；此节是源码记录，不是发布或安装回执。

- **调整：**恢复按日期整理的中、英、日更新日志及首页入口；将持续维护日志列为项目共用
  规则，并同步到 Codex 项目分叉聊天。文档校验同时检查三语历史、日期、源码引用和首页入口。
- **修复：**任务栏重新启动信息从正在运行的程序资源读取显示品牌，同时保留固定启动器
  作为启动目标。[源码](https://github.com/fubukisama/GaussianSplattingWorkbench/commit/e1ec20a69f7a44523bc747fbbb71000984864684)。
- **改进：**自动外观、空闲时的暂停和不可用的继续菜单项，通过可访问性接口提供原因说明，
  避免只显示无法使用的操作。[外观说明](https://github.com/fubukisama/GaussianSplattingWorkbench/commit/aa09ab26ba637387f3b1049dc11492916cce2b11) · [暂停／继续说明](https://github.com/fubukisama/GaussianSplattingWorkbench/commit/d87343fa215389815c6cf037aef85153a5bf6b91)。

## 2026-10-29

状态：**已实现**；验证和交付仍以单独记录的对应源码版本为准。

- **调整：**产品显示名称改为 **Gaussian Splatting Workbench**，保留原有程序文件名、设置键
  和应用标识的兼容性。[源码](https://github.com/fubukisama/GaussianSplattingWorkbench/commit/b0aacf85bb5e87b82b6bfcde0ce0a6ba5728b93b)。
- **改进：**初始化和批处理表单在受限窗口内更一致地适配；补充字段可访问性和任务状态说明。
  [表单布局](https://github.com/fubukisama/GaussianSplattingWorkbench/commit/c170af833aa8b35bed560da0a403d1160b4657c0) · [字段与任务呈现](https://github.com/fubukisama/GaussianSplattingWorkbench/commit/c3e667a249e0de1b4384aa3d97d29172e08a474b)。
- **新增：**独立恢复入口及显式重新验证的恢复选项；下载开始前检查可用空间。
  这些更新组件不代表已开放公共签名更新通道，也不证明完整首次安装／更新流程已验收。
  [恢复](https://github.com/fubukisama/GaussianSplattingWorkbench/commit/3ed5a4857ded622e9c188847494bc4d2c59ed650) · [空间预检](https://github.com/fubukisama/GaussianSplattingWorkbench/commit/9db10d37bb0e9d2ce60d38d91c573dab530a6d2e)。
- **文档：**加入真实 3DGS／2DGS 训练阶段与原生界面操作视频，明确说明使用合成输入和
  已保存阶段回放；演示不作为重建画质、精度或性能基准。[源码](https://github.com/fubukisama/GaussianSplattingWorkbench/commit/7222c1203ebdc791d89d02768baf81cf27275915)。

## 2026-10-28

状态：**已实现**；隔离组件检查不等于整套软件或公共更新通道验收。

- **改进：**初始化内容可在受限窗口内滚动，保留底部操作按钮可达。
  [源码](https://github.com/fubukisama/GaussianSplattingWorkbench/commit/fa1c39fe2fcf5cacd3bb39e266a2b0bc10dc9b7b)。
- **新增：**默认静默的**帮助 → 软件更新**界面，支持三语即时切换。联网仍需兼容的可信引导
  组件和已配置的签名更新源，不表示已有公开服务。[更新界面](https://github.com/fubukisama/GaussianSplattingWorkbench/commit/55064f8c5c5b26d99f6feab1d063423e20af1e7e) · [主程序接入](https://github.com/fubukisama/GaussianSplattingWorkbench/commit/ce3e4e044647dfd59a9508674f77c2ed5db74320)。
- **安全：**加强完整压缩包／源码验证、长 Unicode 更新路径、离线启动检查和显式回退验证。
  这些保护不构成生产签名或公共更新源批准。[路径与回退](https://github.com/fubukisama/GaussianSplattingWorkbench/commit/fa1c39fe2fcf5cacd3bb39e266a2b0bc10dc9b7b) · [离线启动与源码证明](https://github.com/fubukisama/GaussianSplattingWorkbench/commit/ce3e4e044647dfd59a9508674f77c2ed5db74320)。

## 2026-10-27

状态：**已实现**；存在源码提交不代表已有桌面安装完成更新。

- **新增：**随工程保存的自动／固定网格间距、网格／坐标轴可见性、显示单位和只读视口详情。
  用空闲／等待状态替代过期 FPS；显示设置不改变模型尺度，也不代表重建精度。
  [源码](https://github.com/fubukisama/GaussianSplattingWorkbench/commit/d9e1d361fa5daee23617852bcacf11cd64156d50)。
- **修复：**隐藏最后一个可见模型时保留画布布局和参考平面，避免改变视口尺寸或重置参考基础。
  [画布布局](https://github.com/fubukisama/GaussianSplattingWorkbench/commit/6ea81a50c0f8641c03056d9f5fa0922096ec8d70) · [参考范围](https://github.com/fubukisama/GaussianSplattingWorkbench/commit/17cef1e1222b07c7e4109de59deab5a3e4294d7d)。
- **新增：**固定原生启动器和经校验的版本目录原子发布；后续修复快捷方式选项保留与活动版本
  指针替换。正在运行和用于回退的版本，与新准备的版本目录相互独立。
  [启动器与发布](https://github.com/fubukisama/GaussianSplattingWorkbench/commit/8653293e4ce7209ce9396cc597a89e199ead4da4) · [快捷方式保留](https://github.com/fubukisama/GaussianSplattingWorkbench/commit/32f8e8d904ea462d5ace2717211170fb54466045) · [原子替换](https://github.com/fubukisama/GaussianSplattingWorkbench/commit/87a797bd2fe5072df1a72e5587414841f62ee494)。
- **修复：**worker 原子发布状态文件时遇到 Windows 短暂文件共享冲突，采用有界重试，
  不再立即失败。[源码](https://github.com/fubukisama/GaussianSplattingWorkbench/commit/b87448ece4bb5664b713881a00ebd553bebb085e)。

## 2026-10-26

状态：**已实现**；后台渲染检查不替代录屏所示前台异常的实际复测。

- **修复：**静态高斯对象拾取改用只读路径；混合模型选择、叠加层独立处理并恢复 OpenGL
  状态和像素读回，选择操作不应修改模型数据或其他模型的渲染状态。
  [高斯拾取](https://github.com/fubukisama/GaussianSplattingWorkbench/commit/c8901e2ff70845d23f97a724da34afe8d6552bb6) · [混合模型状态隔离](https://github.com/fubukisama/GaussianSplattingWorkbench/commit/14be3f9aba02d166a5f25db817f67887c5c6cb0f)。
- **修复：**翻译生成统一由共享资源目标负责，避免多个使用方在语言切换时并行读取未完成的目录。
  [源码](https://github.com/fubukisama/GaussianSplattingWorkbench/commit/d8df7867e475e4f271eb9c1a505895c6c47b8d90)。

## 2026-10-25

状态：**已实现**；不据此承诺通用加速比例或内存硬上限。

- **改进：**隐藏模型保留坐标身份，视口重绘和资源调度按需进行；隐藏数据推迟不必要的 GPU
  资源申请。[按需调度](https://github.com/fubukisama/GaussianSplattingWorkbench/commit/253b35fec2ebea58822066ce2b280129745f8207) · [隐藏数据资源](https://github.com/fubukisama/GaussianSplattingWorkbench/commit/68186326266ef9cd6abc7a3a0294e4599cc630e9)。
- **修复：**磁盘分页数据导出时保留原始数据来源；包启动检查隔离旧运行残留，不接受过期文件。
  [源码](https://github.com/fubukisama/GaussianSplattingWorkbench/commit/bd521628558d62612c8d077df0bd6a655fd70f03)。

## 2026-10-24

状态：**已实现**；这些查看控件不会自动配准或合并独立重建。

- **新增：**工程树逐模型眼睛按钮，显示／隐藏状态随工程保存；名称选择与可见性更新保持分离。
  [可见性控件](https://github.com/fubukisama/GaussianSplattingWorkbench/commit/b7c91307813c46b9258d48e2fdb3c9c365cee3a0) · [保留选择](https://github.com/fubukisama/GaussianSplattingWorkbench/commit/3552eb20374f321cf19a60dbb352681d05761597)。
- **调整：**采用 Blender 衍生轴向导航：点击轴进入对应正交视图，从轴向视图自由旋转时恢复
  透视；平移和缩放保持当前投影，移除中心投影切换控件。
  [源码](https://github.com/fubukisama/GaussianSplattingWorkbench/commit/4627ae3db353f027dbb01e695d0563f3e8ac6ea5)。

## 2026-10-23

状态：**已实现**；实验性方法执行不等于最终画质或独立几何精度验收。

- **新增：**完整源数据的 3DGS／2DGS PLY 变换烘焙，保留属性保真度。不支持的非均匀变换
  与方向球谐组合明确拒绝，不静默近似；双尺度 2DGS 不添加虚构厚度。
  [源码](https://github.com/fubukisama/GaussianSplattingWorkbench/commit/0a7116bba76ee83ad53b0567df91e60e5f079dba)。
- **新增：**默认关闭的实验性质量计划及 Mip 感知交付。3DGS／2DGS 共享工作流身份和遮罩／深度监督，
  但不会把体积方法直接当作等效的面元算法；MCMC 仍需独立验收，在桌面选择器中不可用。
  [源码](https://github.com/fubukisama/GaussianSplattingWorkbench/commit/bc09c7dbe3b103c7b54617b123bd5a89ee1d4120)。
- **修复：**处理等价 Windows 结果路径和持久元数据共享，避免已记录的成果发布／暂停失败。
  [源码](https://github.com/fubukisama/GaussianSplattingWorkbench/commit/0bbea50994a0dea6431672977887d6fc5b9bc470)。

</div>
