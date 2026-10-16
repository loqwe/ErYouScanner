# 本地构建与打包

当前发行版本 **v2.00**，产品名 **二游扫码**，CMake target 为 `ErYouScanner`。源码目录已迁移为 `D:/崩铁脚本/二游扫码/source`。旧版验证记录保留在 `FIX_v1.16.*.md` 等历史文档中。

## 依赖

- Visual Studio 2022 / Build Tools，x64 C++ 工具集和 Windows SDK。
- CMake 3.26 以上，C++23，Release，动态 MSVC 运行库 `/MD`。
- Qt 6.8.0，`msvc2022_64`，Widgets 与 Concurrent。
- vcpkg `x64-windows-static-md`；版本基线见 `vcpkg.json`。
- 同一套 FFmpeg shared SDK 的头文件、导入库和 DLL。

现有开发依赖继续放在 `D:/mhy-build-deps`，不随产品改名迁移。默认 Qt 路径是 `D:/mhy-build-deps/qt/6.8.0/msvc2022_64`。脚本优先使用 `source/.deps/ffmpeg`，也支持显式指定已有 FFmpeg SDK，不自动选择新的 `latest` 替换本地依赖。JSON 依赖默认使用 `source/.deps/nlohmann_json`，存在时通过 FetchContent 源码覆盖参数使用；可用 `-JsonSourceRoot` 指定其他源码目录，避免依赖旧 build 中的缓存。

旧 CMake 缓存含原目录的绝对路径，不能把旧 build 文件夹搬过来直接增量编译。新版本统一配置到同级 `../build/v2.00`。

## 一条命令构建并打包

在源码目录执行；脚本兼容 Windows PowerShell 5.1 与 PowerShell 7：

```powershell
./tools/package.ps1
```

默认 SDK 尚未归位时，`-FfmpegRoot` 必须指向实际存在、包含 `bin/lib/include` 的 SDK。也可以指定依赖和工具路径：

```powershell
./tools/package.ps1 `
  -DependencyRoot 'D:/mhy-build-deps' `
  -QtRoot 'D:/mhy-build-deps/qt/6.8.0/msvc2022_64' `
  -FfmpegRoot 'D:/path/to/ffmpeg-shared-sdk' `
  -JsonSourceRoot 'D:/path/to/nlohmann_json' `
  -CMakePath 'C:/path/to/cmake.exe' `
  -Parallel 4
```

脚本每次重新生成 CMake 缓存，编译后安装 Runtime 和 Symbols 两个组件，并创建全新的 staging 目录。已有同版本 ZIP 时默认停止；确实需要替换当前开发包时显式增加 `-ReplacePackage`。对外重新发布修改后的程序应使用新版本号。

输出位置：

```text
../build/v2.00/                         编译目录
../build/staging-v2.00-<id>/             当次干净打包目录
../releases/v2.00/
  二游扫码_v2.00_win64.zip
  SHA256SUMS.txt
../symbols/v2.00/<build-id>/
  二游扫码_v2.00.pdb
  build-info.json
  release-info.json
```

ZIP 内有一个完整运行目录、版本化 EXE、依赖、模型、许可、使用说明和 `build-info.json`。清单包含唯一符号构建编号、EXE/PDB 哈希、源码提交、是否有未提交改动、运行文件哈希及构建时间，可用于找到精确对应的 PDB。PDB 先暂存在当次 staging，ZIP 压缩成功后归档到唯一 build-id 目录，不覆盖同版本旧符号；符号旁的 `release-info.json` 额外记录最终 ZIP 哈希。源码提交值本身不能代替保存未提交源码。

脚本不会运行程序、替换 `app`、提交、推送或发布 GitHub Release。

## 直接使用 CMake

脚本等价于以下流程；工具链、Qt 和 FFmpeg 目录按实际环境填写：

```powershell
cmake --fresh -S . -B ../build/v2.00 -G 'Visual Studio 17 2022' -A x64 `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_TOOLCHAIN_FILE=D:/mhy-build-deps/vcpkg/scripts/buildsystems/vcpkg.cmake `
  -DVCPKG_TARGET_TRIPLET=x64-windows-static-md `
  -DVCPKG_INSTALLED_DIR=D:/mhy-build-deps/installed `
  -DVCPKG_MANIFEST_INSTALL=OFF `
  -DCMAKE_PREFIX_PATH=D:/mhy-build-deps/qt/6.8.0/msvc2022_64 `
  -DQT6_INSTALL_PREFIX=D:/mhy-build-deps/qt/6.8.0/msvc2022_64 `
  -DFFMPEG_DIR=D:/mhy-build-deps/ffmpeg -DUNIT_TESTS=OFF -DDEV=OFF
cmake --build ../build/v2.00 --config Release --parallel 4
cmake --install ../build/v2.00 --config Release --component Runtime --prefix ../build/manual-stage
cmake --install ../build/v2.00 --config Release --component Symbols --prefix ../build/manual-symbols
```

不指定 `FFMPEG_DIR` 时，底层 CMake 保留原先下载 FFmpeg 的行为；维护发行版时应明确指定已验证的 SDK。

## 验证与发布

离线与本机回归执行 `./tools/test.ps1`。实际应用界面测试可在主项目配置时增加
`-DERYOU_UI_TESTS=ON`，构建 `test_app_ui` 后执行 `ctest --test-dir ../build/v2.00 -C Release --output-on-failure`。
该测试使用临时配置，不读取实际账号；WebView 检查只初始化官方窗口并取消，不提交登录。
本次验证记录见 [FIX_v2.00.md](FIX_v2.00.md)。

至少确认完整编译、对应的回归测试、无账号配置的启动和 ZIP 内容。真实账号、短信、官网验证及实际游戏登录需要单独验证，不得只因接口返回成功就宣称游戏已上线。

版本号需要同时更新 CMake、Windows RC 元数据和文档；应用显示版本、EXE 与 ZIP 名称从同一个 CMake 版本产生。

CI 在 main、标签和手动触发时构建 ZIP artifact，并单独保存带构建编号的 Symbols artifact（默认保留 90 天，应在到期前归档）。用户 ZIP 和 GitHub Release 不包含 PDB。手动在对应版本标签上选择 `create_draft_release` 才创建新的草稿，已存在的 Release 会拒绝覆盖。最终发布前需核对测试结果和哈希。

CI 默认仍从 FFmpeg 上游 `latest` 下载 SDK，不保证与本地固定 SDK 的二进制相同；CI 制品必须单独验证，不能直接沿用本地包的验证结论。正式发布需要同时保留当次运行依赖、源码和匹配符号。
