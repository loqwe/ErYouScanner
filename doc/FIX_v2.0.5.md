# v2.0.5 扫码请求设备标识修复

## 根因

`KuroApi` 已支持发送 `devCode`，但 `KuroScanSession` 创建扫码 API 客户端时没有传入设备标识。因此鸣潮扫码的 `roleInfos`、`scanLogin` 和 `scanSms` 请求头为空，与上游扫码客户端不一致。

## 修复

扫码会话统一使用上游客户端的稳定设备标识，并覆盖三条扫码请求。库街区手机号登录仍使用其独立的临时设备标识，不与扫码会话混用。

## 验证

- `kuro_protocol_regression`：通过。
- `kuro_session_regression`：通过，并断言三条扫码请求的 `devCode` 一致。
- Windows x64 Release EXE：已编译，版本资源与 `2.0.5` 一致。

真实账号短信发送结果仍需在用户电脑上完成人机验证后实测；若服务端继续返回业务码 242，再根据新日志判断是否还存在验证码字段或接口状态差异。
