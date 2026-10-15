# Third-party source and optional dependency builds

[简体中文](DEPENDENCIES.zh-CN.md) · [README](../README.md) · [Licenses](../THIRD_PARTY_NOTICES.md)

## 1. Source layout

Third-party source belongs in `deps/`. First-party integration belongs in `cmake/`, `tools/` and `src/`; generated files and installed dependency artifacts belong in ignored build directories. Upstream files are not patched.

```text
deps/
  openssl/          Git submodule
  libarchive/       Git submodule
  zlib/             Git submodule
  xz/               Git submodule
  zstd/             Git submodule
  curl/             libcurl SDK submodule
  c-ares/           Cancellable DNS submodule (1.34.8)
  utf8proc/         Unicode SDK submodule
  7zip/             Optional native backend submodule
  cjson/            Optional parser submodule
  libyaml/          Optional parser submodule
  minixml/          Optional parser submodule
  inih/             Optional parser submodule
  tomlc/            Optional parser submodule
  kcp/upstream/     Active KCP submodule
  uthash/upstream/  Active uthash submodule
  kcp/<commit>/     Unchanged inactive source snapshot
  uthash/v2.4.0/    Unchanged inactive source snapshot
  minicoro/v0.1.3/  Retained evaluation only; never built
  .fetchcontent/   Ignored alternative source cache
  .cache/          Ignored legacy parser/iconv release cache
```

Old `.deps/` and `deps/.cache/` caches remain usable through explicit source overrides. New builds use one provider in `cmake/HxDependencySources.cmake`; the legacy Python preparation commands delegate to that provider. They no longer maintain separate download/version lists.

## 2. Default: Git submodules

```sh
git clone --branch dev --recurse-submodules \
  https://github.com/HuangX666/hx_c_platform.git
cd hx_c_platform
# For an existing checkout:
git submodule update --init --recursive
```

The main build and optional builders check exact dependency commits. A missing or different revision fails explicitly; a branch tip is never selected silently. The active development branch is `dev`. Submodule URLs are in [.gitmodules](../.gitmodules); all 16 Git pins and the iconv/SQLite archive hashes are in [HxDependencySources.cmake](../cmake/HxDependencySources.cmake).

Default containers and KCP require their initialized submodules. The five parser backends remain opt-in; enabling them no longer requires five source paths. Existing `HX_OS_<PARSER>_SOURCE_DIR` overrides are still supported. An explicitly supplied tree remains the integrator's responsibility.

GNU libiconv is one source-provider exception: its official Git checkout omits generated conversion tables. `HX_OS_ENABLE_ICONV=ON` therefore retrieves the official 1.18 release archive through FetchContent, verifies its SHA256, and reuses `deps/.fetchcontent/iconv/`. This works on Windows and cross builds without requiring host table-generation tools. `HX_OS_ICONV_USE_SYSTEM=ON` explicitly selects system iconv instead; an existing `HX_OS_ICONV_SOURCE_DIR` can still be supplied. The module remains optional and retains its LGPL terms.

SQLite is the other verified release exception: its official 3.54.0 amalgamation archive is downloaded through FetchContent, SHA256-checked and cached under `deps/.fetchcontent/sqlite/`. This avoids adopting an unrelated third-party Git mirror or generating the amalgamation on every host. The PERSISTENT_QUEUE module compiles this unchanged source into its native SQLite target; the installed package includes its matching header/library.

The main build uses a prepared SDK for default-enabled CRYPTO/TLS, ARCHIVE, COMPRESSION, HTTP/URL and UNICODE. The SDK now includes shared libcurl and utf8proc alongside OpenSSL and the static compression codecs. Disable the corresponding switches plus PERSISTENT_QUEUE when these SDKs are unnecessary, or use `cmake/minimal.cmake`. UTF-8 validation/conversion in the core and builtin configuration parsers remain available; full Unicode transformations require utf8proc. Memory compression can remain enabled independently of file archives. See [native access](NATIVE_DEPENDENCIES.md), [everyday tools](UTILITY_EXTENSIONS.md), [cryptography](CRYPTO.md), [TLS](TLS.md) and [archives](ARCHIVE.md).

## 3. Alternative: FetchContent

Use `-DHX_OS_DEPENDENCY_PROVIDER=FETCHCONTENT` in the main project, `tools/dependencies` or `tools/sevenzip`. It retrieves the same fixed commits under `deps/.fetchcontent/<name>/<commit>`. Initial acquisition requires network access; subsequent builds reuse the cache. A failed download stops configuration. A per-source lock serializes initial acquisition, and an already prepared source is not repopulated while another configuration compiles it.

`HX_OS_DEPENDENCY_CACHE_DIR` can point to a separate writable cache, useful with a read-only checkout. CMake's `FETCHCONTENT_SOURCE_DIR_HX_DEP_<NAME>` overrides are accepted; Git checkouts must still match their pin. SUBMODULE does not acquire Git sources during configuration. Enabling source-built GNU iconv or SQLite can download their verified official archives as described above. Prepare sources separately when builds need to be offline:

