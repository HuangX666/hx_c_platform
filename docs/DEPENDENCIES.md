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
  7zip/             Optional native backend submodule
  kcp/<commit>/     Existing unchanged vendor files
  uthash/v2.4.0/    Existing unchanged vendor files
  minicoro/v0.1.3/  Retained evaluation only; never built
  .fetchcontent/   Ignored alternative source cache
  .cache/          Ignored legacy parser/iconv release cache
```

Old local `.deps/` caches are retained so existing CMake build trees can still use their previously selected paths. New `fetch_parsers.py` / `fetch_iconv.py` downloads go to `deps/.cache/`; the CI and current build instructions use that path. Historical verification records retain their original paths.

## 2. Default: Git submodules

```sh
git clone --recurse-submodules https://github.com/HuangX666/hx_c_platform.git
cd hx_c_platform
# For an existing checkout:
git submodule update --init --recursive
```

The optional dependency builder checks the exact commit of each source checkout. It fails on a missing or different revision and never silently follows a branch tip. Submodule URLs are in [.gitmodules](../.gitmodules); matching versions and revisions are in [HxDependencySources.cmake](../cmake/HxDependencySources.cmake).

The ordinary build uses a prepared SDK instead of automatically compiling third parties. Default-enabled CRYPTO and ARCHIVE need the SDK below; TLS reuses it. Set both `HX_OS_ENABLE_CRYPTO=OFF` and `HX_OS_ENABLE_ARCHIVE=OFF` for a build without OpenSSL/compression dependencies. Existing dependency-free Unicode and builtin parsers still work. See [cryptography](CRYPTO.md), [TLS](TLS.md) and [archives](ARCHIVE.md).

## 3. Alternative: FetchContent

Use `-DHX_OS_DEPENDENCY_PROVIDER=FETCHCONTENT` when configuring `tools/dependencies`. It fetches the same fixed commits, by default under `deps/.fetchcontent/<name>/<commit>`. Downloads are opt-in through this provider and require network access; subsequent builds reuse the cache. A failed download stops configuration.

`HX_OS_DEPENDENCY_CACHE_DIR` can point to a separate writable cache, useful when the checkout is read-only. CMake's `FETCHCONTENT_SOURCE_DIR_HX_DEP_<NAME>` overrides are accepted, but the selected checkout must still match the pin. The SUBMODULE default does not download source during configuration. FetchContent source population was exercised using zlib; the other four checkouts were supplied through source overrides in that provider test.

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

The prefix is `build-deps/prefix` unless `CMAKE_INSTALL_PREFIX` is supplied. The builder uses out-of-source builds, with Release as the default: OpenSSL is shared by default, while compression libraries are static and position-independent. For static OpenSSL, use `HX_DEPS_OPENSSL_SHARED=OFF` and select `HX_OS_OPENSSL_STATIC=ON` in the main project; external threads must follow the [cleanup contract](CRYPTO.md#5-threads-memory-and-unloading). OpenSSL command-line test suites, legacy providers/engines, archive command-line tools and unselected compression backends are not part of this build. `HX_DEPS_JOBS` limits parallel jobs inside sub-builds; the outer `--parallel` option limits concurrently building dependencies.

For an MSVC Debug consumer, configure its compression SDK with `-DHX_DEPS_CONFIGURATION=Debug`. Keep Debug and Release SDKs in separate prefixes; do not mix `/MDd` and `/MD` static libraries. The OpenSSL shared runtime stays independently built.

Windows libarchive configuration targets WIN7 and links the selected dependencies explicitly. Actual Windows 7/8.1 execution remains unverified. The tested Windows OpenSSL configuration used ASM=OFF; its results are not evidence of assembly performance. Cross-compilation needs a suitable toolchain and explicit `HX_DEPS_OPENSSL_TARGET`; passing that option alone is not evidence that the target compiler/archiver/OpenSSL environment has been configured correctly. Android, embedded Linux and Apple dependency packages still need their own build/runtime validation.

The SDK retains deprecated ENGINE ABI entry points because other OpenSSL 3 consumers can reference them; dynamically loaded engine modules remain disabled. Do not mix incompatible OpenSSL builds in one process.

## 5. Check real compression and encryption support

After the build, use an absolute prefix path:

```sh
cmake -S tools/dependencies/check -B build-deps-check -DCMAKE_PREFIX_PATH=/absolute/project/build-deps/prefix
cmake --build build-deps-check --config Release
ctest --test-dir build-deps-check -C Release --output-on-failure
```

The check writes and reads ZIP, AES-256 password ZIP, ordinary 7z, TAR, TAR.GZ, TAR.XZ and TAR.Zstd in memory. It compares the original binary payload, checks the encrypted flag and verifies that a wrong ZIP password fails. Filter setup must succeed without an external command-line fallback. These dependency probes do not replace the public API, filesystem and failure tests described in [ARCHIVE](ARCHIVE.md).

The default stack uses libarchive, OpenSSL, zlib, liblzma and zstd. The separate optional 7-Zip dynamic backend provides encrypted 7z and filename encryption; see [its build and deployment](ARCHIVE.md#4-password-and-filename-encryption-in-7z). Enabling the adapter also uses the dependency provider and pinned `deps/7zip` headers. Native 7-Zip is built separately with `tools/sevenzip`; it is not part of the default SDK or copied implicitly into the core installation. Preserve the complete notices for every distributed component.
