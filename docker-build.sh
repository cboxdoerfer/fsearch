#!/bin/bash
# Docker build script for FSearch
# This script builds FSearch in a sandboxed Docker container

set -e  # Exit on error

echo "========================================="
echo "FSearch Docker Build Script"
echo "========================================="
echo ""

# Check if Docker is installed
if ! command -v docker &> /dev/null; then
    echo "ERROR: Docker is not installed."
    echo "Please install Docker first:"
    echo "  sudo apt install docker.io"
    echo "  sudo usermod -aG docker $USER"
    echo "  # Then log out and log back in"
    exit 1
fi

# Check if user can run Docker
if ! docker ps &> /dev/null; then
    echo "ERROR: Cannot run Docker commands."
    echo "You may need to:"
    echo "  1. Start Docker service: sudo systemctl start docker"
    echo "  2. Add yourself to docker group: sudo usermod -aG docker $USER"
    echo "  3. Log out and log back in"
    exit 1
fi

echo "Step 1: Building Docker image..."
docker build -t fsearch-builder:latest .

echo ""
echo "Step 2: Running build in container..."
docker run --name fsearch-build-temp fsearch-builder:latest

echo ""
echo "Step 3: Extracting built binary..."
docker cp fsearch-build-temp:/build/builddir/src/fsearch ./fsearch-binary

echo ""
echo "Step 4: Cleaning up container..."
docker rm fsearch-build-temp

echo ""
echo "========================================="
echo "Build completed successfully!"
echo "========================================="
echo ""
echo "The built binary is available at: ./fsearch-binary"
echo ""
echo "To inspect the build environment:"
echo "  docker run -it --rm fsearch-builder:latest /bin/bash"
echo ""
echo "To extract other files from the build:"
echo "  docker run --name temp fsearch-builder:latest"
echo "  docker cp temp:/build/builddir/src/fsearch ."
echo "  docker rm temp"
echo ""
echo "To remove the Docker image:"
echo "  docker rmi fsearch-builder:latest"
echo ""
