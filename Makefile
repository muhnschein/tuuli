# harbour-tuuli developer entry points.
#
# `make check` runs exactly what CI runs: from a clean checkout, without a phone,
# an SDK, or network access. Every target below is also usable on its own.

BUILD ?= build
JOBS ?= $(shell nproc 2>/dev/null || echo 2)
CMAKE_FLAGS ?= -DCMAKE_BUILD_TYPE=Debug -DTUULI_COVERAGE=ON
COVERAGE_MIN ?= 80

CXX_SOURCES := $(shell find src tests -name '*.cpp' -o -name '*.h' | sort)
TS_FILES := translations/harbour-tuuli.ts translations/harbour-tuuli-fi.ts

.PHONY: all configure build test coverage fmt fmt-apply tidy qml-lint packaging-lint \
        harbour-check harbour-selftest lint check translations clean

all: build

configure: $(BUILD)/CMakeCache.txt

$(BUILD)/CMakeCache.txt: CMakeLists.txt src/CMakeLists.txt tests/CMakeLists.txt translations/CMakeLists.txt
	cmake -S . -B $(BUILD) $(CMAKE_FLAGS)

build: configure
	cmake --build $(BUILD) -j $(JOBS)

# Tests run one per process, serially, with no retries (a second-attempt pass is a defect).
test: build
	cd $(BUILD) && ctest --output-on-failure -j 1 --timeout 120

coverage: test
	mkdir -p $(BUILD)/coverage
	gcovr --root . --object-directory $(BUILD) \
	      --filter 'src/' --exclude 'src/main\.cpp' \
	      --print-summary --fail-under-line $(COVERAGE_MIN) \
	      --sonarqube $(BUILD)/coverage/sonar-coverage.xml \
	      --xml $(BUILD)/coverage/cobertura.xml \
	      --html-details $(BUILD)/coverage/index.html

fmt:
	clang-format --dry-run --Werror $(CXX_SOURCES)

fmt-apply:
	clang-format -i $(CXX_SOURCES)

tidy: configure
	ci/clang-tidy.sh $(BUILD)

qml-lint:
	ci/qml-lint.sh

packaging-lint:
	ci/packaging-lint.sh

harbour-check:
	ci/harbour-check.sh

harbour-selftest:
	ci/harbour-check-selftest.sh

lint: fmt qml-lint packaging-lint harbour-check harbour-selftest

check: lint build test coverage tidy
	@echo "check: all gates green"

translations:
	lupdate -no-obsolete -locations none qml src -ts $(TS_FILES)

clean:
	rm -rf $(BUILD)
