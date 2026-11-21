# 第三方源码管理与可选原生依赖构建指南

[English](DEPENDENCIES.md) · [返回首页](../README.zh-CN.md) · [第三方许可证声明](../THIRD_PARTY_NOTICES.zh-CN.md)

`hx_c_platform` 的基础能力（文件、字符串、内置配置解析、容器、线程池、基础网络等）仅需子模块源码即可直接编译；而密码学（OpenSSL）、归档压缩（libarchive / zlib / lzma / zstd）、HTTP（libcurl）、Unicode 高级变换（utf8proc）等重度模块则通过统一的依赖构建工程（`tools/dependencies`）预先编译为原生 SDK。

本文说明 `deps/` 目录结构、子模块初始化方式，以及如何在桌面端、嵌入式 Linux 与 Android 上一键构建所需的原生依赖 SDK。

---

## 1. `deps/` 目录布局

所有第三方上游源码统一存放在 `deps/` 下，并且**保持上游源码完全不修改**；本项目自己的构建适配、符号隔离与包装逻辑均位于 `cmake/`、`tools/` 和 `src/` 中，构建产物均输出到 Git 忽略的构建目录：

```text
deps/
  openssl/          Git 子模块（OpenSSL 3.5.9）
  libarchive/       Git 子模块（libarchive 3.8.9）
  zlib/             Git 子模块（zlib 1.3.2）
  xz/               Git 子模块（XZ / liblzma 5.8.4）
  zstd/             Git 子模块（zstd 1.5.7）
  curl/             Git 子模块（libcurl SDK）
  c-ares/           Git 子模块（可取消 DNS 1.34.8）
  utf8proc/         Git 子模块（Unicode 变换 2.12.0）
  7zip/             Git 子模块（独立可选 7-Zip 动态后端 26.04）
  cjson/            Git 子模块（可选 JSON 解析后端）
  libyaml/          Git 子模块（可选 YAML 解析后端）
  minixml/          Git 子模块（可选 XML 解析后端）
  inih/             Git 子模块（可选 INI 解析后端）
  tomlc/            Git 子模块（可选 TOML 解析后端）
  kcp/upstream/     当前使用的 KCP 子模块
  uthash/upstream/  当前使用的 uthash 子模块
  kcp/<commit>/     保留的旧快照（不参与当前构建）
  uthash/v2.4.0/    保留的旧快照（不参与当前构建）
  minicoro/v0.1.3/  保留的早期评估副本（不参与任何构建）
  .fetchcontent/    Git 忽略的 FetchContent 下载缓存目录
  .cache/           Git 忽略的旧版归档缓存目录
```

所有固定版本号、Git 提交哈希与发布包 SHA256 均集中定义在 [cmake/HxDependencySources.cmake](../cmake/HxDependencySources.cmake) 中（旧版 Python 准备脚本也直接委托给该 CMake 模块）。

---

## 2. 方式一（默认推荐）：使用 Git Submodule

克隆仓库时带上 `--recurse-submodules`，或在已有仓库中拉取全部子模块：

```sh
git clone --branch dev --recurse-submodules \
  https://github.com/HuangX666/hx_c_platform.git
cd hx_c_platform

# 若已克隆主仓库，补齐子模块：
git submodule update --init --recursive
```

主工程和 `tools/dependencies` 在配置阶段都会严格校验子模块的 Git 提交号：如果子模块未初始化或停留在其他提交，CMake 会立即报错停止，绝不会静默使用未知分支的代码。

### 两个官方归档包例外（`libiconv` 与 `SQLite`）

1. **GNU libiconv（`HX_OS_ENABLE_ICONV=ON`，默认关闭）**：由于 GNU libiconv 官方 Git 仓库不包含发布包中预生成的编码转换表，开启源码版 `ICONV` 时会通过 `FetchContent` 自动下载官方 `1.18` 发布包、校验 SHA256 并缓存到 `deps/.fetchcontent/iconv/`（无需在 Windows 或交叉编译宿主机上安装转换表生成工具）。若设置 `HX_OS_ICONV_USE_SYSTEM=ON` 则改用系统自带的 `iconv`。
2. **SQLite（`HX_OS_ENABLE_PERSISTENT_QUEUE=ON`，默认开启）**：通过 `FetchContent` 下载 SQLite 官方 `3.54.0` Amalgamation 合并源码包、校验 SHA256 并缓存到 `deps/.fetchcontent/sqlite/`，直接编译为 `hx_c_platform::sqlite` 目标并随安装包导出头文件和库。

> [!TIP]
> **如果不需要外部原生 SDK 模块**：
> 可以直接关闭 `CRYPTO`、`TLS`、`ARCHIVE`、`COMPRESSION`、`HTTP`、`URL`、`UNICODE` 和 `PERSISTENT_QUEUE`，或者在配置 CMake 时使用 `-C cmake/minimal.cmake` 预设。此时核心 UTF-8 校验/转换与内置配置解析器仍然可用。

---

## 3. 方式二（备选）：使用 CMake `FetchContent`

