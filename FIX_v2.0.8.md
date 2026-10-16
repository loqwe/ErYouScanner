# v2.0.8 扫码短信 242 现场取证与诊断修正

## 根因（已用真实账号 + 真实游戏二维码取证）

`POST /user/sms/scanSms` 的真实响应是：

```json
{"code":242,"msg":"发送短信频繁","success":false,"traceId":"..."}
```

**242 是服务端对账号的限流，不是请求缺字段。**
v2.0.5（补 `devCode`）、v2.0.6（`geeTestData` 改空）、v2.0.7（原始 body + `Content-Type`）
三轮修改的目标本来就没有问题：本项目报文与两个上游参考逐字一致
（DSVVA/KuRo_Scanner `cpr::Body{"geeTestData="}`、Rocszzz/tide-scanner `{"geeTestData": ""}`），
日志自证 `body_bytes=12`。

### 现场证据

用 `app/Config/userinfo.json` 中 DPAPI 凭据（库街区账号 `3ci3446e` / uid `33934465`）
在本机解密后直接请求线上接口：

| # | devCode | token | body | 结果 |
|---|---|---|---|---|
| A | 上游固定 UUID | 真实 | `geeTestData=` | `242 发送短信频繁` |
| B | 随机 UUID | 真实 | `geeTestData=` | `242 发送短信频繁` |
| C | 上游固定 | 缺失 | `geeTestData=` | `220 访问令牌不能为空` |
| D | 随机 UUID | 真实 | 假极验票据 | `242 发送短信频繁` |
| E/F | 上游固定 | 伪造 | `geeTestData=` | `220 登录已过期，请重新登录` |

真实游戏二维码（`G152#KURO_038a5d17-…`）实测全链路：

| 步骤 | 响应 |
|---|---|
| `/user/auth/roleInfos` | `200`，`data` 含绑定手机号 `166****3616`、`puid`、`support` |
| `/user/auth/scanLogin`（`verifyCode` 空） | `2240` **当前用户未进行扫码二次验证** |
| `/user/sms/scanSms` | `242` **发送短信频繁** |

校验顺序为 token → 限流 → 参数；限流不按 `devCode`；限流发生在参数校验之前，
所以 242 不能用来判断 `geeTestData` 是否正确。

### 换账号对照（决定性）

新增账号 `339cf69b`（uid `33936692`）后，同一台机器、同一出口 IP、同一分钟内重跑，
`/user/sms/scanSms` 返回：

```json
{"code":200,"data":{"geeTest":false},"msg":"请求成功","success":true}
```

同一时刻 1 号账号仍为 `242 发送短信频繁`。这一次对照定死两点：

1. **242 按账号限流**，不按 IP、不按 `devCode`；被卡住的是 1 号账号本身。
2. **空 `geeTestData=` 是正确写法**——服务端返回 `"geeTest":false` 并受理，
   说明它不需要也不会消费弹窗里的极验票据。v2.0.5／v2.0.6／v2.0.7 三轮围绕该字段的改动
   至此确认全部无效。

时间线：当日 8 次 `scanSms` 全部 242；`16:29:26 → 17:22:01` 中间 53 分钟无任何请求
（含程序重启）仍为 242，故 1 号账号的窗口 **> 53 分钟**；同期 `/user/getSmsCode` 返回 200，
两类短信额度互相独立。

## 为什么此前"一直过不去"

`KuroScanSmsDialog::sent()` 收到 242 时提示"请等待倒计时后重新验证"，而倒计时只有 60 秒。
用户按提示每 60 秒重试一次，每次都落在限流窗口内，**重试行为本身在给限流续期**，
形成自锁循环。

## 修复

- `KuroResponse` 新增 `serverMessage`，保留服务端 `msg`（上限 200 字符、剔除控制字符）。
- `kuro request:` 与 `kuro scan SMS: result` 日志输出该字段，失败原因不再需要靠猜。
- `businessMessage()` 增加 `242 → 发送短信频繁（服务端限流）`。
- 收到 242 后停止本次扫码会话内的重试，按钮显示"服务端限流，已停止重试"，
  文案改为提示等待较长时间后重新扫码。
- 更正 README、RELEASE_README、CHANGELOG 中关于 242 的表述。
- `tests/app_ui/test_app_ui.cpp` 原断言要求 242 **不得**被描述为限流，
  即用测试固化了错误前提，现已改为要求 242 被描述为服务端限流且不再提供重试；
  接受路径改为从新的扫码会话验证。
- `tests/kuro_protocol/test_kuro_protocol.cpp` 新增 `diagnostics()`：
  校验 `msg` 被捕获、截断与清洗，且面向用户的消息不直接复制服务端原文。

## 验证

- `kuro_protocol_regression`：通过（新增 `diagnostics()`）。
- `kuro_session_regression`：通过。
- `app_ui_regression`：`test_app_ui` 编译通过；运行在 WebView2 初始化处不稳定
  （随机卡在 `smsFlow` 或 `scanSmsFlow` 的 captcha 初始化，均早于本轮改动的 242 路径），
  判定为构建环境问题，需在正常桌面会话复测。
- **真实链路已完整闭环**（账号 2 `339cf69b`，2026-10-16 19:01）：

  | 步骤 | 结果 |
  |---|---|
  | `/user/auth/roleInfos` | `200` |
  | `/user/auth/scanLogin`（`verifyCode` 空） | `2240 当前用户未进行扫码二次验证` |
  | `/user/sms/scanSms`（空 `geeTestData=`） | `200 {"data":{"geeTest":false},"msg":"请求成功"}` |
  | `/user/auth/scanLogin`（`verifyCode=423952`） | **`200 {"data":true,"msg":"请求成功"}`** |

  即扫码授权、二次短信、验证码续交四步全部成功，游戏端可进入。软件流程本身没有缺陷；
  本轮修的是失败路径的表述与重试策略。

## 未决

1 号账号限流窗口的准确长度。现只有 `> 53 分钟` 的下界；在测出确切值之前，
不把任何"等待 N 分钟再试"的数值写进代码。窗口只影响 1 号账号，换未受限账号可正常发送。
