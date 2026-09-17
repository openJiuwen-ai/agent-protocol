#!/usr/bin/env bash
# Install system dependencies for yellow_a2a (a2a_cpp).
# Speeds up CMake by satisfying third_party.cmake system lookups before FetchContent.
#
# Usage:
#   bash ./scripts/install_deps.sh
#   sudo bash ./scripts/install_deps.sh

set -euo pipefail

echo "==> Checking system dependencies for yellow_a2a..."

if [[ ! -f /etc/os-release ]]; then
    echo "Error: Linux with /etc/os-release is required" >&2
    exit 1
fi
# shellcheck disable=SC1091
. /etc/os-release
OS="${ID:-}"
OS_VERSION="${VERSION_ID:-}"

if [[ "${EUID}" -ne 0 ]]; then
    echo "Warning: Not running as root. Package install may require sudo."
    SUDO="sudo"
else
    SUDO=""
fi

command_exists() {
    command -v "$1" >/dev/null 2>&1
}

check_deb_package() {
    dpkg -l "$1" 2>/dev/null | grep -q "^ii"
}

check_rpm_package() {
    rpm -q "$1" >/dev/null 2>&1
}

# Returns 0 if header/library appears present (best-effort).
have_libevent() {
    [[ -f /usr/include/event2/event.h ]] \
        || [[ -f /usr/local/include/event2/event.h ]] \
        || pkg-config --exists libevent 2>/dev/null
}

have_nlohmann_json() {
    [[ -f /usr/include/nlohmann/json.hpp ]] \
        || [[ -f /usr/local/include/nlohmann/json.hpp ]]
}

have_http_parser() {
    [[ -f /usr/include/http_parser.h ]] \
        || [[ -f /usr/local/include/http_parser.h ]]
}

