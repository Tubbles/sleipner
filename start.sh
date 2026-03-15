#!/bin/bash
set -e
cd "$(dirname "$0")"

# Re-exec self after pull so we always run the latest version.
# This must happen before the tee redirect — process substitution
# with exec causes a forked duplicate when combined with re-exec.
if [ -z "$SLEIPNER_REEXECED" ]; then
    export SLEIPNER_REEXECED=1
    git pull || true
    exec "./start.sh" "$@"
fi

LOGFILE="$(pwd)/tmp/start.log"
mkdir -p "$(pwd)/tmp"
exec > >(tee -a "$LOGFILE") 2>&1
ts() { date +%s.%N; }
T_START=$(ts)
echo "=== start.sh $(date) ==="

# Install system dependencies if missing.
# Check first without sudo; only escalate when packages are actually needed.
PACMAN_DEPS=(
    python python-virtualenv base-devel cmake ninja pkgconf mesa
    libx11 libxcb libxrandr libxinerama libxcursor libxi
    libxext libxfixes libxrender libxcomposite libxdamage
    libxxf86vm libxkbfile libxmu libxpm libxt libxtst libxv
    libxss libxaw libice libsm libfontenc
    xcb-util xcb-util-image xcb-util-keysyms xcb-util-renderutil
    xcb-util-wm xcb-util-cursor xcb-util-errors
    util-linux-libs
)

APT_DEPS=(
    python3-venv build-essential cmake ninja-build pkg-config libgl-dev
    libx11-dev libx11-xcb-dev libfontenc-dev libice-dev libsm-dev
    libxaw7-dev libxcomposite-dev libxcursor-dev libxdamage-dev
    libxext-dev libxfixes-dev libxi-dev libxinerama-dev
    libxkbfile-dev libxmu-dev libxmuu-dev libxpm-dev libxrandr-dev
    libxrender-dev libxres-dev libxss-dev libxt-dev libxtst-dev
    libxv-dev libxxf86vm-dev libxcb-glx0-dev libxcb-render0-dev
    libxcb-render-util0-dev libxcb-xkb-dev libxcb-icccm4-dev
    libxcb-image0-dev libxcb-keysyms1-dev libxcb-randr0-dev
    libxcb-shape0-dev libxcb-sync-dev libxcb-xfixes0-dev
    libxcb-xinerama0-dev libxcb-dri3-dev uuid-dev
    libxcb-cursor-dev libxcb-dri2-0-dev libxcb-present-dev
    libxcb-composite0-dev libxcb-ewmh-dev libxcb-res0-dev
    libxcb-util-dev
)

install_deps() {
    if command -v pacman &>/dev/null; then
        local missing=()
        for pkg in "${PACMAN_DEPS[@]}"; do
            pacman -Qi "$pkg" &>/dev/null || missing+=("$pkg")
        done
        if [ ${#missing[@]} -gt 0 ]; then
            echo "Installing missing packages: ${missing[*]}"
            sudo pacman -S --needed --noconfirm "${missing[@]}"
        else
            echo "All pacman dependencies already installed"
        fi
    elif command -v dpkg &>/dev/null; then
        local missing=()
        for pkg in "${APT_DEPS[@]}"; do
            dpkg -s "$pkg" &>/dev/null 2>&1 || missing+=("$pkg")
        done
        if [ ${#missing[@]} -gt 0 ]; then
            echo "Installing missing packages: ${missing[*]}"
            sudo apt-get install -y "${missing[@]}"
        else
            echo "All apt dependencies already installed"
        fi
    fi
}

# Only build if binary is missing or sources are newer
needs_build() {
    [ ! -f ./build/Release/sleipner ] && return 0
    for f in src/*.c src/*.h CMakeLists.txt conanfile.py Dockerfile; do
        [ "$f" -nt ./build/Release/sleipner ] && return 0
    done
    return 1
}

# Save Steam environment for the game binary, clean it for build tools
SAVED_LD_PRELOAD="${LD_PRELOAD:-}"
SAVED_LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-}"
unset LD_PRELOAD
unset LD_LIBRARY_PATH

echo "LOG: needs_build check at +$(echo "$(ts) - $T_START" | bc)s"
if needs_build; then
    echo "LOG: build needed at +$(echo "$(ts) - $T_START" | bc)s"

    # Prefer docker, fall back to podman, then native build
    CONTAINER_CMD=""
    if command -v docker &>/dev/null; then
        CONTAINER_CMD=docker
    elif command -v podman &>/dev/null; then
        CONTAINER_CMD=podman
    fi

    if [ -n "$CONTAINER_CMD" ]; then
        echo "LOG: building with $CONTAINER_CMD via ci.sh..."
        CONTAINER_CMD="$CONTAINER_CMD" ./ci.sh build
        echo "LOG: container build finished at +$(echo "$(ts) - $T_START" | bc)s"
    else
        echo "LOG: no container runtime, building natively with conan..."

        echo "LOG: installing deps..."
        install_deps || true
        echo "LOG: deps done at +$(echo "$(ts) - $T_START" | bc)s"

        # Set up Python venv for conan
        VENV_DIR="$(pwd)/.venv"
        if [ ! -d "$VENV_DIR" ]; then
            echo "LOG: creating venv at $VENV_DIR"
            python3 -m venv "$VENV_DIR"
        fi
        source "$VENV_DIR/bin/activate"
        echo "LOG: venv activated at +$(echo "$(ts) - $T_START" | bc)s"

        command -v conan &>/dev/null || pip install conan
        conan profile path default &>/dev/null || conan profile detect
        echo "LOG: conan ready at +$(echo "$(ts) - $T_START" | bc)s"

        echo "LOG: conan install starting..."
        conan install . --output-folder=build --build=missing
        echo "LOG: conan install done at +$(echo "$(ts) - $T_START" | bc)s"

        echo "LOG: conan build starting..."
        conan build .
        echo "LOG: conan build done at +$(echo "$(ts) - $T_START" | bc)s"
    fi
else
    echo "LOG: binary up to date, skipping build at +$(echo "$(ts) - $T_START" | bc)s"
fi

# Restore Steam environment for the game binary
export LD_PRELOAD="${SAVED_LD_PRELOAD}"
export LD_LIBRARY_PATH="${SAVED_LD_LIBRARY_PATH}"

echo "LOG: launching sleipner at +$(echo "$(ts) - $T_START" | bc)s"
exec ./build/Release/sleipner
