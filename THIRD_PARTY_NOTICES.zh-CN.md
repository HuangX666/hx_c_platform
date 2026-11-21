# 第三方组件与许可证声明

[English](THIRD_PARTY_NOTICES.md) · [返回首页](README.zh-CN.md)

`hx_c_platform` 基础库本身不依赖 OBS、`hx_c_log`、Qt 或 C++ 运行时库。各可选功能模块在内部集成或引用的第三方开源组件及其固定版本、SHA256 校验值与许可证声明汇总如下。

> [!IMPORTANT]
> **自有代码与第三方组件许可边界**：
> - `hx_c_platform` 自有代码的分发许可证尚未最终确定，对外分发前需由项目所有者明确指定。
> - `deps/` 目录下引用的第三方组件均保持原始上游源码不变，并各自遵循其原始许可证。启用对应模块并安装时，原始许可证文件会自动安装到 `share/hx_c_platform/licenses/` 目录。

---

## 1. 可选 C 容器后端（`uthash`）

开启 `HX_OS_ENABLE_CONTAINERS=ON`（默认开启）时，哈希映射（Hash Map）在内部私有包含 `deps/uthash/upstream` 下未修改的 `uthash` 2.4.0 头文件；其余容器结构与所有权管理均为本库自有实现。关闭该开关即可完全移除此依赖。

- **上游地址**：https://github.com/troydhanson/uthash/tree/v2.4.0
- **固定提交**：`a49bed0b4abb7dff16c73906dcdc8a9718d582d2`
- **头文件 SHA256**：`1636295b3b97fe8b7b76677bba2de782b0daef5d5f0fa26c471d39967c505d3b`
- **LICENSE SHA256**：`40f48f89e1d0095bf1c5fb4d00affd74959c3ff77463b672cf7fac547d1d6465`
- **许可证**：[BSD-1-Clause](deps/uthash/upstream/LICENSE)（安装路径：`share/hx_c_platform/licenses/uthash-LICENSE`）

自定义内存分配器钩子与可恢复的内存不足（OOM）处理均通过本库自有适配层完成，公共 ABI 不暴露任何 `uthash` 符号或类型。

> [!NOTE]
> 仓库 `deps/minicoro/v0.1.3/` 下还保留了一份早期技术评估时的 `minicoro` 0.1.3 源码与 [MIT 许可证](deps/minicoro/v0.1.3/LICENSE)。该副本**不参与任何构建、安装或链接**；本库当前的协程调度器与 POSIX 上下文切换均为自研实现（Windows 端使用操作系统原生 Fiber）。

---

## 2. 可选 KCP 可靠 UDP 传输（`kcp`）

开启 `HX_OS_ENABLE_KCP=ON`（默认开启）时，库内部将未修改的上游 KCP v1 源码编译为私有目标代码；关闭后不再编译该依赖，相关 C 接口将返回 `HX_OS_NOT_SUPPORTED`。

- **上游地址**：https://github.com/skywind3000/kcp/tree/8004f7eba5d1bf33f0691eef5f887f2cd3140cb5
- **固定提交**：`8004f7eba5d1bf33f0691eef5f887f2cd3140cb5`
- **`ikcp.c` SHA256**：`b476f4ae41b555f28e819f3a6f34b7e2307c93e17875b4864a8f5fc217f67f60`
- **`ikcp.h` SHA256**：`f04f978a0893a8209d7b82095d19c38cefcf8f30bc932a85bb7fa53b33f9e76c`
- **`LICENSE` SHA256**：`6587fd02a924b97dbd6758b0573ddec056fb494a84f3709c5aaa33ce1f7f029c`
- **许可证**：[MIT](deps/kcp/upstream/LICENSE)（安装路径：`share/hx_c_platform/licenses/kcp-LICENSE`）

本库自有适配层对 KCP 符号进行了隔离，按实例预留有界内存，不会修改进程级全局分配器，且不在公共 ABI 中暴露 KCP 类型。IPC 消息帧、会话管理与多线程协调均为本库自研实现（普通 UDP 不经过 KCP，详见 [IPC 教程](docs/IPC.zh-CN.md)）。

