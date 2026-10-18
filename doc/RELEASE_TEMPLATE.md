# 发布说明格式

发布标题为版本号，例如 `v2.0.1`。正文使用以下四节，短句列出实际变化，不粘贴调试过程或整段历史。不适用的条目写“无”，不能将未验证的功能写成已完成。

程序名、标题栏和 EXE 统一使用 `ErYouScanner` / `ErYouScanner.exe`。ZIP 命名 `ErYouScanner_v<版本号>_win64.zip`；仅上传 ZIP 和校验文件，不单独上传 EXE。说明文档放在 `doc`，README 保留使用入口。

```markdown
## 二改说明

基于 [DSVVA/MHY_Scanner](https://github.com/DSVVA/MHY_Scanner) 二次修改。

## 新增功能

- 本版新增内容。

## 修复和优化

- 本版修复内容。

## 下载

下载 ErYouScanner_v<版本号>_win64.zip，完整解压后运行 ErYouScanner.exe。
更新前备份 Config。

SHA256：见附件 SHA256SUMS.txt。
```