如果你不希望使用 Git Submodule，可以在主工程、`tools/dependencies` 或 `tools/sevenzip` 配置时传入 `-DHX_OS_DEPENDENCY_PROVIDER=FETCHCONTENT`。CMake 会自动拉取完全一致的固定提交并缓存到 `deps/.fetchcontent/<名称>/<提交号>`（支持并发文件锁，只读源码树可通过 `-DHX_OS_DEPENDENCY_CACHE_DIR=...` 指定外部可写缓存目录）。

需要在离线构建前预先拉取源码时，可运行 `tools/sources` 辅助工程：

```sh
# 校验默认 submodule 源码：
cmake -S tools/sources -B build-sources

# 或通过 FetchContent 预下载固定版本源码：
cmake -S tools/sources -B build-sources-fetch \
  -DHX_OS_DEPENDENCY_PROVIDER=FETCHCONTENT
```

---

## 4. 构建桌面端原生依赖 SDK（`tools/dependencies`）

虽然 `hx_c_platform` 核心库仅要求 CMake 3.16+，但**编译 `tools/dependencies` 原生依赖工程需要 CMake 3.20+**（上游 XZ 的 CMake 构建要求）。此外，编译 OpenSSL 需要构建机安装 **Perl**（Windows 默认启用汇编优化时还需安装 **NASM** 并加入 `PATH`，这些仅是编译期工具，非运行时依赖）。

### 4.1 Linux 构建命令

```sh
cmake -S tools/dependencies -B build-deps -DCMAKE_BUILD_TYPE=Release
cmake --build build-deps --parallel 2
```

### 4.2 Windows（MSVC x64）构建命令

打开 **x64 Native Tools Command Prompt for VS 2022** 执行（若未安装 NASM，可附加 `-DHX_DEPS_OPENSSL_ASM=OFF` 使用可移植 C 实现构建）：

```bat
cmake -S tools/dependencies -B build-deps -G "Visual Studio 17 2022" -A x64 -DHX_DEPS_OPENSSL_ASM=OFF
cmake --build build-deps --config Release --parallel 2
```

### 4.3 依赖 SDK 输出与配置说明

