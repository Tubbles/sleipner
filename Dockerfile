# Toolchain image: reproducible build environment for Sleipner.
# Uses multi-stage build for smaller final image.
# Contains clang-22, clang-format, clang-tidy, conan, cmake, X11/GL dev libs.
#
# Build the image:
#   podman build -t sleipner-toolchain .
#
# Use via ci.sh for format, build, test, lint steps.

# Stage 1: Builder with full toolchain
FROM debian:bookworm-slim as builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build pkg-config git \
    python3 python3-venv python3-pip \
    wget gnupg lsb-release software-properties-common \
    libgl-dev \
    libx11-dev libx11-xcb-dev libxcb-glx0-dev libxcb-render0-dev \
    libxcb-render-util0-dev libxcb-xkb-dev libxcb-icccm4-dev \
    libxcb-image0-dev libxcb-keysyms1-dev libxcb-randr0-dev \
    libxcb-shape0-dev libxcb-sync-dev libxcb-xfixes0-dev \
    libxcb-xinerama0-dev libxcb-dri3-dev libxcb-cursor-dev \
    libxcb-util-dev \
    && rm -rf /var/lib/apt/lists/*

# Install LLVM 22 via llvm.sh (sets up repo + installs clang)
RUN wget -qO /tmp/llvm.sh https://apt.llvm.org/llvm.sh \
    && chmod +x /tmp/llvm.sh \
    && /tmp/llvm.sh 22 \
    && apt-get install -y --no-install-recommends clang-format-22 clang-tidy-22 \
    && rm -rf /tmp/llvm.sh /var/lib/apt/lists/* \
    && update-alternatives --install /usr/bin/clang clang /usr/bin/clang-22 100 \
    && update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-22 100 \
    && update-alternatives --install /usr/bin/clang-format clang-format /usr/bin/clang-format-22 100 \
    && update-alternatives --install /usr/bin/clang-tidy clang-tidy /usr/bin/clang-tidy-22 100

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

WORKDIR /src

# Install conan in a venv
RUN python3 -m venv /opt/venv
ENV PATH="/opt/venv/bin:$PATH"
RUN pip install --no-cache-dir conan

# Configure conan profile for clang-22
RUN conan profile detect \
    && sed -i 's/compiler=gcc/compiler=clang/' ~/.conan2/profiles/default \
    && sed -i 's/compiler.version=.*/compiler.version=22/' ~/.conan2/profiles/default \
    && sed -i '/compiler.cppstd/d' ~/.conan2/profiles/default \
    && printf '\n[conf]\ntools.build:compiler_executables={"c": "clang", "cpp": "clang++"}\n' >> ~/.conan2/profiles/default \
    && sed -i 's/"21"\]/"21", "22"]/' ~/.conan2/settings.yml

# Stage 2: Minimal runtime image
FROM debian:bookworm-slim

# Copy only essential tools from builder (exclude Android SDK/NDK for smaller size)
COPY --from=builder /opt/venv /opt/venv
COPY --from=builder /usr/bin/clang* /usr/bin/
COPY --from=builder /usr/lib/llvm-22 /usr/lib/llvm-22
COPY --from=builder /usr/lib/x86_64-linux-gnu/libc++.so* /usr/lib/x86_64-linux-gnu/
COPY --from=builder /usr/lib/x86_64-linux-gnu/libstdc++.so* /usr/lib/x86_64-linux-gnu/
COPY --from=builder /opt/gradle-8.11.1 /opt/gradle-8.11.1

ENV PATH="/opt/venv/bin:${PATH}"
ENV LD_LIBRARY_PATH=/usr/lib/llvm-22/lib:/usr/lib/x86_64-linux-gnu

WORKDIR /src
