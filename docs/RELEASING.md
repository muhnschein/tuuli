# Releasing

Semantic versioning, signed tags, `main` always releasable.

1. Update `docs/CHANGELOG.md`: move `[Unreleased]` entries under the new version and date.
2. `make check` green on `main`.
3. Tag: `git tag -s vX.Y.Z -m "tuuli X.Y.Z"` and push the tag.
4. Build: the `rpm` workflow stamps `X.Y.Z` into `rpm/harbour-tuuli.spec`, builds the
   `aarch64` RPM and runs the Harbour validator. Locally the same is
   `sed -i 's/^Version:.*/Version:    X.Y.Z/' rpm/harbour-tuuli.spec && sfdk build`.
5. Validate: `sfdk check -s harbour RPMS/harbour-tuuli-X.Y.Z-1.aarch64.rpm` must pass,
   then the device smoke test in `TESTING.md` on the installed RPM.
6. Submit the RPM at https://harbour.jolla.com with the changelog entry as release notes.
7. After approval, note the Harbour release date in the changelog.

Never re-tag: a rejected submission gets a new patch version.
