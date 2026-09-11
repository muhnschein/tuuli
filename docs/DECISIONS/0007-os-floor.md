# 0007 — OS floor without `Requires: sailfish-version`

## Context
SCOPE.md §2 asks for `Requires: sailfish-version >= 5.2.0`. Harbour's validator rejects
every `Requires` outside `allowed_requires.conf`, and `sailfish-version` is not listed.

## Decision
Do not add the dependency. The floor is carried by building with the 5.2 SDK target
(Harbour checks the resulting `__libc_start_main` version) and by minimum versions on
allowed packages in the spec.

## Consequences
An older device could install the package from a side channel; Harbour serves it only to
compatible releases. The minimum package versions in the spec must be checked against
the 5.2.0 release notes before Phase 1.
