# 第三方源码管理与可选依赖构建

[English](DEPENDENCIES.md) · [首页](../README.zh-CN.md) · [许可证说明](../THIRD_PARTY_NOTICES.zh-CN.md)

## 1. 目录怎么放

第三方源码统一放在 `deps/`。本项目自己的适配逻辑放在 `cmake/`、`tools/`、`src/`；生成文件和依赖构建产物放在 Git 忽略的构建目录，不修改上游源码。

```text
deps/
  openssl/          Git 子模块
  libarchive/       Git 子模块
  zlib/             Git 子模块
  xz/               Git 子模块
  zstd/             Git 子模块
  7zip/             独立可选原生后端子模块
  cjson/            可选解析器子模块
  libyaml/          可选解析器子模块
  minixml/          可选解析器子模块
  inih/             可选解析器子模块
  tomlc/            可选解析器子模块
  kcp/upstream/     当前使用的 KCP 子模块
  uthash/upstream/  当前使用的 uthash 子模块
  kcp/<commit>/     原样保留的旧快照，当前构建不使用
  uthash/v2.4.0/    原样保留的旧快照，当前构建不使用
  minicoro/v0.1.3/  仅保留早期评估副本，不参与构建
  .fetchcontent/   忽略的备选下载缓存
  .cache/          忽略的旧解析器／iconv 发布包缓存
```

原有 `.deps/` 和 `deps/.cache/` 可通过显式源码路径继续使用。新构建统一经过 `cmake/HxDependencySources.cmake`；旧 Python 准备命令也委托给它，不再单独维护下载地址和版本表。历史验证记录保留当时的路径。

## 2. 默认使用 Git submodule

```sh
git clone --branch dev --recurse-submodules https://github.com/HuangX666/hx_c_platform.git
cd hx_c_platform
# 已有仓库补齐子模块：
git submodule update --init --recursive
```

主工程和可选依赖构建器都会校验固定 Git 提交，缺失或版本不符会明确失败，不会跟随分支最新提交。当前开发分支是 `dev`。13 个 Git 子模块的 URL 见 [.gitmodules](../.gitmodules)，版本、提交及 iconv 归档散列统一见 [HxDependencySources.cmake](../cmake/HxDependencySources.cmake)。

默认开启的容器、KCP 需要初始化对应子模块。五种可选解析后端仍默认关闭，开启时不再要求逐个填写源码路径；既有集成仍可传入 `HX_OS_<PARSER>_SOURCE_DIR`，显式提供的源码树由集成方管理。

GNU libiconv 是唯一的来源例外：其官方 Git 仓库缺少发布包中的生成转换表。因此 `HX_OS_ENABLE_ICONV=ON` 默认通过 FetchContent 获取官方 1.18 发布包，校验 SHA256，缓存于 `deps/.fetchcontent/iconv/`。这样 Windows 和交叉构建不必额外准备转换表生成工具。显式设置 `HX_OS_ICONV_USE_SYSTEM=ON` 才选择系统 iconv；仍支持已有 `HX_OS_ICONV_SOURCE_DIR`。模块保持可选，LGPL 条件不变。

普通构建使用已经准备好的 SDK，不会自动编译第三方库。默认开启的 CRYPTO、ARCHIVE 需要下文的 SDK，TLS 复用它。不需要这些依赖时，同时设置 `HX_OS_ENABLE_CRYPTO=OFF` 和 `HX_OS_ENABLE_ARCHIVE=OFF`；基础 Unicode 和内置配置解析仍可使用。对应入口见[加密](CRYPTO.zh-CN.md)、[TLS](TLS.zh-CN.md)和[压缩](ARCHIVE.zh-CN.md)。

## 3. 备选使用 FetchContent

主工程、`tools/dependencies`、`tools/sevenzip` 均支持 `-DHX_OS_DEPENDENCY_PROVIDER=FETCHCONTENT`，取得相同固定提交，默认缓存于 `deps/.fetchcontent/<名称>/<提交号>`。首次准备需要网络，之后复用缓存；下载失败终止配置。按源码缓存加锁，已准备的源码不会在另一个构建正在编译时被重新展开。

