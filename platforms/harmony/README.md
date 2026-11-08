# ARMSX2 for HarmonyOS（鸿蒙版）

在 HarmonyOS NEXT（纯血鸿蒙，API 12+）上运行的 ARMSX2。这是一个 DevEco Studio 工程（Stage 模型）：

- **模拟器核心**：和 Android、iOS 共用仓库根目录的 `common/`、`pcsx2/`，用鸿蒙 NDK 编译成 `libarmsx2.so`。
- **界面**：PCSX2 自带的 Big Picture 全屏界面（游戏列表、设置、游戏内菜单），由 Vulkan 直接画到 ArkUI 的 `XComponent` 上。
- **ArkTS 层**：只负责屏幕虚拟手柄、实体手柄按键、导入文件和前后台切换（`entry/src/main/ets`）。

## 当前状态

| 部分 | 状态 |
|---|---|
| 核心 + 宿主层用鸿蒙 SDK（API 18，clang 15）编译、链接 | ✅ 已在 Linux 上实际编译通过 |
| ARM64 JIT（鸿蒙编译器编译） | ✅ 在 qemu 里跑了 JIT 差分测试：2426 个通过；剩余失败均为测试环境原因（见下文） |
| Vulkan 画面（`VK_OHOS_surface`）、OHAudio 声音、触屏、虚拟手柄 | ⚠️ 代码完成，**尚未在真机上运行过** |
| ArkTS 界面 | ⚠️ 通过了 SDK 自带的 ArkTS 类型检查和 Linter，但还没用 DevEco 完整打包过 |
| OpenGL 渲染器、联网功能（成就、封面下载）、实体手柄摇杆、USB 设备 | ❌ 暂未支持 |

## 关于 JIT 权限（必读）

速度靠 JIT（把 PS2 指令实时翻译成 ARM64 代码），JIT 需要"可写可执行内存"。鸿蒙把这项能力做成了受限权限
`ohos.permission.kernel.ALLOW_WRITABLE_CODE_MEMORY`：

- 需要在 AppGallery Connect 申请 ACL，并在签名用的 Profile 里包含这个权限。
- 按华为当前文档，**只开放给平板和 2in1（鸿蒙电脑）设备，手机不在范围内**。
- 用户开启"坚盾守护模式"时，系统会拒绝这类内存。

拿不到权限时，核心会**自动退回解释器模式**（能跑，但大多数游戏会非常慢），画面上会提示一次。

`entry/src/main/module.json5` 里默认声明了这个权限。**如果你的签名 Profile 里没有这个权限，安装会失败**。
这时删掉 `requestPermissions` 里的这一项，就能装上解释器模式的测试包。

## 编译

### 1. 准备

- DevEco Studio 5.0 或更新版本（自带 HarmonyOS SDK，API 12 以上）。
- 完整克隆仓库（需要根目录的 `bin/resources`、`common/`、`pcsx2/`，以及 Android 目录里的第三方库）。
- 拉取 shaderc 的依赖（只需一次，Android 构建也要这一步）：

  ```bash
  python3 platforms/android/app/src/main/cpp/3rdparty/shaderc/utils/git-sync-deps
  ```

- Windows 上建议开启长路径支持（第三方库目录很深）。

### 2. 用 DevEco Studio 编译

1. `File > Open`，打开 `platforms/harmony` 目录。
2. `File > Project Structure > Signing Configs`：登录华为账号自动签名，或者配置你自己的证书。
3. 连接设备（或用云调试），点运行。

第一次编译原生部分大约需要 20–60 分钟（取决于电脑性能），之后是增量编译。
编译时 `entry/hvigorfile.ts` 会自动把仓库的 `bin/resources` 复制到 `rawfile/resources`（这个目录已被 git 忽略）。

### 3. 只编译原生库（不用 DevEco，可用于 CI）

```bash
OHOS_NATIVE=/path/to/ohos-sdk/linux/native
$OHOS_NATIVE/build-tools/cmake/bin/cmake -G Ninja \
  -DCMAKE_MAKE_PROGRAM=$OHOS_NATIVE/build-tools/cmake/bin/ninja \
  -DCMAKE_TOOLCHAIN_FILE=$OHOS_NATIVE/build/cmake/ohos.toolchain.cmake \
  -DOHOS_ARCH=arm64-v8a -DOHOS_STL=c++_shared -DCMAKE_BUILD_TYPE=Release \
  -S platforms/harmony/entry/src/main/cpp -B build-ohos
$OHOS_NATIVE/build-tools/cmake/bin/ninja -C build-ohos armsx2
```

