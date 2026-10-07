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
  kcp/<commit>/     原有第三方文件，内容保持原样
  uthash/v2.4.0/    原有第三方文件，内容保持原样
  minicoro/v0.1.3/  仅保留早期评估副本，不参与构建
  .fetchcontent/   忽略的备选下载缓存
  .cache/          忽略的旧解析器／iconv 发布包缓存
```

原有本地 `.deps/` 缓存继续保留，避免破坏已经配置好的构建目录。之后 `fetch_parsers.py`、`fetch_iconv.py` 默认下载到 `deps/.cache/`，CI 和当前构建说明也改用该位置。历史验证记录中的旧路径按当时事实保留。

## 2. 默认使用 Git submodule

```sh
git clone --recurse-submodules https://github.com/HuangX666/hx_c_platform.git
cd hx_c_platform
# 已有仓库补齐子模块：
git submodule update --init --recursive
```

依赖构建器检查每个源码目录的确切提交号。缺失或版本不同会明确报错，不会自动跟随最新分支。[.gitmodules](../.gitmodules) 记录地址，[HxDependencySources.cmake](../cmake/HxDependencySources.cmake) 记录一致的版本与提交号。

普通构建使用已经准备好的 SDK，不会自动编译第三方库。默认开启的 CRYPTO、ARCHIVE 需要下文的 SDK，TLS 复用它。不需要这些依赖时，同时设置 `HX_OS_ENABLE_CRYPTO=OFF` 和 `HX_OS_ENABLE_ARCHIVE=OFF`；基础 Unicode 和内置配置解析仍可使用。对应入口见[加密](CRYPTO.zh-CN.md)、[TLS](TLS.zh-CN.md)和[压缩](ARCHIVE.zh-CN.md)。

## 3. 备选使用 FetchContent

配置 `tools/dependencies` 时传入 `-DHX_OS_DEPENDENCY_PROVIDER=FETCHCONTENT`，即可下载相同的固定提交，默认位置为 `deps/.fetchcontent/<名称>/<提交号>`。该模式由调用者显式选择，首次下载需要网络，之后复用缓存。下载失败会终止配置。

源码目录只读时，可用 `HX_OS_DEPENDENCY_CACHE_DIR` 指向可写缓存。也支持 CMake 的 `FETCHCONTENT_SOURCE_DIR_HX_DEP_<名称>` 覆盖，但指定的 Git 目录仍须符合固定提交。默认 SUBMODULE 模式不会在配置时自动下载。本轮实际测试了 zlib 的 FetchContent 下载；该次测试的另外四个依赖通过 source override 使用已有目录。

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

Windows 的 libarchive 配置选择 WIN7，并显式连接选定的依赖，但 Windows 7/8.1 运行仍待验证。本轮 Windows OpenSSL 使用 ASM=OFF，不能据此声称验证了汇编性能。交叉编译需要合适的 toolchain 和显式 `HX_DEPS_OPENSSL_TARGET`；只填写这个参数并不代表目标编译器、归档器及 OpenSSL 环境已经配置正确。Android、嵌入式 Linux 和 Apple 的依赖包仍需分别验证。

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
