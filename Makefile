# harbour-tuuli -- developer targets.
#
# `make check` is what CI runs, minus what needs the Sailfish SDK or a
# phone.  It is not entirely offline: `msrv` fetches a toolchain the first
# time it runs, and `vendor-check` fetches a crate tarball to compare with.
#
# Qt5 packages (Debian/Ubuntu):
#   apt install qtbase5-dev qtdeclarative5-dev qt5-qmake libqt5opengl5-dev \
#               qtdeclarative5-dev-tools qttools5-dev-tools qml-module-qtquick2 \
#               g++ pkg-config rpm desktop-file-utils shellcheck file binutils
# The render-path smoke test also wants xvfb and Mesa (`make smoke`).

.PHONY: check test lint fmt msrv lockfile-lint qml-lint packaging-lint harbour \
        vendor-check smoke translations clean

CARGO ?= cargo
# The Qt layer's smoke test drives a real Qt event loop headless.
export QT_QPA_PLATFORM = offscreen

## What CI runs, in the same order.  Keep in step with .github/workflows/ci.yml.
check: fmt lint test msrv lockfile-lint qml-lint packaging-lint harbour vendor-check

## The core's unit tests and the Qt layer's headless smoke test.
test:
	$(CARGO) test --workspace

## Clippy with warnings denied, over tests and binaries too.
lint:
	$(CARGO) clippy --workspace --all-targets -- -D warnings

fmt:
	$(CARGO) fmt --all --check

## Compile against the toolchain floor the Sailfish SDK ships.  Clippy on
## a modern toolchain does not catch newer std methods; only a real 1.75
## build proves the device still builds.
msrv:
	rustup toolchain install 1.75.0 --profile minimal
	$(CARGO) +1.75.0 check --workspace --all-targets

## Cargo.lock must stay v3: the SDK's cargo 1.75 cannot read v4.
lockfile-lint:
	./ci/check-lockfile.sh

## Parse every .qml file.
qml-lint:
	./ci/qml-lint.sh

## The spec parses, the desktop entry is valid, the icons are current, the
## shell scripts are clean.  A missing tool is a SKIP here and a failure in
## CI (PACKAGING_LINT_STRICT=1).
packaging-lint:
	./ci/packaging-lint.sh

## What Harbour would reject, read off the sources; `sfdk check -s harbour`
## on a built RPM is the authority (docs/HARBOUR.md).  Wants a built binary
## (`cargo build -p tuuli-browser`) for its last checks; without one it says
## so rather than passing quietly.
harbour:
	./ci/harbour-check.sh
	./ci/harbour-check-selftest.sh

## third_party/qmetaobject is upstream plus one three-line patch, and this
## proves it (network: fetches the crates.io tarball).
vendor-check:
	./ci/vendor-check.sh

## Regenerate the catalog from the QML, and compile it beside the .ts so a
## source-tree run (TUULI_TRANSLATIONS_DIR=translations) shows real text.
translations:
	./scripts/update-translations.sh
	./scripts/release-translations.sh

## The FBO render path under Xvfb + Mesa.
smoke:
	xvfb-run -a -s "-screen 0 1080x2260x24" env QT_QPA_PLATFORM=xcb LIBGL_ALWAYS_SOFTWARE=1 \
		$(CARGO) test -p tuuli-browser --test smoke -- --nocapture

clean:
	$(CARGO) clean
