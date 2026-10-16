# v2.0.7 仍返回 242 — 现场抓包结论

**结论：242 不是请求字段缺失，是服务端按账号限流。**

```json
{"code":242,"msg":"发送短信频繁","success":false,"traceId":"1474f79e-b54d-45b4-8b27-cac829e47fb9"}
```

v2.0.5（补 devCode）、v2.0.6（geeTestData 改空）、v2.0.7（原始 body + Content-Type）
三轮改的都是同一个**本来就没问题**的东西。请求形状从头到尾就没有错。

## 取证方法

用 `app/Config/userinfo.json` 里 DPAPI 加密的库街区凭据
（账号 `3ci3446e` / uid `33934465`，token 为 123 字符 JWT）在本机解密后，
按上游逐字一致的请求形状直接请求 `https://api.kurobbs.com/user/sms/scanSms`，
读取**响应正文**（软件自身把 msg 丢掉了，所以此前一直看不到）。

请求头固定：`source=android`、`version=2.5.0`、`versionCode=2500`、
`User-Agent=okhttp/4.9.0`、`Accept=application/json`、
`Content-Type=application/x-www-form-urlencoded`。

## 对照探测

| # | devCode | token | body | 结果 |
|---|---|---|---|---|
| A | 上游固定 `3dee77f8-…dab6` | 真实 | `geeTestData=` | `242 发送短信频繁` |
| B | **随机 UUID** | 真实 | `geeTestData=` | `242 发送短信频繁` |
| C | 上游固定 | **缺失** | `geeTestData=` | `220 访问令牌不能为空` |
| D | 随机 UUID | 真实 | **假极验票据** | `242 发送短信频繁` |
| E | 上游固定 | **伪造** | `geeTestData=` | `220 登录已过期，请重新登录` |
| F | 上游固定 | 伪造（另一串） | `geeTestData=` | `220 登录已过期，请重新登录` |

## 判定

1. **242 的官方含义是"发送短信频繁"**，即服务端限流。
   README 中"业务码 242 暂无已核实的官方含义，不会将它解释为发送间隔不足"**不成立**，
   应更正。
2. **校验顺序：token → 限流 → 参数。** C 证明缺 token 报 220；E/F 证明凭证无效报 220。
   所以 220 与限流无关，此前把 220 当作异常的排查方向也不对。
3. **限流不按设备标识。** A 与 B 只差 devCode，结果都是 242。
   补充：上游 `Rocszzz/tide-scanner` 的 `devCode` 干脆是空字符串，说明它根本不是关键字段。
4. **限流发生在参数校验之前。** D 带了假极验票据仍是 242，说明 `geeTestData`
   在限流判定时根本没被读取——三轮围绕这个字段的修改不可能生效。
5. **限流按账号维度，不按 devCode，也不按出口 IP。** 见下节对照实验。

## 请求形状与上游完全一致（逐字核对）

| | 路径 | body | 关键头 |
|---|---|---|---|
| DSVVA/KuRo_Scanner `SendScanSms` | `/user/sms/scanSms` | `cpr::Body{"geeTestData="}` | devCode 固定 UUID、source/version/versionCode、token |
| Rocszzz/tide-scanner `send_scan_sms` | `/user/sms/scanSms` | `{"geeTestData": ""}` | devCode **空串**、source/version/versionCode、token |
| 本项目 v2.0.7 | `/user/sms/scanSms` | `rawBody = "geeTestData="` | 与上面一致 |

v2.0.7 日志也已自证：

```
kuro request-shape: method=POST path=/user/sms/scanSms content_type=application/x-www-form-urlencoded
                    body_bytes=12 token=present devCode=present
```

`body_bytes=12` 正是 `geeTestData=` 的长度。**报文没有任何缺失字段。**

## 对照实验：换第二个库街区账号（决定性）

新增账号 `339cf69b`（uid `33936692`），同一台机器、同一出口 IP、同一分钟内，
用它的 token 与另一个真实游戏二维码（`G152#KURO_92780739-8efd-4ea6-922c-2b76302d1b3b`）重跑：

