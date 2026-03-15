#!/bin/bash
# CI utility script — runs build steps inside the sleipner-toolchain container.
#
# Usage:
#   ./ci.sh format      # Auto-format source files in-place
#   ./ci.sh check       # Check formatting (dry-run, fails on violations)
#   ./ci.sh build       # Install deps + compile
#   ./ci.sh test        # Run unit tests (builds first if needed)
#   ./ci.sh lint        # Run clang-tidy (builds first if needed)
#   ./ci.sh all         # check + build + test + lint
#
# The toolchain image is built automatically if missing.

set -euo pipefail
cd "$(dirname "$0")"

IMAGE="sleipner-toolchain"
CONTAINER_CMD="${CONTAINER_CMD:-podman}"
SOURCES="src/*.c src/*.h engine/src/*.c engine/src/*.h engine/test/*.c"

run() {
    "$CONTAINER_CMD" run --rm -v "$(pwd)":"$(pwd)":Z -w "$(pwd)" "$IMAGE" "$@"
}

ensure_image() {
    if ! "$CONTAINER_CMD" image exists "$IMAGE" 2>/dev/null; then
        echo "Building toolchain image..."
        "$CONTAINER_CMD" build -t "$IMAGE" .
    fi
}

conan_setup='conan install . --output-folder=build --build=missing'

do_format() {
    echo "=== format ==="
    run clang-format -i $SOURCES
}

do_check() {
    echo "=== check ==="
    run clang-format --dry-run --Werror $SOURCES
}

do_build() {
    echo "=== build ==="
    mkdir -p build/Release
    run bash -c "$conan_setup && conan build ."
}

do_test() {
    echo "=== test ==="
    mkdir -p build/Release
    run bash -c "$conan_setup && conan build . && ./build/Release/engine/test/engine_tests"
}

do_lint() {
    echo "=== lint ==="
    mkdir -p build/Release
    run bash -c "$conan_setup && conan build . && cd build/Release && clang-tidy -p . ../../src/*.c ../../engine/src/*.c"
}

do_all() {
    echo "=== all ==="
    mkdir -p build/Release
    run bash -c "$conan_setup && conan build . \
        && ./build/Release/engine/test/engine_tests \
        && clang-format --dry-run --Werror $SOURCES \
        && cd build/Release && clang-tidy -p . ../../src/*.c ../../engine/src/*.c"
}

ensure_image

case "${1:-all}" in
    format) do_format ;;
    check)  do_check ;;
    build)  do_build ;;
    test)   do_test ;;
    lint)   do_lint ;;
    all)    do_all ;;
    *)
        echo "Usage: $0 {format|check|build|test|lint|all}"
        exit 1
        ;;
esac
