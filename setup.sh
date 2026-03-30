#!/bin/bash
# AULA RGB Controller — Linux setup script
# Run: chmod +x setup.sh && ./setup.sh

set -e

echo "=== AULA RGB Controller Setup ==="

# Detect package manager and install dependencies
if command -v apt &>/dev/null; then
    echo "[1/4] Installing dependencies (apt)..."
    sudo apt update
    sudo apt install -y libusb-1.0-0-dev libjson-c-dev libpulse-dev libsystemd-dev cmake build-essential
    read -p "Install GTK4 GUI? [y/N] " gui
    if [[ "$gui" =~ ^[Yy]$ ]]; then
        sudo apt install -y libgtk-4-dev libadwaita-1-dev
        GUI_FLAG="-DBUILD_GUI=ON"
    fi
elif command -v dnf &>/dev/null; then
    echo "[1/4] Installing dependencies (dnf)..."
    sudo dnf install -y libusb1-devel json-c-devel pulseaudio-libs-devel systemd-devel cmake gcc make
    read -p "Install GTK4 GUI? [y/N] " gui
    if [[ "$gui" =~ ^[Yy]$ ]]; then
        sudo dnf install -y gtk4-devel libadwaita-devel
        GUI_FLAG="-DBUILD_GUI=ON"
    fi
elif command -v pacman &>/dev/null; then
    echo "[1/4] Installing dependencies (pacman)..."
    sudo pacman -S --noconfirm libusb json-c libpulse systemd cmake base-devel
    read -p "Install GTK4 GUI? [y/N] " gui
    if [[ "$gui" =~ ^[Yy]$ ]]; then
        sudo pacman -S --noconfirm gtk4 libadwaita
        GUI_FLAG="-DBUILD_GUI=ON"
    fi
else
    echo "[1/4] Unknown package manager. See README.md for manual dependency list."
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
cmake .. -DCMAKE_BUILD_TYPE=Release ${GUI_FLAG:-}
make -j$(nproc)

# Test
echo "[4/4] Running tests..."
ctest --output-on-failure

echo ""
echo "=== Setup complete ==="
echo ""
echo "Install system-wide (optional):"
echo "  cd build && sudo make install"
echo ""
echo "Or run directly:"
echo "  ./build/cli/f87ctl info"
echo "  ./build/daemon/f87d &"
if [ -n "$GUI_FLAG" ]; then
echo "  ./build/gui/f87control"
fi