源码目录只读时，可用 `HX_OS_DEPENDENCY_CACHE_DIR` 指向可写缓存。支持 CMake 的 `FETCHCONTENT_SOURCE_DIR_HX_DEP_<名称>`，Git 源码仍需符合固定提交。SUBMODULE 模式不会下载 Git 源码；启用源码版 iconv 时仍可能下载前述官方归档。离线构建前可集中准备：

```sh
cmake -S tools/sources -B build-sources

# 替代来源，版本不变：
cmake -S tools/sources -B build-sources-fetch -DHX_OS_DEPENDENCY_PROVIDER=FETCHCONTENT
```

`HX_OS_DEPENDENCIES` 接受用分号隔开的名称列表，默认准备五种解析器、KCP、uthash、iconv。`tools/fetch_parsers.py` 和 `tools/fetch_iconv.py` 保留为这个 CMake 工程的兼容入口，不改写第三方源码。

## 4. 构建依赖

核心项目仍以 CMake 3.16 为基线。**此可选依赖构建器及其检查工程需要 CMake 3.20**，原因是所选上游 XZ 的构建要求。OpenSSL 还需要 Perl；Windows 默认启用的汇编优化需要 NASM。这些是构建电脑的工具，不是业务程序运行时的依赖。

Linux：

```sh
cmake -S tools/dependencies -B build-deps -DCMAKE_BUILD_TYPE=Release
cmake --build build-deps --parallel 2
```

Windows 在 Visual Studio x64 Developer Command Prompt 中运行。默认优化构建需要安装 NASM 并加入 PATH；也可以显式选择不使用汇编的可移植构建：

```bat
cmake -S tools/dependencies -B build-deps -G "Visual Studio 17 2022" -A x64 -DHX_DEPS_OPENSSL_ASM=OFF
cmake --build build-deps --config Release --parallel 2
```

