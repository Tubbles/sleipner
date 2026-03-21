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
#   ./ci.sh android     # Build Android arm64 shared library
#   ./ci.sh apk         # Build Android APK (includes conan + gradle)
#
# The toolchain image is built automatically if missing.

set -euo pipefail
cd "$(dirname "$0")"

IMAGE="sleipner-toolchain"
CONTAINER_CMD="${CONTAINER_CMD:-podman}"
SOURCES="engine/src/*.c engine/src/*.h engine/test/*.c"

run() {
    "$CONTAINER_CMD" run --rm -v "$(pwd)":"$(pwd)":Z -w "$(pwd)" \
        -e CONAN_HOME="$(pwd)/build/.conan2" "$IMAGE" "$@"
}

ensure_image() {
    if ! "$CONTAINER_CMD" image inspect "$IMAGE" > /dev/null 2>&1; then
        echo "Building toolchain image..."
        "$CONTAINER_CMD" build -t "$IMAGE" .
    fi
}

conan_profile_setup='conan profile detect --force \
    && sed -i "s/compiler=gcc/compiler=clang/" "$CONAN_HOME/profiles/default" \
    && sed -i "s/compiler.version=.*/compiler.version=22/" "$CONAN_HOME/profiles/default" \
    && sed -i "/compiler.cppstd/d" "$CONAN_HOME/profiles/default" \
    && printf "\n[conf]\ntools.build:compiler_executables={\"c\": \"clang\", \"cpp\": \"clang++\"}\n" >> "$CONAN_HOME/profiles/default" \
    && sed -i "s/\"21\"]/\"21\", \"22\"]/" "$CONAN_HOME/settings.yml"'

conan_setup="$conan_profile_setup && conan install . --output-folder=build --build=missing"

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
    run bash -c "export ASAN_OPTIONS=detect_leaks=1 && \
                 export UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 && \
                 $conan_setup && conan build . && ./build/Release/engine/test/engine_tests"
}

do_lint() {
    echo "=== lint ==="
    mkdir -p build/Release
    run bash -c "$conan_setup && conan build . \
        && cd build/Release && clang-tidy -p . ../../engine/src/*.c ../../engine/test/*.c"
}

do_all() {
    echo "=== all ==="
    mkdir -p build/Release
    run bash -c "export ASAN_OPTIONS=detect_leaks=1 && \
                 export UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 && \
                 $conan_setup && conan build . \
                 && ./build/Release/engine/test/engine_tests \
                 && clang-format --dry-run --Werror $SOURCES \
                 && cd build/Release && clang-tidy -p . ../../engine/src/*.c ../../engine/test/*.c"
}

android_conan_setup="$conan_profile_setup"' && conan install . --output-folder=build/android/arm64-v8a --build=missing \
    -s os=Android -s os.api_level=27 -s arch=armv8 \
    -s compiler=clang -s compiler.version=18 -s compiler.libcxx=c++_static -s compiler.cppstd=17 \
    -s build_type=Release \
    -c tools.android:ndk_path=${ANDROID_HOME}/ndk/28.0.13004108'

do_android() {
    echo "=== android ==="
    mkdir -p build/android/arm64-v8a
    run bash -c "$android_conan_setup \
        && cmake -S android/app/src/main/cpp -B build/android/arm64-v8a/build \
            -DCMAKE_TOOLCHAIN_FILE=\${ANDROID_HOME}/ndk/28.0.13004108/build/cmake/android.toolchain.cmake \
            -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=BOTH \
            -Draylib_DIR=\$(pwd)/build/android/arm64-v8a/build/Release/generators \
            -DANDROID_ABI=arm64-v8a \
            -DANDROID_PLATFORM=android-27 \
            -DANDROID_NDK=\${ANDROID_HOME}/ndk/28.0.13004108 \
            -DCMAKE_BUILD_TYPE=Release \
        && cmake --build build/android/arm64-v8a/build"
}

do_apk() {
    echo "=== apk ==="
    mkdir -p build/android/arm64-v8a
    run bash -c "$android_conan_setup \
        && [ -f android/keystore.jks ] || keytool -genkeypair -v \
            -keystore android/keystore.jks -keyalg RSA -keysize 2048 \
            -validity 10000 -alias sleipner \
            -storepass sleipner -keypass sleipner \
            -dname 'CN=Sleipner,O=Sleipner' \
        && cd android \
        && gradle wrapper --gradle-version 8.11.1 \
        && ./gradlew assembleRelease"
}

ensure_image

case "${1:-all}" in
    format)  do_format ;;
    check)   do_check ;;
    build)   do_build ;;
    test)    do_test ;;
    lint)    do_lint ;;
    all)     do_all ;;
    android) do_android ;;
    apk)     do_apk ;;
    *)
        echo "Usage: $0 {format|check|build|test|lint|all|android|apk}"
        exit 1
        ;;
esac
