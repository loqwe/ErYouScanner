# v2.0.9 扫码解码耗时修复

口径：**二维码显示 → 程序解码锁定**。不含模型加载（用户已自行预热）、
不含 `confirm_delay_seconds`、不含 passport 登录 HTTP。

原始实测数据见 [扫码延迟取证](DIAG_scan_latency.md)。

## 根因

`src/Core/QRScanner.cpp` 构造时调用：

```cpp
detector->setScaleFactor(1.0);
```

OpenCV 的 `WeChatQRCode` 文档（`opencv2/wechat_qrcode.hpp`）写明：

> By default, the input image is scaled to an image with an area of **160000 pixels**.

即不调用时，检测网络只吃 16 万像素。传 `1.0` 表示**按原尺寸送入**，
而应用两条扫码路径都把帧归一化到 1280×720 = **921600 像素**，是库默认面积的 **5.76 倍**。
成本结构实测为：固定的 3.5~4 ms（候选框的 SR + 译码）＋ 约 0.09 µs/px × 网络输入面积。

## 现场证据

同一张真实 1920×1080 截图（花背景，鸣潮登录码），同一码文，各 20 次取中位：

| `setScaleFactor` | 网络输入面积 | 解码中位 | 解出的码文 |
|---|---|---|---|
| **1.00（改前）** | 1280×720 = 921600 px | **87.9 ms** | `G152#KURO_038a5d17-…` |
| 0.71 | 909×511 = 464000 px | 16.3 ms | 一致 |
| 0.50 | 640×360 = 230400 px | 9.4 ms | 一致 |
| **0.42（改后）** | 538×302 = 162500 px | **7.3 ms** | 一致 |
| 0.35 | 448×252 = 112900 px | 6.3 ms | 一致 |
| 0.25 | 320×180 = 57600 px | 4.6 ms | 一致 |
| 不调用（库默认） | 目标 160000 px | 7.1 ms | 一致 |

七个档位的码文**逐字一致**，说明缩放因子只影响速度、不影响结果。

### 被否决的备选

| 方案 | 实测总耗时 | 结论 |
|---|---|---|
| ROI 裁剪 + `cv::QRCodeDetector` 定位 @1/1 | 18.6 + 3.3 = 21.9 ms | 慢于调 sf，否决 |
| ROI 裁剪 + 定位 @1/2 | 6.0 + 3.5 = 9.5 ms | 仍慢于 7.3 ms，否决 |
| ROI 裁剪 + 定位 @1/4 | 定位失效 | 不可用 |

成本几乎全在检测网络的输入面积上，`setScaleFactor` 直接、无副作用地动了这个量，
因此不需要引入 ROI 定位这一步。

## 检测能力下限（确认无损失）

扫描帧固定归一化到 1280×720，所以决定成败的是**码在帧内占多少像素**。
合成码（花背景）在不同帧内尺寸下的结果，各 10 次：

| 帧内码尺寸 | sf=1.00 | sf=0.50 | sf=0.42 | sf=0.35 | sf=0.25 |
|---|---|---|---|---|---|
| 154 px（真实码量级，实测 149 px） | OK | OK | OK | OK | OK |
| 109 px | OK | OK | OK | OK | OK |
| 87 px | OK | OK | OK | OK | OK |
| 66 px | OK | OK | OK | OK | **MISS** |
| 44 px | OK | OK | OK | OK | **MISS** |
| ≤45 px | **MISS** | MISS | MISS | MISS | MISS |

- 44 px 是整条链路的物理下限，`sf=1.0` 也解不出。
- `sf ≥ 0.35` 时下限不变，**检测能力没有任何损失**；`sf=0.25` 开始丢码，不用。
- 真实登录码 149 px，`sf=0.42` 仍有 **3.4 倍**余量。

## 修复

`src/Core/QRScanner.cpp`：

```diff
-    detector->setScaleFactor(1.0);
+    detector->setScaleFactor(0.42f);
```

注释已写明原因、库默认面积换算与 44/149 px 两个下限依据。

影响面：`QRScanner` 由两路共用 —— 屏幕扫描（`QRCodeForScreen`，1 条解码线程）
与直播流扫描（`QRCodeForStream`，3 条线程、`thread_local QRScanner`）。
直播流在 `setStreamHW()` 里对主流 1920×1080 源同样落到 1280×720，帧面积一致，
收益与风险同构。

## 验证

`source/tests/scan_latency/verify_shipped_scanner.cpp` —— 测的是**应用本体的 `QRScanner`**，
不是基准里自建的 detector。内置 `expected` 码文常量（改前 sf=1.0 解出的原文），
码文不符或中位 > 20 ms 即返回非零退出码：