注意要用 SDK 自带的 CMake（`build-tools/cmake`），系统里的 CMake 不认识 `OHOS` 平台。
OpenHarmony 的公开 SDK（例如 [5.1.0 Release](https://repo.huaweicloud.com/openharmony/os/5.1.0-Release/)）就能编译原生部分。

## 在设备或云调试上测试

1. 安装运行后，第一次启动会把资源解压到应用沙箱（几秒钟）。
2. 点顶部的 **Import BIOS**，选择你自己的 PS2 BIOS 文件；再点 **Import games**，选择 ISO/CHD 等游戏镜像。
   文件会复制到应用沙箱的 `bios/`、`games/` 目录，游戏列表会自动刷新。
3. 在 Big Picture 界面里点游戏启动。返回手势或顶部 **Menu** 打开游戏内菜单（存读档、设置、退出）。

**看日志**：

- DevEco Studio 的 HiLog 窗口，按 tag `ARMSX2`（domain `0x3200`）过滤，或者用命令行 `hdc hilog | grep ARMSX2`。
- 应用沙箱里的 `files/logs/emulog.txt` 是完整日志。
- 日志里出现 `Executable memory was refused` 说明没拿到 JIT 权限，正在用解释器。

反馈问题时，请附上 hilog 日志、设备型号、系统版本，以及哪个游戏、在哪一步出的问题。

## 技术说明（给开发者）

鸿蒙 NDK 自带的是 clang/libc++ 15，比 Android NDK 旧，核心为此做了几处兼容（在其他平台上不起作用）：

- **AArch64 的 `preserve_most` / `preserve_all`**：LLVM 18 起才支持。ARM64 JIT 调用 vtlb 内存读写函数时，依赖被调用方保留 x9–x15。
  在 clang 18 以下，这些函数改为普通调用约定，JIT 改为调用 `pcsx2/arm64/vtlbThunks-arm64.cpp` 里的汇编跳板，
  由跳板负责保存和恢复 x9–x15，对 JIT 生成的代码来说约定不变。EE FPU 模型调用则走它已有的"多保存寄存器"分支。
- **libc++ 15**：通过编译参数打开 `std::ranges`（`_LIBCPP_ENABLE_EXPERIMENTAL`），并由 `ohos_libcxx_compat.h` 补上 `make_unique_for_overwrite`。
- **平台适配**：Vulkan 窗口 `WindowInfo::Type::OHOS`、音频后端 `AudioBackend::OHAudio`、CMake 中的 `OHOS` 分支（无 libcurl、libpcap、D-Bus）。

**JIT 测试（不需要真机）**：仓库的 JIT 差分测试（同一段 PS2 代码分别用 JIT 和解释器跑，比较结果）可以用鸿蒙工具链静态编译，
再用 `qemu-aarch64-static` 在 x86 Linux 上运行。需要打开 `-DENABLE_RECOMPILER_TEST_HOOKS=ON`，并且不链接 OHAudio
（`-DARMSX2_USE_OHAUDIO=OFF`）。剩下的失败：两个测试要启动子进程，在 qemu 里做不到；
`EeRecUnalignedStore.SoftmemFormKeepsTheAlignmentBranch` 只在和其他测试同批运行时失败，单独运行能通过。

主要文件：

| 文件 | 作用 |
|---|---|
| `entry/src/main/cpp/CMakeLists.txt` | 原生构建入口 |
| `entry/src/main/cpp/cmake/SearchForStuff.cmake` | 第三方库（复用 Android 目录里的源码） |
| `entry/src/main/cpp/ohos_host.cpp` | Host 接口、CPU 线程、窗口、输入 |
| `entry/src/main/cpp/ohos_main.cpp` | NAPI 模块和 XComponent 回调 |
| `entry/src/main/cpp/ohos_resources.cpp` | 把 rawfile 资源解压到沙箱 |
| `pcsx2/Host/OHAudioStream.cpp` | OHAudio 音频输出 |
| `entry/src/main/ets/` | ArkTS 页面、虚拟手柄 |

## 许可证

和整个 ARMSX2 项目一样，采用 GPL-3.0-or-later。分发这个应用（包括上架应用市场）时，必须同时以 GPLv3 提供完整源码。
PS2 BIOS 和游戏受版权保护，不能随应用一起分发，只能由用户自己导入。
