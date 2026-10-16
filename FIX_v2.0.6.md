# v2.0.6 扫码短信字段修复

## 根因

对照 `DSVVA/KuRo_Scanner` 的 `KuRoApi::SendScanSms` 实现，扫码短信接口使用 `Body{ "geeTestData=" }`，并不接收添加账号流程使用的极验票据。v2.0.5 虽然已通过人机验证并携带稳定 `devCode`，仍把极验 JSON 放入该字段，因此真实服务端继续返回 242。

## 修复

人机验证仍然是本地发送前置条件；验证成功后，扫码短信请求按上游协议发送空 `geeTestData` 字段，同时继续携带扫码会话的稳定 `devCode`。

## 验证

- `kuro_protocol_regression`：通过，断言 `scanSms` 使用空字段。
- `kuro_session_regression`：通过，仍断言三条扫码请求使用一致 `devCode`。
- Windows x64 Release 与版本资源：待 v2.0.6 构建后验证。

真实账号短信发送结果需要在目标电脑上完成人机验证后复测。