- **默认输出路径**：安装在 `build-deps/prefix`（可通过 `-DCMAKE_INSTALL_PREFIX=...` 自定义）。随后构建主工程时，通过 `-DCMAKE_PREFIX_PATH=/absolute/path/to/build-deps/prefix` 或 `-DHX_OS_DEPENDENCY_PREFIX=...` 传入该路径即可。
- **动态与静态组合**：默认情况下，OpenSSL、libcurl、utf8proc 编译为动态库（DLL / `.so`），zlib、liblzma、zstd、libarchive 编译为启用位置无关代码（PIC）的静态库。
- **静态链接 OpenSSL**：如需静态 OpenSSL，请在构建依赖时传入 `-DHX_DEPS_OPENSSL_SHARED=OFF`，并在主工程传入 `-DHX_OS_OPENSSL_STATIC=ON`（外部自建线程需遵守 [OpenSSL 线程清理约定](CRYPTO.zh-CN.md#5-线程内存与插件卸载)）。
- **MSVC Debug 工程注意**：如果你的业务工程使用 MSVC `Debug`（`/MDd`），请使用独立的构建目录并传入 `-DHX_DEPS_CONFIGURATION=Debug` 生成 Debug 版 SDK prefix，切勿将 `/MDd` 与 `/MD` 静态库混用。
- **并行度控制**：外层 `--parallel` 控制同时编译几个依赖库，`-DHX_DEPS_JOBS=N` 控制单个子库内部的编译并发数。

---

## 5. 验证编译好的原生依赖 SDK

依赖构建完成后，可运行内置的自检工程验证全部归档格式与加密算法是否正常工作（请使用 `prefix` 的绝对路径）：

```sh
cmake -S tools/dependencies/check -B build-deps-check \
  -DCMAKE_PREFIX_PATH=/absolute/project/build-deps/prefix
cmake --build build-deps-check --config Release
ctest --test-dir build-deps-check -C Release --output-on-failure
```

该检查程序会在内存中实际执行 ZIP、AES-256 加密 ZIP、普通 7z、TAR、TAR.GZ、TAR.XZ 与 TAR.Zstd 的打包与解包，核对二进制载荷并确认错误密码会被正确拒绝。

> [!NOTE]
> 如需支持 **7z 密码加密与文件名加密**，可按 [压缩教程第 4 节](ARCHIVE.zh-CN.md#4-7z-的密码与文件名加密) 通过独立的 `tools/sevenzip` 工程构建可选的 `7-Zip` 动态后端。

---

## 6. 嵌入式 Linux ARM 与 Android 交叉编译

交叉编译应在 Linux 或 WSL 环境下执行，并为每个目标架构/API 单独维护一套依赖 SDK prefix。

### 6.1 嵌入式 Linux（AArch64 / ARMv7）

```sh
# 1. 交叉编译 AArch64 依赖 SDK
cmake -S tools/dependencies -B build-deps-aarch64 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/toolchains/embedded-linux.cmake" \
  -DHX_OS_TARGET_PROCESSOR=aarch64 \
  -DHX_OS_CROSS_PREFIX=aarch64-linux-gnu- -DHX_OS_SYSROOT=/ \
  -DHX_DEPS_OPENSSL_TARGET=linux-aarch64
cmake --build build-deps-aarch64 --parallel 2

# 2. 交叉编译主工程
cmake -S . -B build-arm64 -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/toolchains/embedded-linux.cmake" \
  -DHX_OS_TARGET_PROCESSOR=aarch64 \
  -DHX_OS_CROSS_PREFIX=aarch64-linux-gnu- -DHX_OS_SYSROOT=/ \
  -DHX_OS_DEPENDENCY_PREFIX="$PWD/build-deps-aarch64/prefix" \
  -DCMAKE_CROSSCOMPILING_EMULATOR="qemu-aarch64;-L;/usr/aarch64-linux-gnu"
cmake --build build-arm64 --parallel 4
```

若目标为 32 位 **ARMv7 hard-float**，只需将对应参数替换为 `armv7`、`arm-linux-gnueabihf-`、OpenSSL 目标 `linux-armv4` 以及 `qemu-arm;-L;/usr/arm-linux-gnueabihf`。

### 6.2 Android NDK（`arm64-v8a` / `armeabi-v7a`）

构建 Android 依赖 SDK 需要 Linux / WSL 宿主环境、Linux 版 Android NDK、`make` 与 `Perl`（编译生成的 SDK 包可重定位，之后也可直接在 Windows 下配合相同 ABI/API 的 Windows NDK 消费）：

```sh
hx_android_toolchain="$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake"

cmake -S tools/dependencies -B build-deps-android-arm64 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$hx_android_toolchain" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-21 \
  -DHX_DEPS_OPENSSL_TARGET=android-arm64
cmake --build build-deps-android-arm64 --parallel 2
```

- 32 位 ARMv7 使用 `-DANDROID_ABI=armeabi-v7a` 与 `-DHX_DEPS_OPENSSL_TARGET=android-arm`；x86_64 模拟器使用 `x86_64` 与 `android-x86_64`。
- **Android OpenSSL SONAME 隔离**：为了避免与 Android 系统内置的同名 BoringSSL 冲突，本项目通过 `openssl-android.conf` 的 `shlib_variant` 将运行库 SONAME 设置为 `libssl-hx.so` 和 `libcrypto-hx.so`，详见 [Android 构建指南](ANDROID.zh-CN.md)。

---

## 7. 导出的原生 SDK CMake 目标

当主工程启用对应模块并安装后，除了导出 `hx_c_platform::hx_c_platform` 与 `hx_c_platform::cpp` 外，还会自动导出以下底层原生目标供业务直接调用（详见 [底层依赖调用](NATIVE_DEPENDENCIES.zh-CN.md)）：

- 单项原生目标：`hx_c_platform::sqlite`、`hx_c_platform::openssl`、`hx_c_platform::curl`、`hx_c_platform::utf8proc`、`hx_c_platform::archive`、`hx_c_platform::zlib`、`hx_c_platform::lzma`、`hx_c_platform::zstd`、`hx_c_platform::cares`。
- 汇总目标：`hx_c_platform::native`（一次性链接核心库与所有已启用的底层原生 SDK）。

---

## 8. 可选的可取消 DNS（`c-ares`）配置

本库包含两个彼此独立的 `c-ares` 开关：

1. **核心网络模块（`HX_OS_ENABLE_CARES`，默认 `ON`）**：
   直接将 `deps/c-ares`（1.34.8）编译为 PIC 静态库链入核心库，无需额外 DLL，用于显式 DNS Resolver、TCP 拨号、TLS、事件循环 DNS 与协程 DNS。传入 `-DHX_OS_ENABLE_CARES=OFF` 则回退为仅使用系统 DNS，显式 Resolver 接口返回 `HX_OS_NOT_SUPPORTED`。
2. **`libcurl` HTTP SDK（`HX_DEPS_CURL_CARES`，默认 `OFF`）**：
   默认构建的 `libcurl` 使用系统原生 DNS（HTTP 超时截止时间无法强行打断阻塞中的系统 DNS 解析）。如果你的 HTTP/HTTPS 请求在 DNS 无响应时也必须支持按截止时间立即取消，可在已有 `build-deps` 目录中开启 `HX_DEPS_CURL_CARES=ON` 并重新编译 `curl`：

   ```sh
   cmake -S tools/dependencies -B build-deps -DHX_DEPS_CURL_CARES=ON
   cmake --build build-deps --config Release --target hx_dep_build_curl
   ```

   该选项会将 `c-ares` 静态编入 `libcurl` 动态库中（不产生额外 DLL）。随后重新编译主库，并在代码中调用 `hx_os_http_use_cancellable_dns()`（或协程端 `hx_os_coro_http_use_cancellable_dns()`）即可显式校验并启用可取消 HTTP DNS（若当前加载的是普通系统 DNS 版 `libcurl`，该调用会明确返回 `HX_OS_NOT_SUPPORTED`）。
