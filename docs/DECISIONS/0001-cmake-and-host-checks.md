# 0001 — CMake build with a host `make check`

## Context
The SDK templates use qmake. The scope requires `make check` to run everything CI runs
without a phone, SDK or network, with coverage, clang-tidy and `-Werror`.

## Decision
CMake for both the device RPM and the host build, fronted by a Makefile. `libsailfishapp`
is required for the device build (`TUULI_REQUIRE_SAILFISHAPP=ON`) and replaced on the
host by `tests/stubs/sailfishapp/`, so `src/main.cpp` compiles under the same flags
everywhere. The spec calls `cmake` directly instead of `%cmake` because the macro's
behaviour differs between RPM distributions.

## Consequences
`compile_commands.json` drives clang-tidy; coverage instrumentation is a CMake option.
Two build flavours share one file, so install rules are checked by `ci/harbour-check.sh`
rather than by hand.
