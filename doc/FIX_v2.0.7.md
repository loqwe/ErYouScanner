# v2.0.7 扫码短信抓包对齐

## 证据

v2.0.6 在真实环境中已确认人机验证完成后仍返回 242。对照 `DSVVA/KuRo_Scanner` 的 `SendScanSms` 源码，扫码短信请求使用原始 body `geeTestData=`，并显式设置 `Content-Type: application/x-www-form-urlencoded`。

## 修复

- `scanSms` 从 CPR `Payload` 改为原始 `Body`。
- 显式设置 `Content-Type`。
- 增加脱敏请求摘要日志：method、path、Content-Type、body 长度、Token/devCode 是否存在；不记录凭据和验证码。

## 验证

- `kuro_protocol_regression`：通过。
- `kuro_session_regression`：通过。
- `app_ui_regression`：待 v2.0.7 构建后验证。
- 真实账号结果：待目标电脑使用 v2.0.7 复测。
