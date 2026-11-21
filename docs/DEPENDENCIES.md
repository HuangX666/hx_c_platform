# Third-Party Sources and Optional Native Dependency Builds

[简体中文](DEPENDENCIES.zh-CN.md) · [Back to README](../README.md) · [Third-Party Notices](../THIRD_PARTY_NOTICES.md)

Core `hx_c_platform` modules (files, strings, built-in configuration parsers, containers, thread pools, and basic networking) compile directly from the repository and its submodules. Modules that wrap heavy native libraries—cryptography (OpenSSL), archives and compression (libarchive / zlib / lzma / zstd), HTTP (libcurl), and full Unicode transformations (utf8proc)—use a unified dependency builder (`tools/dependencies`) to produce a prebuilt native SDK prefix.

This guide explains the `deps/` directory layout, Git submodule vs. `FetchContent` source management, and how to build the native dependency stack for desktop, embedded Linux, and Android targets.

---

## 1. `deps/` Directory Layout

All third-party upstream sources live under `deps/` and are **never modified or patched in place**. First-party build adapters, symbol isolation, and wrappers live in `cmake/`, `tools/`, and `src/`, and all generated files and build artifacts are written to Git-ignored build directories:

```text
deps/
  openssl/          Git submodule (OpenSSL 3.5.9)
  libarchive/       Git submodule (libarchive 3.8.9)
  zlib/             Git submodule (zlib 1.3.2)
  xz/               Git submodule (XZ / liblzma 5.8.4)
  zstd/             Git submodule (zstd 1.5.7)
  curl/             Git submodule (libcurl SDK)
  c-ares/           Git submodule (cancellable DNS 1.34.8)
  utf8proc/         Git submodule (Unicode transformations 2.12.0)
  7zip/             Git submodule (optional 7-Zip dynamic backend 26.04)
  cjson/            Git submodule (optional JSON parser backend)
  libyaml/          Git submodule (optional YAML parser backend)
  minixml/          Git submodule (optional XML parser backend)
  inih/             Git submodule (optional INI parser backend)
  tomlc/            Git submodule (optional TOML parser backend)
  kcp/upstream/     Active KCP submodule
  uthash/upstream/  Active uthash submodule
  kcp/<commit>/     Inactive legacy snapshot (not used by the build)
  uthash/v2.4.0/    Inactive legacy snapshot (not used by the build)
  minicoro/v0.1.3/  Retained early evaluation copy (never built)
  .fetchcontent/    Git-ignored FetchContent download cache
  .cache/           Git-ignored legacy release archive cache
```

All pinned versions, Git commit hashes, and release archive SHA256 checksums are defined in a single source of truth: [cmake/HxDependencySources.cmake](../cmake/HxDependencySources.cmake).

---

## 2. Option 1 (Default): Git Submodules

Clone the repository with `--recurse-submodules`, or initialize submodules in an existing checkout:

```sh
git clone --branch dev --recurse-submodules \
  https://github.com/HuangX666/hx_c_platform.git
cd hx_c_platform

# For an existing checkout:
git submodule update --init --recursive
```

Both the main project and `tools/dependencies` verify exact Git commit hashes at configure time. If a submodule is missing or checked out at a different revision, CMake fails immediately rather than silently building against an arbitrary branch tip.

### Two Verified Release-Archive Exceptions (`libiconv` and `SQLite`)

1. **GNU libiconv (`HX_OS_ENABLE_ICONV=ON`, off by default)**: Because the official GNU libiconv Git repository omits pre-generated conversion tables, enabling source-built `ICONV` downloads the official `1.18` release tarball via `FetchContent`, verifies its SHA256 checksum, and caches it under `deps/.fetchcontent/iconv/` (avoiding host table-generation tools on Windows and cross builds). Pass `HX_OS_ICONV_USE_SYSTEM=ON` to use the host OS's system `iconv` instead.
2. **SQLite (`HX_OS_ENABLE_PERSISTENT_QUEUE=ON`, on by default)**: Downloads the official SQLite `3.54.0` amalgamation archive via `FetchContent`, verifies its SHA256 checksum, caches it under `deps/.fetchcontent/sqlite/`, and compiles it into `hx_c_platform::sqlite`.

> [!TIP]
> **Building Without External Native SDKs**:
> If your project does not need OpenSSL, libcurl, libarchive, utf8proc, or SQLite, disable `CRYPTO`, `TLS`, `ARCHIVE`, `COMPRESSION`, `HTTP`, `URL`, `UNICODE`, and `PERSISTENT_QUEUE`, or pass `-C cmake/minimal.cmake` when configuring a fresh build directory. Core UTF-8 validation/conversion and built-in configuration parsers remain available.

