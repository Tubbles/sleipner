# Toolchain image: reproducible build environment for Sleipner.
# Contains clang-22, clang-format, clang-tidy, conan, cmake, X11/GL dev libs,
# JDK 17, and Android SDK/NDK for cross-compilation.
#
# Build the image:
#   podman build -t sleipner-toolchain .
#
# Use via ci.sh for format, build, test, lint steps.

FROM debian:bookworm

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build pkg-config git \
    python3 python3-venv python3-pip \
    wget gnupg lsb-release software-properties-common \
    libgl-dev \
    libx11-dev libx11-xcb-dev libfontenc-dev libice-dev libsm-dev \
    libxaw7-dev libxcomposite-dev libxcursor-dev libxdamage-dev \
    libxext-dev libxfixes-dev libxi-dev libxinerama-dev \
    libxkbfile-dev libxmu-dev libxmuu-dev libxpm-dev libxrandr-dev \
    libxrender-dev libxres-dev libxss-dev libxt-dev libxtst-dev \
    libxv-dev libxxf86vm-dev libxcb-glx0-dev libxcb-render0-dev \
    libxcb-render-util0-dev libxcb-xkb-dev libxcb-icccm4-dev \
    libxcb-image0-dev libxcb-keysyms1-dev libxcb-randr0-dev \
    libxcb-shape0-dev libxcb-sync-dev libxcb-xfixes0-dev \
    libxcb-xinerama0-dev libxcb-dri3-dev uuid-dev \
    libxcb-cursor-dev libxcb-dri2-0-dev libxcb-present-dev \
    libxcb-composite0-dev libxcb-ewmh-dev libxcb-res0-dev \
    libxcb-util-dev \
    gcc-mingw-w64-x86-64 \
    && rm -rf /var/lib/apt/lists/*

# Install LLVM 22 via llvm.sh (sets up repo + installs clang)
RUN wget -qO /tmp/llvm.sh https://apt.llvm.org/llvm.sh \
    && chmod +x /tmp/llvm.sh \
    && /tmp/llvm.sh 22 \
    && apt-get install -y --no-install-recommends clang-format-22 clang-tidy-22 lld-22 \
    && rm -rf /tmp/llvm.sh /var/lib/apt/lists/* \
    && update-alternatives --install /usr/bin/clang clang /usr/bin/clang-22 100 \
    && update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-22 100 \
    && update-alternatives --install /usr/bin/clang-format clang-format /usr/bin/clang-format-22 100 \
    && update-alternatives --install /usr/bin/clang-tidy clang-tidy /usr/bin/clang-tidy-22 100 \
    && update-alternatives --install /usr/bin/ld.lld ld.lld /usr/bin/ld.lld-22 100 \
    && update-alternatives --install /usr/bin/llvm-ar llvm-ar /usr/bin/llvm-ar-22 100 \
    && update-alternatives --install /usr/bin/llvm-ranlib llvm-ranlib /usr/bin/llvm-ranlib-22 100

# Install JDK 21 and Android SDK/NDK for Android builds
RUN apt-get update && apt-get install -y --no-install-recommends \
    openjdk-17-jdk-headless unzip \
    && rm -rf /var/lib/apt/lists/*

ENV ANDROID_HOME=/opt/android-sdk
ENV PATH="${ANDROID_HOME}/cmdline-tools/latest/bin:${ANDROID_HOME}/platform-tools:${PATH}"

RUN mkdir -p "${ANDROID_HOME}/cmdline-tools" \
    && wget -qO /tmp/cmdline-tools.zip https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip \
    && unzip -q /tmp/cmdline-tools.zip -d "${ANDROID_HOME}/cmdline-tools" \
    && mv "${ANDROID_HOME}/cmdline-tools/cmdline-tools" "${ANDROID_HOME}/cmdline-tools/latest" \
    && rm /tmp/cmdline-tools.zip \
    && yes | sdkmanager --licenses > /dev/null 2>&1 \
    && sdkmanager "platform-tools" "platforms;android-35" "ndk;28.0.13004108" "cmake;3.22.1"

# Install Gradle
RUN wget -qO /tmp/gradle.zip https://services.gradle.org/distributions/gradle-8.11.1-bin.zip \
    && unzip -q /tmp/gradle.zip -d /opt \
    && rm /tmp/gradle.zip
ENV PATH="/opt/gradle-8.11.1/bin:${PATH}"

# Build cppcheck 2.20.0 from source
RUN wget -qO /tmp/cppcheck.tar.gz https://github.com/danmar/cppcheck/archive/refs/tags/2.20.0.tar.gz \
    && tar xzf /tmp/cppcheck.tar.gz -C /tmp \
    && cmake -S /tmp/cppcheck-2.20.0 -B /tmp/cppcheck-build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build /tmp/cppcheck-build -j"$(nproc)" \
    && cmake --install /tmp/cppcheck-build \
    && rm -rf /tmp/cppcheck.tar.gz /tmp/cppcheck-2.20.0 /tmp/cppcheck-build

# Install conan and pytest in a venv
RUN python3 -m venv /opt/venv
ENV PATH="/opt/venv/bin:$PATH"
RUN pip install --no-cache-dir conan pytest
