#!/usr/bin/env bash

# Install dependencies for mcp_cpp project
# Checks and installs libcurl and openssl if not present on the system
#
# Usage:
#   ./scripts/install_deps.sh
#   sudo ./scripts/install_deps.sh  # if running as non-root user

set -euo pipefail

echo "==> Checking system dependencies for mcp_cpp..."

# Detect OS type
if [ "$(uname -s)" = "Darwin" ]; then
    OS="macos"
    OS_VERSION="$(sw_vers -productVersion 2>/dev/null || echo unknown)"
elif [ -f /etc/os-release ]; then
    . /etc/os-release
    OS=$ID
    OS_VERSION=$VERSION_ID
else
    echo "Error: Cannot detect OS type"
    exit 1
fi

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo "Warning: Not running as root. You may need sudo privileges to install packages."
    SUDO="sudo"
else
    SUDO=""
fi

# Function to check if a command exists
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

# Function to check if a package is installed (Debian/Ubuntu)
check_deb_package() {
    dpkg -l "$1" 2>/dev/null | grep -q "^ii"
}

# Function to check if a package is installed (RHEL/CentOS)
check_yum_package() {
    yum list installed "$1" >/dev/null 2>&1
}

# Install dependencies based on OS
install_dependencies() {
    local need_curl=0
    local need_openssl=0
    local need_libevent=0
    local need_json=0

    # Check dependencies (both runtime lib and development packages)
    case "$OS" in
        ubuntu|debian)
            # Check libcurl
            if check_deb_package libcurl4 || check_deb_package libcurl3; then
                echo "  - libcurl: OK"
            else
                echo "  - libcurl: NOT FOUND"
                need_curl=1
            fi

            # Check libcurl-devel
            if check_deb_package libcurl4-openssl-dev; then
                echo "  - libcurl-devel: OK"
            else
                echo "  - libcurl-devel: NOT FOUND"
                need_curl=1
            fi

            # Check openssl-lib (runtime library)
            if check_deb_package openssl-libs; then
                echo "  - openssl-lib: OK"
            else
                echo "  - openssl-lib: NOT FOUND"
                need_openssl=1
            fi

            # Check openssl-devel (development package)
            if check_deb_package libssl-dev; then
                echo "  - openssl-devel: OK"
            else
                echo "  - openssl-devel: NOT FOUND"
                need_openssl=1
            fi
            ;;

        centos|rhel|fedora|euleros)
            # Check libcurl (runtime library)
            if check_yum_package libcurl; then
                echo "  - libcurl: OK"
            else
                echo "  - libcurl: NOT FOUND"
                need_curl=1
            fi

            # Check libcurl-devel (development package)
            if check_yum_package libcurl-devel; then
                echo "  - libcurl-devel: OK"
            else
                echo "  - libcurl-devel: NOT FOUND"
                need_curl=1
            fi

            # Check openssl-libs (runtime library)
            if check_yum_package openssl-libs; then
                echo "  - openssl-libs: OK"
            else
                echo "  - openssl-libs: NOT FOUND"
                need_openssl=1
            fi

            # Check openssl-devel (development package)
            if check_yum_package openssl-devel; then
                echo "  - openssl-devel: OK"
            else
                echo "  - openssl-devel: NOT FOUND"
                need_openssl=1
            fi
            ;;

        arch|manjaro)
            # Arch packages both lib and devel in one package
            if pacman -Q curl >/dev/null 2>&1; then
                echo "  - curl (lib+devel): OK"
            else
                echo "  - curl (lib+devel): NOT FOUND"
                need_curl=1
            fi

            if pacman -Q openssl >/dev/null 2>&1; then
                echo "  - openssl (lib+devel): OK"
            else
                echo "  - openssl (lib+devel): NOT FOUND"
                need_openssl=1
            fi
            ;;

        macos)
            if ! command_exists brew; then
                echo "  - Homebrew: NOT FOUND (see https://brew.sh)"
                need_curl=1
                need_openssl=1
            else
                if brew list --versions curl >/dev/null 2>&1; then
                    echo "  - curl: OK"
                else
                    echo "  - curl: NOT FOUND"
                    need_curl=1
                fi
                if brew list --versions openssl@3 >/dev/null 2>&1 || brew list --versions openssl >/dev/null 2>&1; then
                    echo "  - openssl: OK"
                else
                    echo "  - openssl: NOT FOUND"
                    need_openssl=1
                fi
                if brew list --versions libevent >/dev/null 2>&1; then
                    echo "  - libevent: OK"
                else
                    echo "  - libevent: NOT FOUND"
                    need_libevent=1
                fi
                if brew list --versions nlohmann-json >/dev/null 2>&1; then
                    echo "  - nlohmann_json: OK"
                else
                    echo "  - nlohmann_json: NOT FOUND"
                    need_json=1
                fi
            fi
            ;;

        *)
            # Fallback: check for header files
            if command_exists curl-config; then
                echo "  - libcurl: OK"
            else
                echo "  - libcurl: NOT FOUND"
                need_curl=1
            fi

            if [ -f /usr/include/openssl/ssl.h ] || [ -f /usr/local/include/openssl/ssl.h ]; then
                echo "  - openssl: OK"
            else
                echo "  - openssl: NOT FOUND"
                need_openssl=1
            fi
            ;;
    esac

    # If all dependencies are present, exit
    if [ $need_curl -eq 0 ] && [ $need_openssl -eq 0 ] && [ $need_libevent -eq 0 ] && [ $need_json -eq 0 ]; then
        echo "==> All dependencies are already installed!"
        return 0
    fi

    echo ""
    echo "==> Installing missing dependencies..."

    case "$OS" in
        ubuntu|debian)
            echo "Detected Debian/Ubuntu system"

            # Update package list
            $SUDO apt-get update -qq

            # Install libcurl if needed
            if [ $need_curl -eq 1 ]; then
                echo "Installing libcurl and libcurl4-openssl-dev..."
                $SUDO apt-get install -y libcurl4-openssl-dev
            fi

            # Install openssl if needed
            if [ $need_openssl -eq 1 ]; then
                echo "Installing libssl-dev (openssl-devel)..."
                $SUDO apt-get install -y libssl-dev
            fi
            ;;

        centos|rhel|fedora|euleros)
            echo "Detected RHEL/CentOS/Fedora/EulerOS system"

            # Install libcurl if needed
            if [ $need_curl -eq 1 ]; then
                echo "Installing libcurl and libcurl-devel..."
                $SUDO yum install -y libcurl libcurl-devel
            fi

            # Install openssl if needed
            if [ $need_openssl -eq 1 ]; then
                echo "Installing openssl-libs and openssl-devel..."
                $SUDO yum install -y openssl-libs openssl-devel
            fi
            ;;

        arch|manjaro)
            echo "Detected Arch Linux system"

            # Install libcurl if needed
            if [ $need_curl -eq 1 ]; then
                echo "Installing curl (includes libcurl)..."
                $SUDO pacman -S --noconfirm curl
            fi

            # Install openssl if needed
            if [ $need_openssl -eq 1 ]; then
                echo "Installing openssl (includes lib and devel)..."
                $SUDO pacman -S --noconfirm openssl
            fi
            ;;

        macos)
            echo "Detected macOS system"

            if ! command_exists brew; then
                echo "Error: Homebrew is required on macOS (see https://brew.sh)" >&2
                exit 1
            fi

            local pkgs=()
            [ $need_curl -eq 1 ] && pkgs+=(curl)
            [ $need_openssl -eq 1 ] && pkgs+=(openssl@3)
            [ $need_libevent -eq 1 ] && pkgs+=(libevent)
            [ $need_json -eq 1 ] && pkgs+=(nlohmann-json)
            if [ ${#pkgs[@]} -gt 0 ]; then
                echo "Installing (Homebrew): ${pkgs[*]}"
                brew install "${pkgs[@]}"
            fi
            ;;

        *)
            echo "Error: Unsupported OS: $OS"
            echo "Please install libcurl-devel and openssl manually"
            exit 1
            ;;
    esac

    echo ""
    echo "==> Dependency installation complete!"
}

