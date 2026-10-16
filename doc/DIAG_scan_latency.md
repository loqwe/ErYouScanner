# 扫码延迟取证：二维码出现 → 解码锁定

口径（按用户定义）：**二维码可见 → 解码出码文（锁定）**。
不含模型加载、不含 `confirm_delay_seconds`、不含 passport HTTP 登录。

测量环境：本机 Release 构建，`QRScanner` + 项目自带的 `ScanModel`（4 个 caffe 模型），
真实截图 `clipboard-2026-10-16T10-34-41-681Z-2e638978.jpg`（1920×1080，花背景，码文
`G152#KURO_038a5d17-f3f6-4474-ba86-dba3aac105d4`）。
基准程序：`tests/scan_latency/bench_scan_latency.cpp`、`tests/scan_latency/bench_scale_floor.cpp`。

---

## 1. 现行链路

`src/UI/QRCodeForScreen.cpp` → `monitorScreen()`，单线程循环 + `QThreadPool(threadNumber=1)`：

| 步骤 | 位置 | 实测 |
|---|---|---|
| `capture.getFrame(100)` 取新帧 | 第 98 行 | 有帧即返回，60Hz 下 ≤16.7ms |
| `cv::resize` → 1280×720 | 第 143 行 | **0.37 ms** |
| `pool.tryStart(...)` 非阻塞投递 | 第 156 行 | 池满即丢帧 |
| `scanner.decodeSingle(image)` | 第 164 行 | **87.9 ms**（中位） |
| `sleep_for(m_scanIntervalMs)` | 第 214 行 | 配置 `scan_interval_ms` = **100 ms** |

解析：
- 循环周期 ≈ 100（sleep）+ 取帧 + 0.37 ≈ **105 ms**
- 每次解 88ms < 105ms，池线程每次都能空出来 → 每个采样帧都解得到
- 因此延迟 = **等到第一个含码的采样帧**（0~105ms 均匀）+ **解码 88ms**

### 现行延迟

| | 值 |
|---|---|
| 最好（码出现在采样瞬间） | ≈ 88 ms |
| 平均 | 52.5 + 88 = **≈ 140 ms** |
| 最坏（码出现在采样之后） | 105 + 88 = **≈ 193 ms** |

> 已核实不是额外开销的地方：非 DEV 构建**不定义** `ERYOU_SCAN_PREVIEW`
> （`CMakeLists.txt`，仅 `DEV` 构建定义），所以 `QRCodeForScreen.cpp` 里受该宏保护的
> 预览块（`cv::imshow` / `waitKey`）**没有编进发布版**，循环线程没有预览开销。
> 已用二进制取证：发布版 EXE 中 `imshow`/`namedWindow`/`waitKey`/`destroyWindow`
> 与窗口标题 `Video_Stream` 全部不存在，DEV 版全部存在。

---

## 2. 根因：`setScaleFactor(1.0)` 把神经网络的输入放大到 5.76 倍

`src/Core/QRScanner.cpp` 第 10 行：

```cpp
detector->setScaleFactor(1.0);
```

OpenCV 文档（`opencv2/wechat_qrcode.hpp` 第 54-64 行）：
> By default, the input image is scaled to an image with an area of **160000 pixels**.

即 `scaleFactor = -1`（不调用）时网络只吃 160000 px；而 `1.0` 表示**按原尺寸**喂进去。
应用固定把桌面缩到 1280×720 = **921600 px**，是库默认值的 **5.76 倍**。

实测（真实截图，同一码文，同一帧，各 20 次）：

| scaleFactor | 网络输入面积 | 解码中位 | 载荷 |
|---|---|---|---|
| **1.00 ← 现状** | 1280×720 = 921600 px | **87.9 ms** | 一致 |
| 0.71 | 909×511 = 464000 px | 16.3 ms | 一致 |
| 0.50 | 640×360 = 230400 px | 9.4 ms | 一致 |
| 0.42 | 538×302 = 162500 px | **7.3 ms** | 一致 |
| 0.35 | 448×252 = 112900 px | 6.3 ms | 一致 |
| 0.25 | 320×180 = 57600 px | 4.6 ms | 一致 |
| 库默认（不调用） | 目标 160000 px | **7.1 ms** | 一致 |

成本 ≈ 固定的 3.5~4 ms（找到框后的 SR + 译码）+ 0.09 µs/px × 网络输入面积。

**只改这一行，真实游戏二维码的解码从 87.9ms 降到 7.3ms（12 倍），码文完全一致。**

### 顺带否掉的两个方案

| 方案 | 实测总耗时 | 结论 |
|---|---|---|
| ROI 裁剪 + `cv::QRCodeDetector` 定位 @1/1 | 18.6 + 3.3 = **21.9 ms** | 比改 scaleFactor 慢，否决 |
| ROI 裁剪 + 定位 @1/2 | 6.0 + 3.5 = **9.5 ms** | 仍慢于 7.3ms，否决 |
| ROI 裁剪 + 定位 @1/4 | 定位**找不到码** | 不可用 |

上一轮"ROI 裁剪是唯一手段"的假设**不成立**：成本几乎全在检测网络的输入面积上，
而 `setScaleFactor` 直接、无副作用地动了这个量。

---

## 3. 能安全压到多小：尺寸下限矩阵

扫描帧固定归一化到 1280×720，所以"缩小截图再放大"等于没缩。
真正决定成败的是**码在 1280×720 帧里占多少像素**。
下表为合成码（花背景）在不同帧内尺寸下的结果（各 10 次）：

