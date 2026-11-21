# Third-Party Notices and Licenses

[简体中文](THIRD_PARTY_NOTICES.zh-CN.md) · [Back to README](README.md)

The `hx_c_platform` core does not require OBS, `hx_c_log`, Qt, or a C++ runtime library. Optional modules integrate the pinned third-party open-source components listed below.

> [!IMPORTANT]
> **First-Party vs. Third-Party Licensing**:
> - The distribution license for newly written first-party code in `hx_c_platform` is pending owner selection before external redistribution.
> - All third-party components under `deps/` remain unmodified and retain their original upstream licenses. When a module is enabled and installed, its license files are automatically installed under `share/hx_c_platform/licenses/`.

---

## 1. Optional C Container Backend (`uthash`)

When `HX_OS_ENABLE_CONTAINERS=ON` (enabled by default), hash maps privately include the unmodified `uthash` 2.4.0 header from `deps/uthash/upstream`. All other container data structures and ownership adapters are first-party code. Setting `HX_OS_ENABLE_CONTAINERS=OFF` removes this dependency completely.

- **Upstream**: https://github.com/troydhanson/uthash/tree/v2.4.0
- **Pinned Commit**: `a49bed0b4abb7dff16c73906dcdc8a9718d582d2`
- **Header SHA256**: `1636295b3b97fe8b7b76677bba2de782b0daef5d5f0fa26c471d39967c505d3b`
- **LICENSE SHA256**: `40f48f89e1d0095bf1c5fb4d00affd74959c3ff77463b672cf7fac547d1d6465`
- **License**: [BSD-1-Clause](deps/uthash/upstream/LICENSE) (installed to `share/hx_c_platform/licenses/uthash-LICENSE`)

Custom allocator hooks and recoverable out-of-memory (OOM) handling are implemented in the first-party adapter without modifying vendor headers, and no `uthash` types appear in the public ABI.

> [!NOTE]
> The repository also retains an unmodified copy of `minicoro` 0.1.3 and its [MIT license](deps/minicoro/v0.1.3/LICENSE) under `deps/minicoro/v0.1.3/` from early evaluation. It is **never built, linked, installed, or exposed**. The coroutine scheduler and POSIX context switching are first-party implementations (Windows uses native OS fibers).

---

## 2. Optional KCP Reliable UDP (`kcp`)

When `HX_OS_ENABLE_KCP=ON` (enabled by default), the unmodified upstream KCP v1 sources are compiled as private object code. Setting `HX_OS_ENABLE_KCP=OFF` omits KCP and makes the corresponding public C entry points return `HX_OS_NOT_SUPPORTED`.

- **Upstream**: https://github.com/skywind3000/kcp/tree/8004f7eba5d1bf33f0691eef5f887f2cd3140cb5
- **Pinned Commit**: `8004f7eba5d1bf33f0691eef5f887f2cd3140cb5`
- **`ikcp.c` SHA256**: `b476f4ae41b555f28e819f3a6f34b7e2307c93e17875b4864a8f5fc217f67f60`
- **`ikcp.h` SHA256**: `f04f978a0893a8209d7b82095d19c38cefcf8f30bc932a85bb7fa53b33f9e76c`
- **`LICENSE` SHA256**: `6587fd02a924b97dbd6758b0573ddec056fb494a84f3709c5aaa33ce1f7f029c`
- **License**: [MIT](deps/kcp/upstream/LICENSE) (installed to `share/hx_c_platform/licenses/kcp-LICENSE`)

The first-party adapter isolates KCP symbols, uses bounded per-instance memory reserves without replacing any process-global allocator, and exposes no KCP types in the public ABI. IPC framing, session management, and multi-threaded coordination are first-party code (ordinary UDP sockets do not pass through KCP; see the [IPC Guide](docs/IPC.md)).

