# 二游扫码 · ErYouScanner

Windows 多平台游戏扫码工具，基于 [DSVVA/MHY_Scanner](https://github.com/DSVVA/MHY_Scanner) 持续开发，保留米哈游官服扫码功能，并接入库街区账号与鸣潮官服扫码。

当前发行版本：**v2.00**。GitHub 仓库名同步为 **ErYouScanner**，发行包见 [GitHub Releases](https://github.com/loqwe/ErYouScanner/releases)。近期变化见 [更新日志](CHANGELOG.md)。

## 功能

- 米哈游／库街区平台切换、多账号管理和账号状态检查。
- 账号序号按当前平台从 1 连续显示；右键“重新登录”可更新原账号凭据，保留备注与默认账号设置。
- 米哈游官服账号支持扫码、短信和 Cookie 登录；保留新版 passport 与 Panda 二维码两阶段确认流程。
- 库街区添加账号仅提供短信登录：输入手机号、完成人机验证、获取验证码并登录。
- 识别鸣潮官服游戏二维码，进行扫码授权，并处理扫码过程中的短信二次验证。
- 从屏幕或支持的直播源识别二维码，可调整扫描间隔。
- 确认延迟支持 `0–60` 秒和小数；等待期间可取消，配置自动保存。
- 直播间历史自动保存；B 站支持房间号和完整链接。
- DXGI 屏幕捕获支持 RGBA16F 浮点纹理，保留直播坏包恢复、超时和后台任务异常保护。

库街区短信登录参考 [DSVVA/KuRo_Scanner](https://github.com/DSVVA/KuRo_Scanner/tree/1.0.0)，游戏扫码协议参考 [tide-scanner](https://github.com/Rocszzz/tide-scanner)。UI 沿用本项目，未改用 Python。真实账号登录、短信验证及完整游戏上线结果仍需要实际账号验证，不能用本地接口模拟结果代替。

## 开始使用

将 `二游扫码_v2.00_win64.zip` **完整解压**到可写目录，启动 `二游扫码_v2.00.exe`。一起保留 DLL、`plugins`、`ScanModel`；不要单独复制 EXE。运行需要 [Visual C++ x64 运行时库](https://aka.ms/vs/17/release/vc_redist.x64.exe)。

选择平台，打开 **账号管理 → 添加账号**：

- 米哈游：选择扫码、短信或 Cookie 登录，Cookie 格式见 [说明](doc/Cookie.md)。仅支持官服；历史 B服记录保留但不支持登录，B 站直播功能仍保留。
- 库街区：输入手机号，点击“获取验证码”，在内嵌窗口完成人机验证，填写收到的短信后点击“登录并添加账号”；可填写账号备注。需要 Microsoft Edge WebView2 Runtime。
- 库街区仅保留短信登录入口，隐藏网页登录、Token、Cookie 和扫码添加入口；已保存账号继续保留。发送请求后倒计时 60 秒，发送结果未知时不会自动重复请求。验证窗口可取消，最长等待 2 分钟。
- 账号失效时，先停止扫描，右键对应账号选择“重新登录”，使用原账号完成登录。软件核对平台和 UID 后更新凭据，不新增重复行；登录到其他账号时不覆盖原记录。
- 鸣潮扫码要求短信二次验证时，点击“验证并发送短信”，在弹窗完成人机验证后才会发起短信请求；再填写短信验证码并提交。人机验证取消不发送，超时不自动重发，扫码会话过期后需重新扫码。

“登录库街区账号”与“扫描鸣潮游戏码”是两个步骤。添加账号的人机验证不会跳过官方验证，也不代表库街区 App 扫码添加账号。

选中账号并设置确认延迟后，点击 **监视屏幕** 或 **监视直播间**。任务运行时再次点击可停止。游戏扫码要求短信二次验证时，按提示获取和提交验证码；此短信与添加账号时的登录短信不同。

## 配置与日志

配置固定保存在 EXE 旁的 `Config`，不受快捷方式“起始位置”影响。更新前备份现有 `Config`，发行 ZIP 不包含用户账号和配置。

Windows 账号凭据使用当前用户 DPAPI 加密保存；历史明文配置首次迁移前保留备份。加密后的凭据不能直接给另一台电脑或另一个 Windows 用户使用，迁移后需要重新登录。备份文件可能仍含原始凭据，应自行保管。

日志位置：`Config/scanner.log`。反馈时提供程序版本、出错时间、操作步骤和相关日志末尾，不要提交 Token、Cookie、验证码或完整二维码。日志记录阶段、业务返回码和可用异常信息。

日志达到 1MB 后滚动，保留当前文件和 3 个历史文件；本版修复与验证见 [v2.00 记录](FIX_v2.00.md)。日志会记录服务端返回的 `msg`（截断并去除控制字符），因此失败原因不必再从业务码猜。业务码 242 的含义已核实为**服务端限流**（服务端原话：发送短信频繁），按账号计，实测窗口超过 53 分钟，现场取证见 [242 抓包结论](DIAG_v2.0.7_242_live_probe.md)。收到 242 后软件会停止本次会话内的重试。

可捕获的致命异常会尽量追加 `fatal:` 日志并生成转储；系统强杀、直接 fail-fast 或进入 `main` 之前的错误可能绕过处理器。转储可能含账号内存，请勿公开上传 `.dmp`。

屏幕捕获使用 DXGI / D3D11，优先选择连接显示器的 GPU；二维码识别和当前直播解码仍使用 CPU，不代表开启 GPU 推理。

## 开发与发行管理

唯一源码放在 `source`，日常运行环境放在同级 `app`，历史 ZIP、符号和诊断材料分别归档。目录说明见 [项目管理](docs/PROJECT_LAYOUT.md)，构建及打包见 [本地构建](BUILD_LOCAL.md)。

GitHub 发行只提供带版本号的 ZIP 与 SHA-256 校验值，不单独上传 EXE，也不在用户包中携带 PDB、账号、日志或崩溃转储。

## 来源与许可

项目许可见 [LICENSE](LICENSE)。复用第三方代码时保留相应许可和版权说明。

- [DSVVA/MHY_Scanner](https://github.com/DSVVA/MHY_Scanner)：原项目。
- [Rocszzz/tide-scanner](https://github.com/Rocszzz/tide-scanner)：库街区登录与鸣潮扫码流程参考。
- [KuRo_Scanner](https://github.com/Theresa-0328/KuRo_Scanner)、[HonkaiScanner](https://github.com/HonkaiScanner)。
- [BililiveRecorder](https://github.com/BililiveRecorder/BililiveRecorder)、[Snap.Hutao](https://github.com/DGP-Studio/Snap.Hutao)。
