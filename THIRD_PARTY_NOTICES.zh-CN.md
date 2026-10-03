# 第三方声明

[English](THIRD_PARTY_NOTICES.md)

基础库不要求 OBS、hx_c_log、Qt 或 C++ 运行库。可选模块的私有依赖分别列在下方。

## 可选 C 容器

哈希 map 私有包含 `third_party/uthash/v2.4.0` 下未改动的 uthash 2.4.0 头文件，其他容器表示及所有权适配属于自有代码。 CONTAINERS 默认 ON，不需要外部安装包；OFF 时排除此实现依赖。

- 上游：https://github.com/troydhanson/uthash/tree/v2.4.0
- 标签提交：a49bed0b4abb7dff16c73906dcdc8a9718d582d2
- 头文件 SHA256： 1636295b3b97fe8b7b76677bba2de782b0daef5d5f0fa26c471d39967c505d3b
- LICENSE SHA256： 40f48f89e1d0095bf1c5fb4d00affd74959c3ff77463b672cf7fac547d1d6465
- [保留的 BSD-1-Clause 许可证](third_party/uthash/v2.4.0/LICENSE)

版本表示实际集成源码，不表示上游最新版。分配器钩子及可恢复 OOM 由自有适配文件配置，不修改第三方文件。公共 ABI 不出现 uthash 类型。启用时安装 `share/hx_c_platform/licenses/uthash-LICENSE`。

仓库还保留初期评估时取得的 minicoro 0.1.3 原始源码和 [MIT 许可证](third_party/minicoro/v0.1.3/LICENSE)。它没有参与构建、安装、链接或公共 API。当前协程调度器和 POSIX 上下文切换由本库自研，Windows 使用操作系统原生 fiber；保留的评估副本不用于本模块。

## 可选 KCP 可靠 UDP

`HX_OS_ENABLE_KCP` 默认开启；关闭后不编译此依赖，公共 C 入口明确报告不支持。上游 v1 源码保持原样，编译为私有对象代码。自有适配隔离符号、按实例预留有界内存，不更改进程级分配器。公共 ABI 不出现 KCP 类型；安装时包含 `share/hx_c_platform/licenses/kcp-LICENSE`。

- [固定版本上游源码](https://github.com/skywind3000/kcp/tree/8004f7eba5d1bf33f0691eef5f887f2cd3140cb5)
- 提交：`8004f7eba5d1bf33f0691eef5f887f2cd3140cb5`，v1 分支集成版本，不宣称为最新版。
- `ikcp.c` SHA256：`b476f4ae41b555f28e819f3a6f34b7e2307c93e17875b4864a8f5fc217f67f60`
- `ikcp.h` SHA256：`f04f978a0893a8209d7b82095d19c38cefcf8f30bc932a85bb7fa53b33f9e76c`
- `LICENSE` SHA256：`6587fd02a924b97dbd6758b0573ddec056fb494a84f3709c5aaa33ce1f7f029c`
- [保留的 MIT 许可证](third_party/kcp/8004f7eba5d1bf33f0691eef5f887f2cd3140cb5/LICENSE)

IPC 分帧、会话管理、多线程协调和原生传输属于自有代码。普通 UDP 不经过 KCP，调用和协议见 [IPC 教程](docs/IPC.zh-CN.md)。

## 警告配置

cmake/HxWarnings.cmake 复制自相邻 hx_c_log 项目，保留其版权声明和 OBS 提交引用，适用 [保留的 MIT 许可证](licenses/hx_c_log-MIT.txt)。没有链接 hx_c_log 运行库或日志实现。

## 可选 GNU libiconv

可复现的源码集成固定使用 GNU libiconv 1.18：

- [GNU 项目](https://www.gnu.org/software/libiconv/)
- 压缩包：https://ftp.gnu.org/pub/gnu/libiconv/libiconv-1.18.tar.gz
- SHA256： 3b08f5f4f9b4eb82f151a7040bfd6fe6c6fb922efe4b1659c66ea933276965e8
- [保留的 LGPL 正文](licenses/libiconv-COPYING.LIB)

这是固定的集成版本，不表示最新版本。下载源码保持不变，配置头生成在构建目录。只将库的 C 源码编译为私有对象目标，不构建 iconv 命令行程序；通过符号别名避免与宿主的另一个 iconv 冲突。上游 Windows 指针偏移警告与自有代码的警告即错误策略分开。

源码方式启用后，编码对象代码被包含到 hx_c_platform 中，共享构建也是如此，并非单独交付一个 libiconv DLL。因此产物包含 LGPL 组件；具体分发方式需要对应的许可证、源码／重新链接材料，本次开发安装不是完整的发布合规包。系统 iconv 的许可证和映射表取决于其实际提供方。关闭 ICONV 后仍能使用核心 Unicode 转换。

新编写的 hx_c_platform 自有代码采用何种许可证，仍需由项目所有者在对外分发前确定。

## 可选配置解析器

无第三方依赖的内置语法子集不需要下列组件。启用后，其对象代码私有编入 hx_c_platform，第三方头文件不进入公共 ABI。上游文件保持原样；配置生成、符号别名和调用重定向属于自有构建适配。

| 组件 | 固定版本 | 许可证 | 上游 |
|---|---|---|---|
| cJSON | 1.7.19 | MIT | https://github.com/DaveGamble/cJSON |
| libyaml | 0.2.5 | MIT | https://github.com/yaml/libyaml |
| Mini-XML | 4.0.6 | Apache-2.0 | https://github.com/michaelrsweet/mxml |
| inih | r62 | BSD-3-Clause | https://github.com/benhoyt/inih |
| tomlc17 | R260821 | MIT | https://github.com/cktan/tomlc17 |

可复现的下载地址和 SHA256 记录在 [fetch_parsers.py](tools/fetch_parsers.py)；这些是集成锁定版本，不表示最新版本。 TOMLC 后端使用 tomlc17，所选版本虽名为 c17，仍按 C99 编译。启用组件的原始许可证安装到 share/hx_c_platform/licenses。 Mini-XML 的上游 NOTICE（含可选许可例外）也保持原样安装。解析范围及后端差异见 [CONFIGURATION.zh-CN.md](docs/CONFIGURATION.zh-CN.md)。
