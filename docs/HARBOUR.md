# Harbour

Harbour is the only distribution channel, so Jolla's rules are build rules.

## Jolla's rules, and how CI gates them

Two checks, kept honest against each other:

| | `ci/harbour-check.sh` | `sfdk check -s harbour` |
|---|---|---|
| Runs | every pull request (`make check`) | when the RPM is built (`rpm` workflow) |
| Reads | source tree | built package |
| Authority | no | **yes** |

`ci/harbour-check.sh` reimplements the logic of Jolla's `rpmvalidation.sh`
(sdk-harbour-rpmvalidator) over the sources: package name, RPM metadata (version,
release, vendor, scriptlets, triggers, provides, dependency types), QML imports against
the allow-list, the desktop file and its `[X-Sailjail]` keys and permissions, install
layout (spec `%files` and CMake destinations), icons, linked libraries, exported
`main()` and link flags, `Requires`, hardcoded paths and the runtime path policy.
`ci/harbour-check-selftest.sh` breaks every one of those rules in a throwaway tree and
asserts the check names it.

`ci/harbour/*.conf` are the validator's allow-lists copied verbatim; `ci/harbour/UPSTREAM`
records the source and the commit. The `rpm` workflow runs the validator itself, from
that commit, on the built package (`ci/harbour-validate-rpm.sh`), so the two checks read
the same rules. The `allow-lists` job in `.github/workflows/ci.yml` warns when upstream
has moved on; `ci/harbour-allowlists-drift.sh --update` refreshes files and pin together.
The lists carry the validator's GPL-2.0-or-later licence; CI reads them as data, the
application never links them.

Anything not in `ci/harbour/waivers.conf` fails, in both checks. A waiver names the
check id (`requires`, `qml-import`, ... for the source check; `rpm-requires`,
`rpm-paths`, ... for the validator's sections), the subject and the message, all as
globs, with the reason as a comment.

## Current waivers

None.

## Deviations from SCOPE.md

- `Requires: sailfish-version >= 5.2.0` is not used: the validator rejects any
  dependency outside `allowed_requires.conf`, and `sailfish-version` is not there. The OS
  floor is carried by the SDK target used to build (Harbour checks the
  `__libc_start_main` version the 5.2 toolchain produces) and by the minimum versions of
  allowed packages in the spec. Verify those minimums against the 5.2.0 release before
  Phase 1 (`DECISIONS/0007-os-floor.md`).

## Sailjail permissions

`harbour-tuuli.desktop`, `[X-Sailjail]`:

| Permission | Why |
|---|---|
| `Internet` | network access for the engine and favicon images |
| `WebView` | Gecko embedding: `/usr/share/mozilla`, the transfer engine for downloads (required for any `Sailfish.WebView` user) |
| `Downloads` | the engine saves downloads to `~/Downloads` |
| `Pictures` | uploading a photo through the platform picker in web forms |
| `Documents` | uploading a document through the platform picker |

`OrganizationName=io.github.muhnschein`, `ApplicationName=tuuli` define the writable
data, cache and config directories; nothing is stored anywhere else. Sharing needs no
permission (part of the `Base` set). Whether the pickers need more than `Pictures` and
`Documents` is SCOPE.md §9 item 5 and is verified on the device smoke test.

## Runtime path policy

Only `QStandardPaths::AppDataLocation`, `AppConfigLocation` and `CacheLocation` are
written; `QSettings` always gets an explicit file path (sailjail-permissions README).
`ci/harbour-check.sh` fails on other standard locations and on `/home/nemo` or
`/home/defaultuser` literals.