```sh
cmake -S tools/sources -B build-sources
# Alternative provider, same versions:
cmake -S tools/sources -B build-sources-fetch \
  -DHX_OS_DEPENDENCY_PROVIDER=FETCHCONTENT
```

`HX_OS_DEPENDENCIES` selects a semicolon-separated list; its default includes the five parsers, KCP, uthash and iconv. The Python commands `tools/fetch_parsers.py` and `tools/fetch_iconv.py` are compatibility frontends for this CMake project. Neither rewrites vendor source.

## 4. Build the native dependency stack

The core project keeps CMake 3.16 as its baseline. **This optional builder and its check project need CMake 3.20**, following the selected upstream XZ build. OpenSSL also requires Perl; its optimized Windows assembly path needs NASM. These are build-host tools, not application runtime dependencies.

On Linux:

```sh
cmake -S tools/dependencies -B build-deps -DCMAKE_BUILD_TYPE=Release
cmake --build build-deps --parallel 2
```

On Windows, run in a Visual Studio x64 Developer Command Prompt. Install NASM and ensure it is on PATH for the default optimized build. An explicit portable build is also available:

```bat
cmake -S tools/dependencies -B build-deps -G "Visual Studio 17 2022" -A x64 -DHX_DEPS_OPENSSL_ASM=OFF
cmake --build build-deps --config Release --parallel 2
```