默认安装位置为 `build-deps/prefix`，可通过 `CMAKE_INSTALL_PREFIX` 指定。构建器在独立目录编译，默认使用 Release 配置：OpenSSL 默认动态链接，压缩依赖静态链接并启用位置无关代码。传入 `HX_DEPS_OPENSSL_SHARED=OFF` 可构建静态 OpenSSL，主库同时选择 `HX_OS_OPENSSL_STATIC=ON`；外部线程需遵守[线程清理约定](CRYPTO.zh-CN.md#5-线程内存与插件卸载)。不构建 OpenSSL 测试套件、旧 provider／engine、归档命令行工具和未选择的压缩后端。`HX_DEPS_JOBS` 限制每个子构建的并行量，外层 `--parallel` 限制同时构建的依赖数量。

MSVC Debug 消费者应为压缩 SDK 指定 `-DHX_DEPS_CONFIGURATION=Debug`。Debug 与 Release SDK 使用不同 prefix，避免混用 `/MDd` 与 `/MD` 静态库；OpenSSL 动态运行库保持独立构建。

Windows libarchive 配置面向 WIN7，并显式链接选定依赖；Windows 7／8.1 系统运行暂缓验证。本次 Windows OpenSSL 使用 ASM=OFF，不能当作汇编性能证据。交叉构建把目标编译器、sysroot、工具链变量、Android ABI/API 传递给各依赖。Android SDK 采用 POSIX 宿主／WSL 和 Linux NDK，因为上游 OpenSSL 使用 POSIX make／Perl；直接从 Windows 宿主构建该 SDK 会明确报错。当前 Android／Linux ARM 结果见[验证记录](VALIDATION.zh-CN.md)，Apple 不在本轮范围。

SDK 保留已弃用的 ENGINE ABI 入口，因为其他 OpenSSL 3 消费者可能引用它们；动态加载的 engine 模块仍不构建。同一进程不要混用不兼容的 OpenSSL 构建。

## 5. 验证实际压缩和密码能力

构建完之后，使用绝对 prefix 路径执行检查：

```sh
cmake -S tools/dependencies/check -B build-deps-check -DCMAKE_PREFIX_PATH=/absolute/project/build-deps/prefix
cmake --build build-deps-check --config Release
ctest --test-dir build-deps-check -C Release --output-on-failure
```

检查程序在内存中写入并读回 ZIP、AES-256 密码 ZIP、普通 7z、TAR、TAR.GZ、TAR.XZ、TAR.Zstd，核对二进制内容、加密标志，并验证 ZIP 错误密码会失败。启用过滤器必须直接成功，不能退回外部命令行工具。这些依赖探针不能代替[压缩教程](ARCHIVE.zh-CN.md)中的公共 API、文件系统与失败路径测试。

默认组合为 libarchive、OpenSSL、zlib、liblzma、zstd。独立可选的 7-Zip 动态后端提供密码和文件名加密，见[构建与部署](ARCHIVE.zh-CN.md#4-7z-的密码与文件名加密)。适配模块同样使用所选依赖来源和固定的 `deps/7zip` 头文件；原生运行库通过 `tools/sevenzip` 单独构建，不属于默认 SDK，也不会隐式复制到核心安装目录。分发时保留实际组件的完整许可证材料。

## 6. Linux ARM 与 Android 交叉构建

以下在 Linux／WSL shell 执行，使用绝对路径，每种目标和 API 单独保留 SDK。请替换成实际板卡 SDK 的编译器及 sysroot。

```sh
cmake -S tools/dependencies -B build-deps-aarch64 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/toolchains/embedded-linux.cmake" \
  -DHX_OS_TARGET_PROCESSOR=aarch64 \
  -DHX_OS_CROSS_PREFIX=aarch64-linux-gnu- -DHX_OS_SYSROOT=/ \
  -DHX_DEPS_OPENSSL_TARGET=linux-aarch64
cmake --build build-deps-aarch64 --parallel 2

cmake -S . -B build-arm64 -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DHX_OS_BUILD_TESTS=ON \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/toolchains/embedded-linux.cmake" \
  -DHX_OS_TARGET_PROCESSOR=aarch64 \
  -DHX_OS_CROSS_PREFIX=aarch64-linux-gnu- -DHX_OS_SYSROOT=/ \
  -DHX_OS_DEPENDENCY_PREFIX="$PWD/build-deps-aarch64/prefix" \
  -DCMAKE_CROSSCOMPILING_EMULATOR="qemu-aarch64;-L;/usr/aarch64-linux-gnu"
cmake --build build-arm64 --parallel 4
ctest --test-dir build-arm64 --output-on-failure
```

本机 ARMv7 hard-float GNU 工具链对应参数为 `armv7`、`arm-linux-gnueabihf-`、OpenSSL `linux-armv4`、`qemu-arm;-L;/usr/arm-linux-gnueabihf`。浮点 ABI 由工具链确定，不由 OpenSSL 目标名推断。QEMU 系统调用转换的限制与库缺陷分别记录，运行这些命令不能替代板卡验收。

```sh
cmake -S tools/dependencies -B build-deps-android-arm64 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-21 \
  -DHX_DEPS_OPENSSL_TARGET=android-arm64
cmake --build build-deps-android-arm64 --parallel 2
```

Android ARMv7 改为 `armeabi-v7a`／`android-arm`；x86_64 原生模拟器改为 `x86_64`／`android-x86_64`。这里 `ANDROID_NDK_ROOT` 指向**构建宿主的 Linux NDK**。准备好的 SDK 也能由 Windows NDK 消费，但必须是相同 ABI/API；CMake 包支持重定位。把 prefix 传给 [Android 脚本](ANDROID.zh-CN.md)，它同时部署核心、实际选择的 `libssl` 和 `libcrypto`。

自有 `openssl-android.conf` 配置使用 OpenSSL 官方支持的 `shlib_variant`。Android 运行库 SONAME 为 `libssl-hx.so`、`libcrypto-hx.so`，避开系统 BoringSSL 和模拟器转译启动器的同名依赖；SDK 仍保留常规链接别名。部署时读取生成的运行库清单，不猜测桌面平台的文件名；脚本部署实际 SONAME 文件，排除与系统 BoringSSL 冲突的链接别名，不修改 OpenSSL 源码。同一 ABI 下，较低最低 API 构建的 SDK 也可供较高最低 API 的应用使用，反过来则不允许。

独立 `tools/sevenzip` 在 POSIX 宿主接受相同工具链、ABI、sysroot 参数，不需要 `HX_DEPS_OPENSSL_TARGET`。Android 构建在该运行库内静态包含 C++ 运行时，线程符号从 Bionic libc 取得。将对应产物 `runtime/7z.so` 传给 `HX_OS_TEST_7ZIP_LIBRARY` 或 Android 的 `--sevenzip-library`。它仍是可选后端，不混入默认压缩 SDK。