# Main
install_dependencies

# Verify installation
echo ""
echo "==> Verifying installation..."
if [ "$OS" = "macos" ]; then
    ok=1
    if ! command_exists brew; then
        echo "✗ Homebrew is missing." >&2
        ok=0
    fi
    # Same package checks as the install step above, so a partially failed
    # install is reported here instead of failing later at CMake configure.
    if brew list --versions openssl@3 >/dev/null 2>&1 || brew list --versions openssl >/dev/null 2>&1; then
        echo "  - openssl: OK"
    else
        echo "✗ openssl is missing (brew install openssl@3)" >&2
        ok=0
    fi
    if brew list --versions curl >/dev/null 2>&1; then
        echo "  - curl: OK"
    else
        echo "✗ curl is missing (brew install curl)" >&2
        ok=0
    fi
    if brew list --versions libevent >/dev/null 2>&1; then
        echo "  - libevent: OK"
    else
        echo "✗ libevent is missing (brew install libevent)" >&2
        ok=0
    fi
    if brew list --versions nlohmann-json >/dev/null 2>&1; then
        echo "  - nlohmann_json: OK"
    else
        echo "✗ nlohmann_json is missing (brew install nlohmann-json)" >&2
        ok=0
    fi
    if [ "${ok}" -eq 1 ]; then
        echo "✓ Dependencies verified (Homebrew)."
        exit 0
    fi
    echo "✗ Some dependencies are still missing. Please check the errors above." >&2
    exit 1
fi
if command_exists curl-config && command_exists openssl; then
    echo "✓ All dependencies verified successfully!"
    exit 0
else
    echo "✗ Some dependencies are still missing. Please check the errors above."
    exit 1
fi
