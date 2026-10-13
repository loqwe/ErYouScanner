# 官服 Cookie 登录

推荐在 **账号管理 → 添加账号 → 扫码登录** 中添加账号。
旧版 `user.mihoyo.com` 获取 Cookie 的说明已失效，不再作为使用步骤。

若已持有自己的有效账号 Cookie，可在 **Cookie登录** 中填写，程序需要：

- 账号标识：`stuid`、`ltuid` 或 `account_id`，任意一种。
- `stoken`。
- `mid`。

字段使用标准 Cookie 格式，以分号分隔。账号有效性以启动自检和实际登录结果为准。
v1.16.4 起仅支持官服账号，不能填写 B服 `access_key` 代替上述字段。
请勿在 Issue、截图、日志附件或公开仓库中粘贴自己的 Cookie。