---

## 3. Option 2 (Alternative): CMake `FetchContent`

Pass `-DHX_OS_DEPENDENCY_PROVIDER=FETCHCONTENT` when configuring the main project, `tools/dependencies`, or `tools/sevenzip` to fetch the exact same pinned commits into `deps/.fetchcontent/<name>/<commit>` without Git submodules (protected by a per-source lock; use `-DHX_OS_DEPENDENCY_CACHE_DIR=...` when building from a read-only source tree).

To pre-populate sources ahead of an offline build, use `tools/sources`:

```sh
# Verify default submodule sources:
cmake -S tools/sources -B build-sources

# Or download pinned sources via FetchContent:
cmake -S tools/sources -B build-sources-fetch \
  -DHX_OS_DEPENDENCY_PROVIDER=FETCHCONTENT
```

---

## 4. Building the Native Dependency Stack (`tools/dependencies`)

While the core `hx_c_platform` project requires CMake 3.16+, **the optional `tools/dependencies` builder and its verification suite require CMake 3.20+** (required by upstream XZ's CMake build). Building OpenSSL also requires **Perl** on the build host (and **NASM** on `PATH` for the default optimized Windows x64 assembly build).

### 4.1 Linux

```sh
cmake -S tools/dependencies -B build-deps -DCMAKE_BUILD_TYPE=Release
cmake --build build-deps --parallel 2
```

### 4.2 Windows (MSVC x64)

Run inside an **x64 Native Tools Command Prompt for VS 2022** (pass `-DHX_DEPS_OPENSSL_ASM=OFF` if NASM is not installed):

```bat
cmake -S tools/dependencies -B build-deps -G "Visual Studio 17 2022" -A x64 -DHX_DEPS_OPENSSL_ASM=OFF
cmake --build build-deps --config Release --parallel 2
```

### 4.3 SDK Output Prefix and Build Options

- **Default Install Prefix**: `build-deps/prefix` (override with `-DCMAKE_INSTALL_PREFIX=...`). Point the main `hx_c_platform` build at this directory via `-DCMAKE_PREFIX_PATH=/absolute/path/to/build-deps/prefix` or `-DHX_OS_DEPENDENCY_PREFIX=...`.
- **Shared vs. Static Libraries**: By default, OpenSSL, libcurl, and utf8proc build as shared libraries (DLLs / `.so`), while zlib, liblzma, zstd, and libarchive build as position-independent (PIC) static libraries.
- **Static OpenSSL**: Pass `-DHX_DEPS_OPENSSL_SHARED=OFF` to `tools/dependencies` and `-DHX_OS_OPENSSL_STATIC=ON` to the main project (external threads must follow the [OpenSSL thread cleanup contract](CRYPTO.md#5-threads-memory-and-unloading)).
- **MSVC Debug Consumers**: When building an MSVC `Debug` (`/MDd`) application, build a separate debug SDK prefix with `-DHX_DEPS_CONFIGURATION=Debug` so `/MDd` and `/MD` static libraries are never mixed.
- **Parallelism**: `--parallel` limits how many dependency packages build concurrently; `-DHX_DEPS_JOBS=N` limits parallel compile jobs inside each package.

---

## 5. Verifying the Built Dependency SDK

After building `tools/dependencies`, run the self-check suite using the absolute path to your `prefix`:

```sh
cmake -S tools/dependencies/check -B build-deps-check \
  -DCMAKE_PREFIX_PATH=/absolute/project/build-deps/prefix
cmake --build build-deps-check --config Release
ctest --test-dir build-deps-check -C Release --output-on-failure
```

This test suite writes and reads back ZIP, AES-256 encrypted ZIP, standard 7z, TAR, TAR.GZ, TAR.XZ, and TAR.Zstd archives in memory, verifies binary payloads and encryption flags, and confirms that wrong ZIP passwords fail.

> [!NOTE]
> For **password-encrypted `.7z` archives and filename encryption**, build the separate optional `7-Zip` dynamic backend via `tools/sevenzip` as described in [Archive Guide Section 4](ARCHIVE.md#4-password-and-filename-encryption-in-7z).

---

## 6. Cross-Compiling for Linux ARM and Android

Run cross-builds from a Linux or WSL shell and maintain a dedicated SDK prefix per target architecture and API level.

### 6.1 Embedded Linux (AArch64 / ARMv7)

```sh
# 1. Cross-build the AArch64 dependency SDK
cmake -S tools/dependencies -B build-deps-aarch64 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/toolchains/embedded-linux.cmake" \
  -DHX_OS_TARGET_PROCESSOR=aarch64 \
  -DHX_OS_CROSS_PREFIX=aarch64-linux-gnu- -DHX_OS_SYSROOT=/ \
  -DHX_DEPS_OPENSSL_TARGET=linux-aarch64
cmake --build build-deps-aarch64 --parallel 2

# 2. Cross-build hx_c_platform
cmake -S . -B build-arm64 -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/toolchains/embedded-linux.cmake" \
  -DHX_OS_TARGET_PROCESSOR=aarch64 \
  -DHX_OS_CROSS_PREFIX=aarch64-linux-gnu- -DHX_OS_SYSROOT=/ \
  -DHX_OS_DEPENDENCY_PREFIX="$PWD/build-deps-aarch64/prefix" \
  -DCMAKE_CROSSCOMPILING_EMULATOR="qemu-aarch64;-L;/usr/aarch64-linux-gnu"
cmake --build build-arm64 --parallel 4
```

For 32-bit **ARMv7 hard-float**, substitute `armv7`, `arm-linux-gnueabihf-`, OpenSSL target `linux-armv4`, and `qemu-arm;-L;/usr/arm-linux-gnueabihf`.

### 6.2 Android NDK (`arm64-v8a` / `armeabi-v7a`)

Building the Android dependency SDK requires a Linux/WSL host, the Linux Android NDK, `make`, and `Perl` (the resulting relocatable SDK prefix can also be consumed on Windows with the matching Windows NDK):

```sh
hx_android_toolchain="$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake"

cmake -S tools/dependencies -B build-deps-android-arm64 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$hx_android_toolchain" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-21 \
  -DHX_DEPS_OPENSSL_TARGET=android-arm64
cmake --build build-deps-android-arm64 --parallel 2
```

- Use `armeabi-v7a` / `android-arm` for 32-bit ARMv7, or `x86_64` / `android-x86_64` for an x86_64 emulator.
- **Android OpenSSL SONAME Isolation**: `openssl-android.conf` uses OpenSSL's `shlib_variant` support to name the Android runtime libraries `libssl-hx.so` and `libcrypto-hx.so`, preventing collisions with the Android system's built-in BoringSSL libraries. See the [Android Build Guide](ANDROID.md).

---

## 7. Exported Native SDK Targets

When the corresponding modules are enabled, the installed CMake package exports direct native targets alongside `hx_c_platform::hx_c_platform` and `hx_c_platform::cpp` (see [Native Dependencies](NATIVE_DEPENDENCIES.md)):

- Individual targets: `hx_c_platform::sqlite`, `hx_c_platform::openssl`, `hx_c_platform::curl`, `hx_c_platform::utf8proc`, `hx_c_platform::archive`, `hx_c_platform::zlib`, `hx_c_platform::lzma`, `hx_c_platform::zstd`, and `hx_c_platform::cares`.
- Aggregate target: `hx_c_platform::native` (links the core library plus all enabled native SDK targets).

---

## 8. Optional Cancellable DNS (`c-ares`)

Two independent CMake options control `c-ares` integration:

1. **Core Networking (`HX_OS_ENABLE_CARES`, default `ON`)**:
   Compiles the unmodified `deps/c-ares` (1.34.8) sources directly into the core as a PIC static library (no extra DNS DLL) for explicit resolver calls, TCP dialing, TLS, event-loop DNS, and coroutine DNS. Setting `-DHX_OS_ENABLE_CARES=OFF` omits `c-ares` and causes explicit resolver APIs to return `HX_OS_NOT_SUPPORTED`.
2. **`libcurl` HTTP SDK (`HX_DEPS_CURL_CARES`, default `OFF`)**:
   By default, `libcurl` uses the OS resolver (where blocking DNS lookups cannot be interrupted early by HTTP deadlines). To enable deadline-cancellable DNS inside HTTP/HTTPS transfers, reconfigure your existing `build-deps` directory with `HX_DEPS_CURL_CARES=ON` and rebuild `curl`:

   ```sh
   cmake -S tools/dependencies -B build-deps -DHX_DEPS_CURL_CARES=ON
   cmake --build build-deps --config Release --target hx_dep_build_curl
   ```

   This links `c-ares` statically into shared `libcurl` without adding a separate DLL. Rebuild `hx_c_platform`, then call `hx_os_http_use_cancellable_dns()` (or `hx_os_coro_http_use_cancellable_dns()` for coroutines) to verify at runtime that the loaded `libcurl` binary supports cancellable DNS (the default system-DNS `libcurl` build returns `HX_OS_NOT_SUPPORTED`).