install_dependencies() {
    local need_build_tools=0
    local need_curl=0
    local need_openssl=0
    local need_libevent=0
    local need_json=0
    local need_http_parser=0

    echo "Detected OS: ${OS} ${OS_VERSION}"

    if command_exists cmake; then
        echo "  - cmake: OK"
    else
        echo "  - cmake: NOT FOUND"
        need_build_tools=1
    fi

    if command_exists g++ || command_exists clang++; then
        echo "  - C++ compiler: OK"
    else
        echo "  - C++ compiler: NOT FOUND"
        need_build_tools=1
    fi

    if command_exists pkg-config; then
        echo "  - pkg-config: OK"
    else
        echo "  - pkg-config: NOT FOUND"
        need_build_tools=1
    fi

    case "${OS}" in
        ubuntu|debian)
            if check_deb_package libcurl4-openssl-dev || check_deb_package libcurl4-gnutls-dev; then
                echo "  - libcurl (dev): OK"
            else
                echo "  - libcurl (dev): NOT FOUND"
                need_curl=1
            fi
            if check_deb_package libssl-dev; then
                echo "  - openssl (dev): OK"
            else
                echo "  - openssl (dev): NOT FOUND"
                need_openssl=1
            fi
            if check_deb_package libevent-dev || have_libevent; then
                echo "  - libevent (dev): OK"
            else
                echo "  - libevent (dev): NOT FOUND"
                need_libevent=1
            fi
            if check_deb_package nlohmann-json3-dev || have_nlohmann_json; then
                echo "  - nlohmann_json (dev): OK"
            else
                echo "  - nlohmann_json (dev): NOT FOUND"
                need_json=1
            fi
            if check_deb_package libhttp-parser-dev || have_http_parser; then
                echo "  - http_parser (dev): OK"
            else
                echo "  - http_parser (dev): NOT FOUND"
                need_http_parser=1
            fi
            ;;
        centos|rhel|fedora|euleros|rocky|almalinux)
            if check_rpm_package libcurl-devel; then
                echo "  - libcurl (dev): OK"
            else
                echo "  - libcurl (dev): NOT FOUND"
                need_curl=1
            fi
            if check_rpm_package openssl-devel; then
                echo "  - openssl (dev): OK"
            else
                echo "  - openssl (dev): NOT FOUND"
                need_openssl=1
            fi
            if check_rpm_package libevent-devel || have_libevent; then
                echo "  - libevent (dev): OK"
            else
                echo "  - libevent (dev): NOT FOUND"
                need_libevent=1
            fi
            if check_rpm_package nlohmann-json-devel || have_nlohmann_json; then
                echo "  - nlohmann_json (dev): OK"
            else
                echo "  - nlohmann_json (dev): NOT FOUND (CMake may fetch from GitHub)"
                need_json=1
            fi
            if check_rpm_package http-parser-devel || have_http_parser; then
                echo "  - http_parser (dev): OK"
            else
                echo "  - http_parser (dev): NOT FOUND (CMake may fetch from GitHub)"
                need_http_parser=1
            fi
            if ! command_exists g++; then
                need_build_tools=1
            fi
            if ! command_exists cmake; then
                need_build_tools=1
            fi
            if ! command_exists pkg-config; then
                need_build_tools=1
            fi
            ;;
        arch|manjaro)
            for pkg in curl openssl libevent nlohmann-json; do
                if pacman -Q "${pkg}" >/dev/null 2>&1; then
                    echo "  - ${pkg}: OK"
                else
                    echo "  - ${pkg}: NOT FOUND"
                    case "${pkg}" in
                        curl) need_curl=1 ;;
                        openssl) need_openssl=1 ;;
                        libevent) need_libevent=1 ;;
                        nlohmann-json) need_json=1 ;;
                    esac
                fi
            done
            if pacman -Q http-parser >/dev/null 2>&1 || have_http_parser; then
                echo "  - http_parser: OK"
            else
                echo "  - http_parser: NOT FOUND"
                need_http_parser=1
            fi
            if ! pacman -Q base-devel cmake pkgconf >/dev/null 2>&1; then
                need_build_tools=1
            fi
            ;;
        *)
            if command_exists curl-config; then
                echo "  - libcurl: OK"
            else
                need_curl=1
            fi
            if [[ -f /usr/include/openssl/ssl.h ]] || [[ -f /usr/local/include/openssl/ssl.h ]]; then
                echo "  - openssl: OK"
            else
                need_openssl=1
            fi
            have_libevent && echo "  - libevent: OK" || { echo "  - libevent: NOT FOUND"; need_libevent=1; }
            have_nlohmann_json && echo "  - nlohmann_json: OK" || { echo "  - nlohmann_json: NOT FOUND"; need_json=1; }
            have_http_parser && echo "  - http_parser: OK" || { echo "  - http_parser: NOT FOUND"; need_http_parser=1; }
            ;;
    esac

    local need_any=$((need_build_tools + need_curl + need_openssl + need_libevent + need_json + need_http_parser))
    if [[ ${need_any} -eq 0 ]]; then
        echo "==> All tracked dependencies are already installed!"
        return 0
    fi

    echo ""
    echo "==> Installing missing dependencies..."

    case "${OS}" in
        ubuntu|debian)
            ${SUDO} apt-get update -qq
            local pkgs=()
            [[ ${need_build_tools} -eq 1 ]] && pkgs+=(build-essential cmake pkg-config)
            [[ ${need_curl} -eq 1 ]] && pkgs+=(libcurl4-openssl-dev)
            [[ ${need_openssl} -eq 1 ]] && pkgs+=(libssl-dev)
            [[ ${need_libevent} -eq 1 ]] && pkgs+=(libevent-dev)
            [[ ${need_json} -eq 1 ]] && pkgs+=(nlohmann-json3-dev)
            [[ ${need_http_parser} -eq 1 ]] && pkgs+=(libhttp-parser-dev)
            if [[ ${#pkgs[@]} -gt 0 ]]; then
                echo "Installing: ${pkgs[*]}"
                ${SUDO} apt-get install -y "${pkgs[@]}"
            fi
            ;;
        centos|rhel|fedora|euleros|rocky|almalinux)
            local pkgs=()
            [[ ${need_build_tools} -eq 1 ]] && pkgs+=(gcc-c++ make cmake pkgconfig)
            [[ ${need_curl} -eq 1 ]] && pkgs+=(libcurl-devel)
            [[ ${need_openssl} -eq 1 ]] && pkgs+=(openssl-devel)
            [[ ${need_libevent} -eq 1 ]] && pkgs+=(libevent-devel)
            [[ ${need_json} -eq 1 ]] && pkgs+=(nlohmann-json-devel)
            [[ ${need_http_parser} -eq 1 ]] && pkgs+=(http-parser-devel)
            if [[ ${#pkgs[@]} -gt 0 ]]; then
                if command_exists dnf; then
                    echo "Installing: ${pkgs[*]}"
                    ${SUDO} dnf install -y "${pkgs[@]}" || true
                else
                    echo "Installing: ${pkgs[*]}"
                    ${SUDO} yum install -y "${pkgs[@]}" || true
                fi
            fi
            if [[ ${need_json} -eq 1 ]] && ! have_nlohmann_json; then
                echo "[INFO] nlohmann-json-devel unavailable on this distro; CMake will fetch nlohmann_json."
            fi
            if [[ ${need_http_parser} -eq 1 ]] && ! have_http_parser; then
                echo "[INFO] http-parser-devel unavailable on this distro; CMake will fetch http_parser."
            fi
            ;;
        arch|manjaro)
            local pkgs=(base-devel cmake pkgconf)
            [[ ${need_curl} -eq 1 ]] && pkgs+=(curl)
            [[ ${need_openssl} -eq 1 ]] && pkgs+=(openssl)
            [[ ${need_libevent} -eq 1 ]] && pkgs+=(libevent)
            [[ ${need_json} -eq 1 ]] && pkgs+=(nlohmann-json)
            [[ ${need_http_parser} -eq 1 ]] && pkgs+=(http-parser)
            echo "Installing: ${pkgs[*]}"
            ${SUDO} pacman -S --noconfirm "${pkgs[@]}"
            ;;
        *)
            echo "Error: Unsupported OS for automatic install: ${OS}" >&2
            echo "Please install manually: cmake, g++, pkg-config, libcurl-devel, openssl-devel," >&2
            echo "libevent-devel, nlohmann-json (optional), http-parser (optional)." >&2
            exit 1
            ;;
    esac

    echo ""
    echo "==> Dependency installation complete!"
}

check_dependencies() {
    echo ""
    echo "==> Verifying installation..."
    local ok=1

    if ! command_exists cmake; then
        echo "✗ cmake is missing" >&2
        ok=0
    fi
    if ! command_exists g++ && ! command_exists clang++; then
        echo "✗ C++ compiler (g++ or clang++) is missing" >&2
        ok=0
    fi
    if ! command_exists pkg-config; then
        echo "✗ pkg-config is missing" >&2
        ok=0
    fi
    if ! command_exists curl-config && ! pkg-config --exists libcurl 2>/dev/null; then
        echo "✗ libcurl development files are missing" >&2
        ok=0
    fi
    if ! command_exists openssl; then
        echo "✗ openssl is missing" >&2
        ok=0
    fi

    if have_libevent; then
        echo "✓ libevent headers found"
    else
        echo "[WARN] libevent not found on system; first build may download libevent via CMake"
    fi
    if have_nlohmann_json; then
        echo "✓ nlohmann/json headers found"
    else
        echo "[WARN] nlohmann_json not found on system; CMake may fetch from GitHub"
    fi
    if have_http_parser; then
        echo "✓ http_parser headers found"
    else
        echo "[WARN] http_parser not found on system; CMake may fetch from GitHub"
    fi

    if [[ ${ok} -eq 1 ]]; then
        echo "✓ Core dependencies verified. You can run: bash ./scripts/build.sh -e"
        return 0
    fi
    echo "✗ Some required dependencies are still missing." >&2
    exit 1
}

install_dependencies
check_dependencies
