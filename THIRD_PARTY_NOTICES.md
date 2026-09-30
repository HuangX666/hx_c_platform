# Third-party notices

[简体中文](THIRD_PARTY_NOTICES.zh-CN.md)

The foundation does not require OBS, hx_c_log, Qt or a C++ runtime.
Optional modules declare their private dependencies below.

## Optional C containers

Hash maps privately include the unmodified uthash 2.4.0 header under
`third_party/uthash/v2.4.0`. The other container representations and ownership
adapters are first-party code. CONTAINERS defaults ON and needs no external
package; OFF removes this implementation dependency from the library.

- Upstream: https://github.com/troydhanson/uthash/tree/v2.4.0
- Tag commit: a49bed0b4abb7dff16c73906dcdc8a9718d582d2
- Header SHA256:
  1636295b3b97fe8b7b76677bba2de782b0daef5d5f0fa26c471d39967c505d3b
- LICENSE SHA256:
  40f48f89e1d0095bf1c5fb4d00affd74959c3ff77463b672cf7fac547d1d6465
- [Retained BSD-1-Clause license](third_party/uthash/v2.4.0/LICENSE)

The pin identifies the integrated source, not the latest upstream version.
Allocation hooks and nonfatal OOM handling are configured in the first-party
adapter; no vendor edit is required. No uthash types enter the public ABI.
Enabled installations include `share/hx_c_platform/licenses/uthash-LICENSE`.

The repository also retains unchanged minicoro 0.1.3 source and its
[MIT license](third_party/minicoro/v0.1.3/LICENSE) from the initial evaluation.
It is not built, installed or exposed by any API; coroutine development is
deferred. Its presence is not a claim of an implemented coroutine module.

## Warning configuration

cmake/HxWarnings.cmake was copied from the neighboring hx_c_log project,
retaining its copyright and OBS commit reference. It is covered by the
[retained MIT license](licenses/hx_c_log-MIT.txt). No hx_c_log runtime or
logging implementation is linked.

## Optional GNU libiconv

The reproducible source integration is pinned to GNU libiconv 1.18:

- [GNU project](https://www.gnu.org/software/libiconv/)
- Archive: https://ftp.gnu.org/pub/gnu/libiconv/libiconv-1.18.tar.gz
- SHA256:
  3b08f5f4f9b4eb82f151a7040bfd6fe6c6fb922efe4b1659c66ea933276965e8
- [Retained LGPL text](licenses/libiconv-COPYING.LIB)

This is a pinned integration version, not a claim that it is the latest release.
The downloaded source remains unchanged. Configuration headers are generated
under the build directory. The library C sources, not the iconv command-line
program, are compiled as a private object target. Symbol aliases prevent
collisions with another host iconv. Upstream Windows pointer-offset warnings
are kept separate from first-party warnings-as-errors.

When enabled from source, codec object code is included inside hx_c_platform,
including shared builds; it is not delivered as a separate libiconv DLL.
The resulting distribution therefore includes an LGPL component. Its license
and appropriate source/relinking materials must be considered for the actual
distribution mode; this development installation is not a complete release
compliance package. System iconv has the selected provider's license and
encoding tables. Core Unicode conversion remains usable with ICONV=OFF.

The license for newly written first-party hx_c_platform code remains an owner
decision before external distribution.

## Optional configuration parsers

The dependency-free builtin profiles need none of these components.
Enabled components are compiled as private object code inside hx_c_platform.
No vendor headers enter the public ABI. Upstream files are not modified;
generated configuration, symbol aliases and call redirection are first-party
build integration.

| Component | Pinned release | License | Upstream |
|---|---|---|---|
| cJSON | 1.7.19 | MIT | https://github.com/DaveGamble/cJSON |
| libyaml | 0.2.5 | MIT | https://github.com/yaml/libyaml |
| Mini-XML | 4.0.6 | Apache-2.0 | https://github.com/michaelrsweet/mxml |
| inih | r62 | BSD-3-Clause | https://github.com/benhoyt/inih |
| tomlc17 | R260821 | MIT | https://github.com/cktan/tomlc17 |

The reproducible URLs and archive SHA256 values are recorded in
[fetch_parsers.py](tools/fetch_parsers.py). They are integration pins, not
claims of being the latest releases. tomlc17 supplies the TOMLC backend;
the selected version compiles as C99 despite its name. Enabled components'
unaltered license files are installed under share/hx_c_platform/licenses.
Mini-XML's upstream NOTICE, including its optional license exceptions, is
also installed unchanged.
The parsing restrictions and backend differences are documented in
[CONFIGURATION.md](docs/CONFIGURATION.md).