> [!NOTE]
> Active builds use the pinned submodules at `deps/kcp/upstream` and `deps/uthash/upstream` (or the same commits via `FetchContent`). Legacy source snapshots in sibling directories remain inactive. The SHA256 hashes above are computed over the original LF source blobs. See the [Dependency Build Guide](docs/DEPENDENCIES.md).

---

## 3. CMake Compiler Warning Module

[cmake/HxWarnings.cmake](cmake/HxWarnings.cmake) is reused from the neighboring `hx_c_log` project, preserving its copyright notice and OBS commit attribution under the [MIT license](licenses/hx_c_log-MIT.txt). No `hx_c_log` runtime is linked.

---

## 4. Optional Legacy Character Encoding (`GNU libiconv`)

When `HX_OS_ENABLE_ICONV=ON` (disabled by default) is built from source, the build fetches the official GNU libiconv 1.18 release archive:

- **Upstream Project**: https://www.gnu.org/software/libiconv/
- **Release Archive**: https://ftp.gnu.org/pub/gnu/libiconv/libiconv-1.18.tar.gz
- **Archive SHA256**: `3b08f5f4f9b4eb82f151a7040bfd6fe6c6fb922efe4b1659c66ea933276965e8`
- **License**: [LGPL-2.1-or-later](licenses/libiconv-COPYING.LIB)

In source mode, only the library C sources (excluding the `iconv` CLI executable) are compiled with symbol aliases as a private object target inside `hx_c_platform` (including in shared-library builds, rather than producing a separate `libiconv` DLL).

> [!WARNING]
> **LGPL Compliance Notice**:
> Enabling source-built `HX_OS_ENABLE_ICONV=ON` incorporates LGPL object code into the compiled `hx_c_platform` binary. If you redistribute a build with `ICONV` enabled from source, you must satisfy the applicable LGPL terms and source/relinking requirements. Core UTF-8 / UTF-16 / UTF-32 validation and conversion remain available with `HX_OS_ENABLE_ICONV=OFF`; alternatively, pass `HX_OS_ICONV_USE_SYSTEM=ON` to use the host OS's system `iconv`.

---

## 5. Optional Configuration Parser Backends

The built-in configuration parsers require zero third-party libraries. When you explicitly enable any of the full-spec parser backends below, its unmodified upstream source is compiled privately into `hx_c_platform` without exposing vendor headers in the public ABI:

| Component | Pinned Release | License | Upstream Repository |
| --- | --- | --- | --- |
| **cJSON** | `1.7.19` | MIT | https://github.com/DaveGamble/cJSON |
| **libyaml** | `0.2.5` | MIT | https://github.com/yaml/libyaml |
| **Mini-XML** | `4.0.6` | Apache-2.0 (with upstream NOTICE exceptions) | https://github.com/michaelrsweet/mxml |
| **inih** | `r62` | BSD-3-Clause | https://github.com/benhoyt/inih |
| **tomlc17** | `R260821` | MIT | https://github.com/cktan/tomlc17 |

Exact Git commits are pinned in [cmake/HxDependencySources.cmake](cmake/HxDependencySources.cmake) (`tomlc17` compiles as standard C99 despite its name). Enabled backends install their license and `NOTICE` files to `share/hx_c_platform/licenses/`. See [Configuration Formats](docs/CONFIGURATION.md) for backend capabilities and limits.

---

## 6. Optional Linux Desktop Power Runtime

