# Dockerfile for building FSearch in isolation
# This ensures the build happens in a clean, sandboxed environment

FROM ubuntu:24.04

# Prevent interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

# Install all required build dependencies
RUN apt-get update && apt-get install -y \
    git \
    build-essential \
    meson \
    itstool \
    libtool \
    pkg-config \
    intltool \
    libicu-dev \
    libpcre2-dev \
    libglib2.0-dev \
    libgtk-3-dev \
    libxml2-utils \
    appstream \
    && rm -rf /var/lib/apt/lists/*

# Create a non-root user for building
RUN useradd -m -s /bin/bash builder

# Set up the build directory with proper ownership
WORKDIR /build
RUN chown -R builder:builder /build

# Copy the source code
COPY --chown=builder:builder . .

# Switch to non-root user for building
USER builder

# Configure the build
RUN meson setup builddir

# Build the project
RUN ninja -C builddir

# Run tests to verify the build
RUN ninja -C builddir test

# The built binary will be at: builddir/src/fsearch
# You can copy it out using: docker cp <container>:/build/builddir/src/fsearch .

CMD ["/bin/bash"]