> [!NOTE]
> 当前构建使用 `deps/kcp/upstream` 与 `deps/uthash/upstream` 子模块（或通过 `FetchContent` 拉取相同固定提交），目录中旧版快照仅作原样保留，不参与构建。上述文件 SHA256 均基于原始 LF 换行格式计算。详见 [依赖构建指南](docs/DEPENDENCIES.zh-CN.md)。

---

## 3. CMake 编译器警告配置模块

[cmake/HxWarnings.cmake](cmake/HxWarnings.cmake) 复用自相邻的 `hx_c_log` 项目，保留了原始版权声明与 OBS 提交引用，遵循 [MIT 许可证](licenses/hx_c_log-MIT.txt)。本项目不链接 `hx_c_log` 运行库。

---

## 4. 可选传统字符编码转换（`GNU libiconv`）

开启 `HX_OS_ENABLE_ICONV=ON`（默认关闭）且使用源码构建模式时，固定拉取 GNU libiconv 1.18 官方发布包：

- **上游项目**：https://www.gnu.org/software/libiconv/
- **官方归档**：https://ftp.gnu.org/pub/gnu/libiconv/libiconv-1.18.tar.gz
- **归档 SHA256**：`3b08f5f4f9b4eb82f151a7040bfd6fe6c6fb922efe4b1659c66ea933276965e8`
- **许可证**：[LGPL-2.1-or-later](licenses/libiconv-COPYING.LIB)

启用源码版 `ICONV` 时，仅将库源码编译为带符号别名的私有对象并直接编入 `hx_c_platform`（包括动态库构建，不会单独生成 `libiconv.dll`）。

> [!WARNING]
> **LGPL 合规提示**：
> 从源码启用 `HX_OS_ENABLE_ICONV=ON` 会使编译产物包含 LGPL 代码，对外分发时需按 LGPL 要求提供相应许可证及重链接所需材料。如果只需要核心 UTF-8 / UTF-16 / UTF-32 校验与转换，保持 `HX_OS_ENABLE_ICONV=OFF` 即可完全不含此组件；也可通过 `HX_OS_ICONV_USE_SYSTEM=ON` 改用目标系统自带的 `iconv` 实现。

---

## 5. 可选配置文件解析器后端

本库内置了零外部依赖的配置语法解析器。当你需要完整规范特性并显式开启以下可选解析器后端时，其未修改的上游源码会被私有编译进 `hx_c_platform`，第三方头文件不会进入公共 ABI：

| 组件名称 | 固定版本 | 许可证 | 上游仓库 |
| --- | --- | --- | --- |
| **cJSON** | `1.7.19` | MIT | https://github.com/DaveGamble/cJSON |
| **libyaml** | `0.2.5` | MIT | https://github.com/yaml/libyaml |
| **Mini-XML** | `4.0.6` | Apache-2.0（含上游 NOTICE 例外） | https://github.com/michaelrsweet/mxml |
| **inih** | `r62` | BSD-3-Clause | https://github.com/benhoyt/inih |
| **tomlc17** | `R260821` | MIT | https://github.com/cktan/tomlc17 |

各组件的固定 Git 提交记录在 [cmake/HxDependencySources.cmake](cmake/HxDependencySources.cmake) 中（`tomlc17` 虽包含 `c17` 名称，所选版本仍按标准 C99 编译）。启用后对应的许可证与 `NOTICE` 文件会安装到 `share/hx_c_platform/licenses/`。内置解析器与外部后端的差异详见 [配置格式说明](docs/CONFIGURATION.zh-CN.md)。

---

## 6. 可选 Linux 桌面电源管理运行库