The Linux desktop sleep-inhibit adapter can dynamically load a system-installed `libsystemd` or `libelogind` runtime library. These libraries are not vendored, built, or statically linked by `hx_c_platform`. If you redistribute them in a container or appliance image, follow their upstream licenses ([systemd LGPL-2.1](https://github.com/systemd/systemd/blob/main/LICENSE.LGPL2.1) and [elogind](https://github.com/elogind/elogind)). See [Linux Desktop Power](docs/LINUX_DESKTOP.md).

---

## 7. Cryptography and Archive Dependencies (OpenSSL / libarchive / Codecs)

Native dependencies are built from pinned Git submodules under `deps/` (or identical commits via `FetchContent`). `CRYPTO`, `TLS`, `ARCHIVE`, and the `libcurl` SDK share dynamically linked OpenSSL by default (to omit OpenSSL entirely, disable `CRYPTO`, `TLS`, `ARCHIVE`, `HTTP`, and `URL`; standalone `COMPRESSION` uses only the compression codecs). See the [Dependency Build Guide](docs/DEPENDENCIES.md) and [Archive Guide](docs/ARCHIVE.md).

| Component | Pinned Version | Retained License | Pinned Commit |
| --- | --- | --- | --- |
| **OpenSSL** | `3.5.9` | [Apache-2.0](deps/openssl/LICENSE.txt) | `45e844fa2a14ec92d146bd8f5778ac130b6625fb` |
| **libarchive** | `3.8.9` | [BSD-2-Clause and file-specific permissive notices](deps/libarchive/COPYING) | `27cbc7827172698143e440801fc0ba39ccb4f1f5` |
| **zlib** | `1.3.2` | [Zlib](deps/zlib/LICENSE) | `da607da739fa6047df13e66a2af6b8bec7c2a498` |
| **liblzma (XZ)** | `5.8.4` | [0BSD for liblzma; see COPYING for other upstream files](deps/xz/COPYING) | `d3e650e63c110e830fd5391e7f8b45df0b91d3da` |
| **zstd** | `1.5.7` | [BSD-3-Clause (selected license option)](deps/zstd/LICENSE) | `f8745da6ff1ad1e7bab384bd1f9d742439278e99` |
| **7-Zip (optional)** | `26.04` | [LGPL / BSD / unRAR](deps/7zip/DOC/License.txt), [LGPL text](deps/7zip/DOC/copying.txt), [unRAR terms](deps/7zip/DOC/unRarLicense.txt) | `9128b80e3a471108678e2b3ec3c985861c8d7b0f` |

> [!NOTE]
> The default dependency stack (`tools/dependencies`) builds no LGPL or GPL compression backends. The optional `7-Zip` dynamic backend (used for password-encrypted `.7z` archives and header encryption) is built separately via `tools/sevenzip` and is never included in the default SDK.

---

## 8. SQLite, libcurl, and utf8proc

| Component | Pinned Source & Checksum | License / Attribution |
| --- | --- | --- |
| **SQLite** | Official `3.54.0` amalgamation archive, SHA256 `68e913b0fe8ec6b4e5f391c594a28872884e80d6cd042a1e50c957be46b5aa4d` | [Public Domain Dedication](https://www.sqlite.org/copyright.html) |
| **libcurl** | Tag `curl-8_22_0`, commit `01346829096c61b372692f6dc43ffa778c6caccd` (upstream header reports `8.22.0-DEV`) | [curl License](deps/curl/COPYING) |
| **utf8proc** | `2.12.0`, commit `79cdd5ab40c79afa559f689ffc13b76812dee1ac` | [MIT and Unicode Data License](deps/utf8proc/LICENSE.md) |

When enabled, these libraries can also be called directly through the exported native CMake targets (see [Native Dependencies](docs/NATIVE_DEPENDENCIES.md)). The built-in regular expression engine is a first-party C99 implementation and does not link PCRE2.

---

## 9. Optional Cancellable DNS Resolver (`c-ares`)

When networking is enabled, `HX_OS_ENABLE_CARES` defaults to `ON` and compiles the unmodified `c-ares` 1.34.8 sources under `deps/c-ares` into a position-independent (PIC) static library—requiring no separate DNS shared library.

- **Upstream**: https://github.com/c-ares/c-ares
- **Pinned Version & Commit**: `1.34.8`, commit `c7a3138dcfe3bb0eaaf10c0c24c36dc66dc790ab`
- **License**: [MIT](deps/c-ares/LICENSE.md) (installed to `share/hx_c_platform/licenses/c-ares-LICENSE.md`)

Pass `-DHX_OS_ENABLE_CARES=OFF` to omit `c-ares` and use only the host OS resolver.
