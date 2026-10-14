# Repository Guidelines

## Project Structure & Module Organization

- `include/` contains public C headers; `include/hx/` provides optional C++ wrappers.
- `src/` holds shared implementations; `src/windows/`, `src/posix/`, `src/linux/`, and `src/apple/` contain CMake-selected adapters. Keep reusable logic shared.
- `examples/` contains public usage programs; `benchmarks/` contains opt-in performance tools. Maintainer `tests/` are local and ignored.
- `cmake/` and `tools/` support builds and dependencies; `deps/` contains pinned third-party sources. Never modify vendor files.
- `docs/` contains paired English/Chinese guides; start with `README.md` and `docs/USAGE.md`.

## Build, Test, and Development Commands

Use CMake 3.16+ and prepare dependencies following [DEPENDENCIES.md](docs/DEPENDENCIES.md). Reuse an existing directory for the matching toolchain; `build` below is an example path.

```sh
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

These initialize pinned dependencies, configure, and compile. New configurations build only the library. For a runnable example, configure with `-DHX_OS_BUILD_EXAMPLES=ON`, build `--target hx_os_hello`, and run `build/bin/hx_os_hello`. Visual Studio builds use `--config Release` and `bin/Release/` executables.

## Coding Style & Naming Conventions

Keep the core C99 and the optional facade C++11; C++20 coroutines remain opt-in. First-party C++ uses `hx::Result<T>`, never `try`, `catch`, or `throw`.

Follow `.editorconfig` and `.clang-format`: UTF-8 without BOM, LF, eight-column tabs, and 80-column source lines. Separate logical steps with blank lines; keep calls adjacent to error checks. Use English comments for non-obvious contracts. Markdown prose need not be hard-wrapped.

All C functions use `lower_snake_case` and existing module prefixes such as `hx_os_`. C++ classes use PascalCase and methods camelCase. Preserve public ABI and ownership contracts; timeout must not free resources still in use. Scope warnings to owned targets.

## Testing Guidelines

CTest regressions require private `tests/` sources and `HX_OS_BUILD_TESTS=ON`. The daily profile contains 20 suites; additional checks require `HX_OS_EXTENDED_TESTS=ON`. Naming: `test_<module>.c`, executable `hx_os_test_<module>`, CTest name `<module>`.

Run affected suites from the existing build directory, for example:

```sh
ctest -R '^(config_serialize|codec)$' --output-on-failure
```

Add meaningful coverage within existing suites. Documentation-only edits need no rebuild. Keep regressions, local configuration, and build artifacts untracked; remote CI remains disabled. Distinguish compiled, emulated, and hardware-tested results.

## Commit & Pull Request Guidelines

Develop on `dev`. Use concise messages such as `fix: 修复 INI 序列化`, following existing `feat`, `fix`, `docs`, `build`, and `chore` prefixes. Describe the problem, changed behavior, and focused validation in PRs; link relevant issues. Update matching English/Chinese documentation when behavior changes.
