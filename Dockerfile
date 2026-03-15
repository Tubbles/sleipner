# Toolchain image: reproducible build environment for Sleipner.
# Contains clang-22, clang-format, clang-tidy, conan, cmake, and all X11/GL dev libs.
#
# Build the image:
#   podman build -t sleipner-toolchain .
#
# Use via ci.sh for format, build, test, lint steps.

FROM debian:bookworm

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build pkg-config \
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
