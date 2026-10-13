# Building FSearch with Docker

This guide explains how to build FSearch in an isolated Docker container, which is useful for:
- Building without installing system dependencies
- Testing builds in a clean environment
- Creating reproducible builds
- Security-conscious users who want to isolate untrusted code compilation

## Quick Start

```bash
# Build the Docker image and compile FSearch
docker build -t fsearch-builder .

# Extract the binary
docker run --name fsearch-build fsearch-builder
docker cp fsearch-build:/build/builddir/src/fsearch ./fsearch
docker rm fsearch-build
```

## Prerequisites

- Docker installed and running
- User added to docker group (or use sudo)

### Installing Docker

**Ubuntu/Debian:**
```bash
sudo apt update
sudo apt install docker.io
sudo systemctl start docker
sudo usermod -aG docker $USER
# Log out and log back in for group changes to take effect
```

**Fedora:**
```bash
sudo dnf install docker
sudo systemctl start docker
sudo usermod -aG docker $USER
```

## Building

### Option 1: Automated Script

Use the provided build script:
```bash
./docker-build.sh
```

This will:
1. Build the Docker image
2. Compile FSearch in the container
3. Run tests
4. Extract the binary to `./fsearch-binary`
5. Clean up temporary containers

### Option 2: Manual Steps

```bash
# 1. Build the Docker image
docker build -t fsearch-builder:latest .

# 2. Run the build
docker run --name fsearch-build fsearch-builder:latest

# 3. Extract the compiled binary
docker cp fsearch-build:/build/builddir/src/fsearch ./fsearch-binary

# 4. (Optional) Extract the entire build directory
docker cp fsearch-build:/build/builddir ./builddir

# 5. Clean up the container
docker rm fsearch-build
```

## Installing the Binary

After extraction:

```bash
# Install to user directory
mkdir -p ~/.local/bin
cp ./fsearch-binary ~/.local/bin/fsearch
chmod +x ~/.local/bin/fsearch

# Or install system-wide
sudo install -Dm755 ./fsearch-binary /usr/local/bin/fsearch
```

## Inspecting the Build Environment

To explore the build environment interactively:

```bash
docker run -it --rm fsearch-builder:latest /bin/bash
```

Inside the container:
```bash
cd /build/builddir
ls -la
./src/fsearch --version
```

## Cleanup

```bash
# Remove the Docker image
docker rmi fsearch-builder:latest

# Remove all unused Docker data
docker system prune -a
```

## Customizing the Build

The Dockerfile supports different base distributions. Edit the first line:

```dockerfile
# For Ubuntu 24.04 (default)
FROM ubuntu:24.04

# For Ubuntu 22.04
FROM ubuntu:22.04

# For Debian
FROM debian:bookworm
```

## Troubleshooting

### Permission Denied

If you get "permission denied" errors:
```bash
sudo usermod -aG docker $USER
newgrp docker
```

### Build Fails

Check the build logs:
```bash
docker build --progress=plain --no-cache -t fsearch-builder:latest .
```

### Missing Dependencies

The Dockerfile includes all required dependencies. If you encounter issues, ensure you're using a supported base image (Ubuntu 22.04+ or Debian Bookworm+).

## Security Considerations

- The build runs as a non-root user inside the container
- The container is isolated from your host system
- Only the final binary is extracted to your host
- You can inspect the Dockerfile to verify all build steps

## Contributing

If you improve the Docker build process, please consider contributing back to the project!