```
shipped QRScanner decodeSingle @1280x720
  min=6.3304ms  median=7.7841ms  max=12.0715ms  (n=20)
  payload=G152#KURO_038a5d17-f3f6-4474-ba86-dba3aac105d4
PASS
```

主程序通过 `tools/package.ps1` 完成干净构建与打包（见 `build-info.json` / `release-info.json`）。

## 整链延迟对照

循环为 `getFrame(100ms 超时) → copyFrameToBuffer → resize 1280×720 (0.37 ms) →
pool.tryStart(解码) → sleep(scan_interval_ms)`，屏幕路径只有 1 条解码线程。
延迟 = 等到第一个含码的采样帧（0~循环周期，均匀分布）＋ 解码耗时。

| 配置 | 平均 | 最坏 |
|---|---|---|
| 改前 sf=1.0，间隔 100 ms | 140 ms | 193 ms |
| 改后 sf=0.42，间隔 100 ms | 59 ms | 111 ms |
| 改后 sf=0.42，间隔 30 ms | **≈23 ms** | **≈40 ms** |

`scan_interval_ms` 无需改代码，界面"扫描间隔"输入框范围本就是 10~5000 ms。

## 历史遗留缺陷清理（零行为变更）

| 缺陷 | 处理 |
|---|---|
| 预览窗口开关语义是反的 | `CMakeLists.txt` 在**非 DEV** 构建定义 `SHOW`，代码却用 `#ifndef SHOW` 包住预览，读起来与行为相反。改为正向宏 `ERYOU_SCAN_PREVIEW`（仅 `DEV` 构建定义），`QRCodeForScreen.cpp` 与 `QRCodeForStream.cpp` 共 6 处站点同步改 |
| 死宏 | `-DTESTSPEED` 全仓只有定义、无任何引用，已删 |
| 截图名写死版本 | `tests/app_ui/test_app_ui.cpp` 用 `-v2.0.2`，同文件另外 6 处用 `MHY_Scanner_VERSION`，已统一 |
| 冒烟测试写死版本 | `tests/smoke_startup.py` 断言 `version=1.16.4`，而 `src/UI/main.cpp:39` 打的是 `MHY_Scanner_VERSION`，**自 2.0.x 起一直对不上**；改为 `expected_version()` 从 CMakeLists 读取 |
| 界面测试版本陈旧 | `tests/confirmation_delay/test_confirmation_ui.cpp` 停在 `2.0.7`，已同步到当前版本 |
| 缺少防复发的守护 | `tests/test_release_version.py` 增加：冒烟测试必须读取版本、测试内截图名不得写死版本号 |

### 预览开关的等价性验证

重构必须**逐字不变**，因此用二进制实际取证，而不是只看逻辑：

| 符号 | RELEASE 构建 | DEV 构建（`-DDEV=ON`） |
|---|---|---|
| `imshow` | 不存在 | 存在 |
| `namedWindow` | 不存在 | 存在 |
| `waitKey` | 不存在 | 存在 |
| `destroyWindow` | 不存在 | 存在 |
| `Video_Stream`（窗口标题） | 不存在 | 存在 |

改动前先对 v2.0.9 发布版 EXE 做了同样的探测，四项全部不存在，
即重构后的发布版与基线一致；DEV 版仍能正常打开预览。

## 未决

- 尺寸下限矩阵用的是合成码，真实样本目前只有一张截图（帧内实测 149 px）。
  建议补充 2~3 张**不同游戏窗口大小 / DPI 缩放**的真实登录码，确认 149 px 是下限而非上限。
- 若出现帧内 60 px 以下的场景，应保留 `sf=0.42` 快通路，另加低频（如每 500 ms 一次）
  `sf=1.0` 兜底复检；兜底频率必须受限，否则池线程被占满会导致 `tryStart` 丢帧。
- `src/UI/WindowMain.cpp` 的 `//FIXME 没有及时更新`（启动任务体首行）**未处理**：
  用 git 追溯不到意图（该行来自导入的边界提交 `d07fc80`，提交信息与注释无关），
  存在三种解读（账号快照是否陈旧、按钮文案、账号状态列刷新），需要明确后再改，不做猜测。
- `src/Core/LiveStreamLink.cpp:169` 的 `//TODO platform 会影响下载时使用的 referer`
  位于 `#if 0` 死块内，是在说明一个已禁用的参数变体（`"platform": "h5"`），不是活缺陷，保留。
- 仓库状态：`HEAD` 仍在 `0c7d7b9 Release v1.16.4`，整个 2.0.x 线（**66 个文件**）
  都在工作区未提交。发行包由工作区构建，提交与否由使用者决定，本次未 commit / push。
