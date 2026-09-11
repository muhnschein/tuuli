# Building

## Toolchain pins

Host (`make check`, CI): Ubuntu 24.04 packages. Qt 5.15.13 (`qtbase5-dev`,
`qtdeclarative5-dev`, `qtdeclarative5-dev-tools`, `qttools5-dev-tools`,
`libqt5sql5-sqlite`, `qml-module-qtquick2`, `qml-module-qtqml`, `qml-module-qtquick-window2`),
GCC 13, clang-format and clang-tidy 18, CMake 3.28, gcovr 7, shellcheck 0.9,
`desktop-file-utils`, `rpm` (for `rpmspec`), `file`, `librsvg2-bin` (icons only).
The exact `apt-get` line is in `.github/workflows/ci.yml`.

Device: the Sailfish SDK 5.2 with the `SailfishOS-<release>-aarch64` target. Older
SDKs produce a `__libc_start_main` version Harbour rejects; the SDK version is a
Harbour rule. Host Qt is 5.15 while the device has Qt 5.6: the static QML tests and
`ci/qml-lint.sh` catch what 5.15 accepts and 5.6 rejects.

## Lints and gates (`make check`)

| Target | What | Fails on |
|---|---|---|
| `fmt` | clang-format, `.clang-format` | any drift (`make fmt-apply` fixes) |
| `qml-lint` | `ci/qml-lint.sh` | qmllint, console calls, pixel counts, Qt 5.6 syntax, untranslated strings, >400 lines |
| `packaging-lint` | `ci/packaging-lint.sh` | spec, desktop entry, shellcheck, translations compile and current, docs references |
| `harbour-check` | `ci/harbour-check.sh` | any Harbour rule not waived |
| `harbour-selftest` | `ci/harbour-check-selftest.sh` | the checker missing a broken rule |
| `build` | CMake, `-Wall -Wextra -Wpedantic -Werror` | warnings |
| `test` | ctest, one process per test, serial, no retries | any failure |
| `coverage` | gcovr over `src/` (excluding `main.cpp`) | line coverage < 80% |
| `tidy` | clang-tidy, `.clang-tidy` | any finding |

Missing tools are SKIP locally and failures in CI (`PACKAGING_LINT_STRICT=1`).

## Test tiers

1. C++ unit tests (`tests/tst_*.cpp`, QtTest) for every model, on temporary directories.
2. QML load tests (`tests/tst_qmlload.cpp`) load the real `qml/` against
   `tests/silica-stubs/` and drive pages by `objectName`. Stubs imitate no layout.
3. Static QML tests (`tests/tst_qmlstatic.cpp`): `Sailfish.WebView` only where §5 allows,
   every `model.<role>` bound by a delegate exists on its model, every singleton member
   referenced exists in C++.
4. Packaging checks (`ci/packaging-lint.sh`).
5. Device smoke test (`TESTING.md`).

## Coverage

`make coverage` writes `build/coverage/sonar-coverage.xml` (SonarQube generic format),
`cobertura.xml` and an HTML report. CI uploads the directory as the `coverage` artifact.
SonarQube Cloud imports the XML; it is a report, not a gate (`make check` decides).

## Device RPM

    sfdk config target=SailfishOS-<release>-aarch64
    sfdk build
    sfdk check -s harbour RPMS/harbour-tuuli-*.aarch64.rpm

The spec keeps `Version: 0.0.0`; CI stamps the tag. `.github/workflows/rpm.yml` runs
the same three steps in the `coderus/sailfishos-platform-sdk` image on tags and manual
dispatch and needs the repository variable `SFOS_RELEASE` (a 5.2 release string).
Without the device SDK the host build links `src/main.cpp` against
`tests/stubs/sailfishapp/` so the entry point still compiles under `-Werror`.