Linux 桌面防休眠适配层在运行时可按需动态调用系统已安装的 `libsystemd` 或 `libelogind`。本项目不随附、不编译也不静态链接这些系统库；若你在发行版或容器镜像中另行分发它们，请遵循其各自的上游许可证（见 [systemd LGPL-2.1](https://github.com/systemd/systemd/blob/main/LICENSE.LGPL2.1) 与 [elogind](https://github.com/elogind/elogind)）。详见 [Linux 桌面电源管理](docs/LINUX_DESKTOP.zh-CN.md)。

---

## 7. 密码学与归档压缩原生依赖（OpenSSL / libarchive / 压缩库）

原生依赖默认通过 `deps/` 下固定提交的 Git 子模块（或相同提交的 `FetchContent`）构建。`CRYPTO`、`TLS`、`ARCHIVE` 以及 `libcurl` SDK 默认共享动态链接的 OpenSSL（若需完全移除 OpenSSL，需同时关闭 `CRYPTO`、`TLS`、`ARCHIVE`、`HTTP` 与 `URL`；独立的 `COMPRESSION` 内存压缩模块仅依赖压缩编解码库）。详见 [依赖构建指南](docs/DEPENDENCIES.zh-CN.md) 与 [压缩与归档教程](docs/ARCHIVE.zh-CN.md)。

| 组件名称 | 固定版本 | 许可证声明 | 固定 Git 提交 |
| --- | --- | --- | --- |
| **OpenSSL** | `3.5.9` | [Apache-2.0](deps/openssl/LICENSE.txt) | `45e844fa2a14ec92d146bd8f5778ac130b6625fb` |
| **libarchive** | `3.8.9` | [BSD-2-Clause 及文件级宽松许可](deps/libarchive/COPYING) | `27cbc7827172698143e440801fc0ba39ccb4f1f5` |
| **zlib** | `1.3.2` | [Zlib](deps/zlib/LICENSE) | `da607da739fa6047df13e66a2af6b8bec7c2a498` |
| **liblzma (XZ)** | `5.8.4` | [liblzma 采用 0BSD；其余工具文件见 COPYING](deps/xz/COPYING) | `d3e650e63c110e830fd5391e7f8b45df0b91d3da` |
| **zstd** | `1.5.7` | [BSD-3-Clause（所选许可项）](deps/zstd/LICENSE) | `f8745da6ff1ad1e7bab384bd1f9d742439278e99` |
| **7-Zip（可选）** | `26.04` | [LGPL / BSD / unRAR](deps/7zip/DOC/License.txt)、[LGPL 正文](deps/7zip/DOC/copying.txt)、[unRAR 条款](deps/7zip/DOC/unRarLicense.txt) | `9128b80e3a471108678e2b3ec3c985861c8d7b0f` |

> [!NOTE]
> 默认 SDK 构建（`tools/dependencies`）不包含任何 LGPL/GPL 压缩后端。`7-Zip` 动态后端（用于加密 7z 及加密文件名）属于完全独立的可选模块，需通过 `tools/sevenzip` 单独编译，默认不构建也不合入主安装包。

---

## 8. SQLite、libcurl 与 utf8proc

| 组件名称 | 固定版本与校验信息 | 许可证声明 |
| --- | --- | --- |
| **SQLite** | 官方 `3.54.0` Amalgamation 归档，SHA256：`68e913b0fe8ec6b4e5f391c594a28872884e80d6cd042a1e50c957be46b5aa4d` | [公有领域（Public Domain）](https://www.sqlite.org/copyright.html) |
| **libcurl** | 标签 `curl-8_22_0`，提交 `01346829096c61b372692f6dc43ffa778c6caccd`（上游头文件版本号显示为 `8.22.0-DEV`） | [curl 许可证](deps/curl/COPYING) |
| **utf8proc** | `2.12.0`，提交 `79cdd5ab40c79afa559f689ffc13b76812dee1ac` | [MIT 及 Unicode 数据许可声明](deps/utf8proc/LICENSE.md) |

上述组件在启用并安装后，也可通过导出的 CMake 原生依赖目标直接调用其底层头文件（见 [底层依赖调用](docs/NATIVE_DEPENDENCIES.zh-CN.md)）。此外，本库的正则表达式引擎为纯 C99 自研实现，不链接 PCRE2。

---

## 9. 可选可取消 DNS 解析器（`c-ares`）

开启网络模块时，`HX_OS_ENABLE_CARES` 默认开启，直接将 `deps/c-ares` 下未修改的 `c-ares` 1.34.8 源码编译为位置无关（PIC）静态库链接入库，无需额外部署 DNS 动态库。

- **上游地址**：https://github.com/c-ares/c-ares
- **固定版本与提交**：`1.34.8`，提交 `c7a3138dcfe3bb0eaaf10c0c24c36dc66dc790ab`
- **许可证**：[MIT](deps/c-ares/LICENSE.md)（安装路径：`share/hx_c_platform/licenses/c-ares-LICENSE.md`）

传入 `-DHX_OS_ENABLE_CARES=OFF` 可移除该依赖并仅使用系统原生 DNS。