| 步骤 | 1 号 `3ci3446e` | 2 号 `339cf69b` |
|---|---|---|
| `/user/getPrivateSet` | `200` | `200` |
| `/user/auth/roleInfos` | `200`，`mobile 166****3616`、`puid 566140304` | `200`，`mobile 188****7790`、`puid 566149027` |
| `/user/auth/scanLogin`（`verifyCode` 空） | `2240 当前用户未进行扫码二次验证` | `2240 当前用户未进行扫码二次验证` |
| `/user/sms/scanSms` | **`242 发送短信频繁`** | **`200` `{"data":{"geeTest":false},"msg":"请求成功"}`** |

这一次实验同时定死两件事：

1. **242 是按账号限流，不是按 IP，也不是按 `devCode`。**
   同一出口、同一时刻，2 号成功、1 号被限，说明限流键是账号本身。
2. **空 `geeTestData=` 是服务端接受的正确写法。**
   2 号返回 `"geeTest":false`，即服务端明确记录"本次未走极验"，仍然受理。
   v2.0.5／v2.0.6／v2.0.7 围绕这个字段的三轮改动至此确认全部无效。
   用户在弹窗里完成的人机验证只作为本地前置门槛，服务端不需要该票据。

3. **全链路已闭环。** 收到短信后用 `verifyCode=423952` 提交
   `/user/auth/scanLogin`，返回 `{"code":200,"data":true,"msg":"请求成功"}`，
   游戏端进入成功。整条扫码授权链路不存在缺陷，本轮修的是失败路径的表述与重试策略。

## 日志时间线佐证

今日 `Config/scanner.log` 中 8 次 `scanSms` 全部 242（16:03:12 → 17:59:13），无一例外：

- `16:05:21 → 16:29:26`（干净间隔 24 分钟）→ 242
- `16:29:26 → 17:22:01`（干净间隔 **53 分钟**，中间程序重启，无任何请求）→ 242
- 同一时段 `/user/getSmsCode`（添加账号的登录短信）多次返回 200
  → 登录短信与扫码短信是两套独立额度，不能拿前者推断后者。

**本地 60 秒冷却与服务端真实窗口至少差 53 倍。**

## 为什么"一直过不去"

`src/UI/KuroScanSmsDialog.hpp` 的 `sent()` 在收到 242 时显示：

> 服务器拒绝发送（业务码 242）。请等待倒计时后重新验证；若仍失败，请反馈此错误码。

而倒计时只有 60 秒。用户照着提示每 60 秒重试一次，每次重试都落在限流窗口里，
**重试本身在给限流续期**——这是一个自己锁死自己的死循环，与代码逻辑错误无关。

## 附带发现的两个工程问题

1. **响应正文被丢弃。** `KuroApi::post()` 只取出 `code`，服务端的 `msg` 直接丢掉；
   `WriteScannerLog` 只记录 `stage/http/code/error`。服务端其实一直在用中文说清楚原因，
   这是前几轮修复只能靠"对照上游猜"的根本原因。
2. **回归测试把错误前提固化了。** `tests/app_ui/test_app_ui.cpp:236`：

   ```cpp
   require(status->text().contains("242") && !status->text().contains(QStringLiteral("不足")),
           "242 falsely described as local rate limit");
   ```

   这条断言主动禁止把 242 描述成限流，等于用测试保护了 bug 本身。

## 建议修复（待确认）

1. 保留并脱敏记录服务端 `msg`（`发送短信频繁` 不含任何凭据），写进 `kuro request:` 日志。
2. `businessMessage()` 增加 `242 → 发送短信频繁（服务端限流）`。
3. 242 不再启动 60 秒倒计时，改为明确提示"服务端限流，请停止重试"。
4. 更正 README、CHANGELOG、FIX_v2.0.4 中关于 242 的表述，并更新上述测试断言。
5. 真机验证顺序：先停止一切重试并等限流窗口过期，再**单次**触发，
   用新的 `msg` 日志确认；此时才能验证空 `geeTestData` 是否真的被接受。

## 未决

- 1 号账号限流窗口的准确长度。现有证据只能确定 **> 53 分钟**，需要一段干净间隔后单次复测。
  在窗口长度确定前，任何"改成等 N 分钟再试"的写法都是推测。
- 限流窗口现在只影响 1 号账号；换一个未被限流的账号（如 2 号）可正常发送。
