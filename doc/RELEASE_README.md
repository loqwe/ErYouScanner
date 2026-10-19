# ErYouScanner v2.0.1

完整解压 ZIP，运行 `ErYouScanner.exe`。更新前备份 EXE 旁的 `Config`，保留 DLL、`plugins` 和 `ScanModel`。

## 新增功能

- 米哈游短信登录支持极验 v4。
- 人机验证增加加载提示、超时提示和阶段日志。

## 修复和优化

- 合并 PR #8 的抖音直播修复。
- 修正米哈游短信接口，恢复短信验证码登录。
- 修复验证窗口白屏、缩放留白及重复打开异常。
- 统一英文程序名。
- 优化历史直播间列表展示、头像缓存、状态卡片和删除操作。

## 使用说明

选择平台并添加账号，设置确认延迟后，点击“监视屏幕”或“监视直播间”。失效账号可右键重新登录。

日志位于 `Config/scanner.log`，短信失败会记录业务返回码，结果未知时不会自动重发。账号凭据与当前 Windows 用户绑定，换电脑后需重新登录。

运行需要 Visual C++ 2015–2022 x64 Runtime，人机验证需要 Microsoft Edge WebView2 Runtime。

发布记录：https://github.com/loqwe/ErYouScanner/releases