| 帧内码尺寸 | sf=1.00 | sf=0.50 | sf=0.42 | sf=0.35 | sf=0.25 |
|---|---|---|---|---|---|
| 308 px | OK | OK | OK | OK | OK |
| 198 px | OK | OK | OK | OK | OK |
| 154 px ← **真实码量级（实测 149px）** | OK | OK | OK | OK | OK |
| 109 px | OK | OK | OK | OK | OK |
| 87 px | OK | OK | OK | OK | OK |
| 66 px | OK | OK | OK | OK | **MISS** |
| 44 px | OK | OK | OK | OK | **MISS** |
| ≤45 px | **MISS** | MISS | MISS | MISS | MISS |

结论：
- 44 px 是整条链路的物理下限，`sf=1.0` 也救不了。
- `sf ≥ 0.35` 时下限仍是 44 px，即**没有任何检测能力损失**。
- `sf=0.25` 会开始丢码（66px 就漏），不要用。
- 真实登录码 149 px，`sf=0.42` 仍有 **3.4 倍**尺寸余量。

---

## 4. 改动（A 已落地，B 待界面设置）

### 改动 A（一行，收益最大）— ✅ 已落地

`src/Core/QRScanner.cpp`：

```diff
     detector = cv::makePtr<cv::wechat_qrcode::WeChatQRCode>(paths[0].string(), paths[1].string(),
                                                           paths[2].string(), paths[3].string());
-    detector->setScaleFactor(1.0);
+    // Scales the image handed to the detector network. The library's own default
+    // targets an area of 160000 px, but 1.0 fed it the entire 1280x720 scan frame
+    // (921600 px, 5.76x more) and cost ~88 ms per decode on a busy screen instead
+    // of ~7 ms. 0.42*1280 x 0.42*720 == 162500 px, i.e. that documented default.
+    // The detection floor is unchanged: codes down to ~44 px inside the scan frame
+    // still decode, and the real Wuthering Waves login code measures ~149 px there.
+    detector->setScaleFactor(0.42f);
```

**落地后实测**（`tests/scan_latency/verify_shipped_scanner.cpp`，跑的是应用本体的
`QRScanner`，不是基准里自建的 detector，20 次）：

```
shipped QRScanner decodeSingle @1280x720
  min=6.3304ms  median=7.7841ms  max=12.0715ms  (n=20)
  payload=G152#KURO_038a5d17-f3f6-4474-ba86-dba3aac105d4
PASS
```

码文与改动前逐字一致（`expected` 常量即改动前 sf=1.0 解出的原文），
中位从 87.9 ms 降到 7.8 ms。该验证程序在码文不符或中位 >20 ms 时会返回非零退出码。

**影响面**：`QRScanner` 被两路共用 —— 屏幕扫描（`QRCodeForScreen`，`threadNumber=1`）
与直播流（`QRCodeForStream`，`threadNumber=3`，`thread_local QRScanner`）。
直播流的喂帧在 `setStreamHW()` 里也是原尺寸（竖屏 / height 480 或 720）或 `width/1.5`，
主流的 1920×1080 源同样落到 1280×720，即面积与屏幕路一致，收益与风险同构。

### 改动 B（零代码，界面即可）— ⏳ 待设置

`scan_interval_ms`：**100 → 30**（`WindowMain.ui` 的 `spinScanInterval` 范围是 10~5000，可直接改；
本机 `app/Config/userinfo.json` 当前为 100）。

> B 只有在 A 生效后才有意义。改 A 之前解码占 88ms，池线程长期被占，
> 把间隔压到 30ms 只会让 `tryStart` 大量丢帧，平均延迟几乎不变。

### 改后延迟

| 配置 | 平均 | 最坏 |
|---|---|---|
| 现状 sf=1.0，间隔 100ms | 140 ms | 193 ms |
| 仅 A：sf=0.42，间隔 100ms | 59 ms | 111 ms |
| **A + B：sf=0.42，间隔 30ms** | **≈ 23 ms** | **≈ 40 ms** |
| A + B 激进：sf=0.35，间隔 10ms | ≈ 15 ms | ≈ 24 ms |

即 **≈140ms → ≈23ms，约 6 倍**。间隔取 30ms 还是 10ms 只差 8ms，
但 10ms 时循环由显示器刷新率兜底（60Hz ≈ 16.7ms），CPU 略高，30ms 更稳。

---

## 5. 未决 / 风险

- 尺寸下限矩阵用的是**合成码**，只有一张真实游戏截图。建议再抓 2~3 张不同
  游戏窗口大小 / 不同缩放比例的真实登录码，确认 149px 这个量级是下限而非上限。
- 「小窗 + 高 DPI 缩放」会让码在 1280×720 帧里变小；若实测有 60px 以下的场景，
  应保留 `sf=0.42` 快通路 + 低频（如每 500ms 一次）`sf=1.0` 兜底复检，
  避免池线程被兜底拖满而丢帧。
- 原先 `CMakeLists.txt` 的 `SHOW` 宏语义是**反的**（非 DEV 构建反而定义 `SHOW`，
  而代码用 `#ifndef SHOW` 包住预览）。v2.0.9 已改为正向的 `ERYOU_SCAN_PREVIEW`，
  行为经二进制取证确认未变，详见 [FIX_v2.0.9.md](FIX_v2.0.9.md)。
- 单线程 CPU 实测值；更慢的机器上解码耗时会等比放大，但 A 的倍数收益不变。
