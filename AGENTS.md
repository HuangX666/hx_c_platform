# Repository Guidelines

## Project Structure & Module Organization

- `include/` contains public C headers; `include/hx/` provides optional C++ wrappers.
- `src/` holds shared implementations; `src/windows/`, `src/posix/`, `src/linux/`, and `src/apple/` contain CMake-selected adapters. Keep reusable logic shared.
- `examples/` contains public usage programs; `benchmarks/` contains opt-in performance tools. Maintainer `tests/` are local and ignored.
- `cmake/` and `tools/` support builds and dependencies; `deps/` contains pinned third-party sources. Vendor files are read-only.
- `docs/` contains paired English/Chinese guides; start with `README.md` and `docs/USAGE.md`.

## Build, Test, and Development Commands

Use CMake 3.16+ and prepare dependencies following [DEPENDENCIES.md](docs/DEPENDENCIES.md). Reuse an existing directory for your toolchain.

```sh
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

For a runnable example, configure with `-DHX_OS_BUILD_EXAMPLES=ON`, build `--target hx_os_hello`, and run `build/bin/hx_os_hello`. Visual Studio uses `--config Release` and `bin/Release/`.

## Coding Style & Naming Conventions

Keep the core C99 and the facade C++11; C++20 coroutines remain opt-in. First-party C++ uses `hx::Result<T>`, never `try`, `catch`, or `throw`.

Follow `.editorconfig` and `.clang-format`: UTF-8 without BOM, LF, eight-column tabs, and 80-column source lines. Separate logical steps with blank lines; keep calls adjacent to error checks. Do not hard-wrap Markdown prose.

Use English comments: `//` for notes and `/** ... */` for multiline contracts, with applicable `\brief`, directed `\param`, `\returns`, `\note`, and `\warning` tags. Document non-obvious contracts once; avoid boilerplate. Preserve required macro markers.

C functions use `lower_snake_case` and prefixes such as `hx_os_`. C++ uses PascalCase classes and camelCase methods. Preserve ABI and ownership; timeout must not free active resources. The library never terminates its host or business threads. Scope warnings to owned targets.

## Testing Guidelines

CTest regressions require private `tests/` sources and `HX_OS_BUILD_TESTS=ON`. 20 daily suites; extended checks require `HX_OS_EXTENDED_TESTS=ON`. Naming: `test_<module>.c`, executable `hx_os_test_<module>`, CTest name `<module>`.

Run affected suites in their existing directory:

```sh
ctest -R '^(config_serialize|codec)$' --output-on-failure
```

Extend existing suites when needed. Prose/comment-only edits normally need no rebuild. Keep regression sources, local configuration, logs and audit records untracked; `docs/local/` is excluded from installation. Report run results briefly in chat. Use scoped diff checks, not routine hash manifests. CI remains disabled; distinguish compilation, emulation and hardware results.

## Commit & Pull Request Guidelines

Develop on `dev`. Use concise messages such as `fix: 修复 INI 序列化`, following existing `feat`, `fix`, `docs`, `build`, and `chore` prefixes. PRs explain behavior and relevant checks; link issues. Update matching English/Chinese documentation when behavior changes.
