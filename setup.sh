#!/bin/bash
# F87Control — Linux setup script
# Run: chmod +x setup.sh && ./setup.sh

set -e

echo "=== F87Control Setup ==="

# Detect package manager and install dependencies
if command -v apt &>/dev/null; then
    echo "[1/4] Installing dependencies (apt)..."
    sudo apt update
    sudo apt install -y libusb-1.0-0-dev libjson-c-dev cmake build-essential
elif command -v dnf &>/dev/null; then
    echo "[1/4] Installing dependencies (dnf)..."
    sudo dnf install -y libusb1-devel json-c-devel cmake gcc make
elif command -v pacman &>/dev/null; then
    echo "[1/4] Installing dependencies (pacman)..."
    sudo pacman -S --noconfirm libusb json-c cmake base-devel
else
    echo "[1/4] Unknown package manager. Install manually: libusb-1.0-dev, json-c-dev, cmake, build-essential"
    exit 1
fi

# Install udev rules
echo "[2/4] Installing udev rules..."
sudo cp udev/99-f87.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger
echo "  udev rules installed. Replug keyboard if already connected."

# Build
echo "[3/4] Building..."
mkdir -p build
cd build
cmake .. -DBUILD_GUI=OFF -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)

# Test
echo "[4/4] Running tests..."
ctest --output-on-failure

echo ""
echo "=== Setup complete ==="
echo ""
echo "Try these commands:"
echo "  ./build/f87ctl list          # Find keyboard"
echo "  ./build/f87ctl info          # Device info"
echo "  ./build/f87ctl key set-all ff0000  # All keys red"
echo "  ./build/f87ctl key set-all 00ff00  # All keys green"
echo "  ./build/f87ctl key set ESC ff0000  # ESC red"
echo "  ./build/f87ctl raw send \"06 82 01 00 01 00 06\"  # Model query"