The prefix is `build-deps/prefix` unless `CMAKE_INSTALL_PREFIX` is supplied. The builder uses out-of-source builds, with Release as the default: OpenSSL, libcurl and utf8proc are shared by default; compression libraries are static and position-independent. For static OpenSSL, use `HX_DEPS_OPENSSL_SHARED=OFF` and select `HX_OS_OPENSSL_STATIC=ON` in the main project; external threads must follow the [cleanup contract](CRYPTO.md#5-threads-memory-and-unloading). OpenSSL command-line test suites, legacy providers/engines, curl command-line tools, archive command-line tools and unselected compression backends are not part of this build. `HX_DEPS_JOBS` limits parallel jobs inside sub-builds; the outer `--parallel` option limits concurrently building dependencies.

For an MSVC Debug consumer, configure its compression SDK with `-DHX_DEPS_CONFIGURATION=Debug`. Keep Debug and Release SDKs in separate prefixes; do not mix `/MDd` and `/MD` static libraries. The OpenSSL shared runtime stays independently built.

Windows libarchive configuration targets WIN7 and links selected dependencies explicitly. Cross builds pass the compiler, sysroot, toolchain and Android ABI/API to each dependency. Android SDK builds require a POSIX host/WSL, Linux NDK, make and Perl; a Windows host is rejected explicitly. See [platform support](PLATFORMS.md) for older-system and device compatibility boundaries.

The SDK retains deprecated ENGINE ABI entry points because other OpenSSL 3 consumers can reference them; dynamically loaded engine modules remain disabled. Do not mix incompatible OpenSSL builds in one process.

## 5. Check real compression and encryption support

After the build, use an absolute prefix path:

```sh
cmake -S tools/dependencies/check -B build-deps-check \
  -DCMAKE_PREFIX_PATH=/absolute/project/build-deps/prefix
cmake --build build-deps-check --config Release
ctest --test-dir build-deps-check -C Release --output-on-failure
```

The check writes and reads ZIP, AES-256 password ZIP, ordinary 7z, TAR, TAR.GZ, TAR.XZ and TAR.Zstd in memory. It compares the original binary payload, checks the encrypted flag and verifies that a wrong ZIP password fails. Filter setup must succeed without an external command-line fallback. These dependency probes do not replace the public API, filesystem and failure tests described in [ARCHIVE](ARCHIVE.md).

The default stack uses libarchive, OpenSSL, zlib, liblzma and zstd. The separate optional 7-Zip dynamic backend provides encrypted 7z and filename encryption; see [its build and deployment](ARCHIVE.md#4-password-and-filename-encryption-in-7z). Enabling the adapter also uses the dependency provider and pinned `deps/7zip` headers. Native 7-Zip is built separately with `tools/sevenzip`; it is not part of the default SDK or copied implicitly into the core installation. Preserve the complete notices for every distributed component.

## 6. Cross-build Linux ARM and Android

Use absolute paths below. Keep one SDK prefix per target and API. These examples run from a Linux/WSL shell; replace the SDK/sysroot/compiler paths with the actual target SDK.

```sh
cmake -S tools/dependencies -B build-deps-aarch64 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/toolchains/embedded-linux.cmake" \
  -DHX_OS_TARGET_PROCESSOR=aarch64 \
  -DHX_OS_CROSS_PREFIX=aarch64-linux-gnu- -DHX_OS_SYSROOT=/ \
  -DHX_DEPS_OPENSSL_TARGET=linux-aarch64
cmake --build build-deps-aarch64 --parallel 2

cmake -S . -B build-arm64 -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/toolchains/embedded-linux.cmake" \
  -DHX_OS_TARGET_PROCESSOR=aarch64 \
  -DHX_OS_CROSS_PREFIX=aarch64-linux-gnu- -DHX_OS_SYSROOT=/ \
  -DHX_OS_DEPENDENCY_PREFIX="$PWD/build-deps-aarch64/prefix" \
  -DCMAKE_CROSSCOMPILING_EMULATOR="qemu-aarch64;-L;/usr/aarch64-linux-gnu"
cmake --build build-arm64 --parallel 4
```

For the tested ARMv7 hard-float GNU toolchain, use `armv7`, `arm-linux-gnueabihf-`, OpenSSL `linux-armv4`, and `qemu-arm;-L;/usr/arm-linux-gnueabihf`. The toolchain, not the OpenSSL target name, determines the float ABI. QEMU syscall limitations are reported separately from library defects; running these commands is not board acceptance.

```sh
hx_android_toolchain="$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake"

cmake -S tools/dependencies -B build-deps-android-arm64 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$hx_android_toolchain" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-21 \
  -DHX_DEPS_OPENSSL_TARGET=android-arm64
cmake --build build-deps-android-arm64 --parallel 2
```

Use `armeabi-v7a` / `android-arm` for Android ARMv7, or `x86_64` / `android-x86_64` for a native x86_64 emulator. `ANDROID_NDK_ROOT` here identifies the **Linux NDK on the build host**. The prepared SDK can be consumed from Windows by the Windows NDK for the same ABI/API; its CMake package is relocatable. Pass its prefix to [the Android runner](ANDROID.md), which deploys the selected `libssl` and `libcrypto` runtimes as well as the core.

The first-party `openssl-android.conf` overlay uses OpenSSL's supported `shlib_variant` setting. Android runtime SONAMEs are `libssl-hx.so` and `libcrypto-hx.so`, avoiding the system BoringSSL names and the emulator's native-bridge dependency. The SDK also retains normal linker aliases. Use the generated runtime manifest rather than assuming desktop library names; the runner deploys resolved SONAME files and omits aliases that collide with system BoringSSL. Upstream OpenSSL files remain untouched. An SDK built with a lower minimum API may also serve a higher-minimum-API application of the same ABI; it must never require a newer API than that application.

The separate `tools/sevenzip` helper accepts the same toolchain/ABI/sysroot arguments on POSIX hosts, without `HX_DEPS_OPENSSL_TARGET`. Its Android build statically includes its C++ runtime and links Bionic thread facilities from libc. Point `HX_OS_TEST_7ZIP_LIBRARY` or Android `--sevenzip-library` at that target's `runtime/7z.so`. This is a separate optional native backend, not part of the default compression SDK.

## 7. New SDK deployment details

The fixed curl tag is `curl-8_22_0`; its upstream header reports `8.22.0-DEV`. The original version text is retained. Its build uses OpenSSL, with command-line tools, threaded DNS, libpsl, SSH, IDN2, GSSAPI and implicit content compression disabled. HTTP deadlines therefore cannot interrupt native blocking DNS. URL IDNA availability follows this selected backend; it does not silently add an IDNA dependency. Utf8proc is pinned at 2.12.0. Both licenses and headers are installed with the SDK.

Native CMake aliases include `hx_c_platform::sqlite`, `::openssl`, `::curl`, `::utf8proc`, `::archive`, `::zlib`, `::lzma` and `::zstd` when enabled. `hx_c_platform::native` collects enabled native targets with the core. A consumer can call these SDK APIs directly without replacing the convenience layer. Installed package configuration resolves matching dependencies from its own prefix, and exports the original native headers. Windows deploys runtime DLLs under `bin`; Unix installations retain shared-library name chains and origin-relative runtime lookup. Do not mix incompatible SDK versions in one process.

## 8. Optional cancellable DNS

`HX_OS_ENABLE_CARES` defaults ON when networking is enabled. Its unmodified c-ares 1.34.8 sources build directly with this project as a PIC static library; no separate DNS DLL is required. Initialize `deps/c-ares`, or select FetchContent as above. The fixed commit is `c7a3138dcfe3bb0eaaf10c0c24c36dc66dc790ab`. Set `-DHX_OS_ENABLE_CARES=OFF` to retain system DNS without this backend; explicit resolver entry points then return `NOT_SUPPORTED`.

Installation includes the c-ares headers, static library, CMake package and MIT license. Link `hx_c_platform::cares` for direct native calls. The wrapper leases c-ares global initialization; applications using native APIs must coordinate global cleanup and must not tear down c-ares while a wrapper resolver is alive. This backend serves explicit resolver/TCP-dial/TLS calls; it does not change the separately built libcurl DNS backend. Coroutine HTTP therefore retains its documented native-DNS close limitation.
