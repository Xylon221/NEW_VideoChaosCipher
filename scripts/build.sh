#!/bin/bash
# VideoChaosCipher 构建脚本
# 支持 RK3588 (aarch64) 和 x86_64 平台

set -e

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${PROJECT_DIR}/build"

echo "========================================"
echo " VideoChaosCipher Build Script"
echo " Platform: $(uname -m)"
echo " Project:  ${PROJECT_DIR}"
echo "========================================"

# 解析参数
BUILD_TYPE="Release"
BUILD_TESTS="OFF"
while [[ $# -gt 0 ]]; do
    case $1 in
        --debug)
            BUILD_TYPE="Debug"
            shift
            ;;
        --test)
            BUILD_TESTS="ON"
            shift
            ;;
        --clean)
            echo "[CLEAN] Removing build directory..."
            rm -rf "${BUILD_DIR}"
            shift
            ;;
        *)
            echo "Unknown option: $1"
            echo "Usage: $0 [--debug] [--test] [--clean]"
            exit 1
            ;;
    esac
done

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

echo "[CMAKE] Configuring..."
echo "  Build type: ${BUILD_TYPE}"
echo "  Build tests: ${BUILD_TESTS}"

cmake .. \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DBUILD_TESTS="${BUILD_TESTS}" \
    -DCMAKE_INSTALL_PREFIX=/usr/local

echo "[MAKE] Building..."
make -j$(nproc)

echo ""
echo "========================================"
echo " Build completed successfully!"
echo ""
echo " Executables:"
ls -lh app test_frame 2>/dev/null || true
echo ""
echo " Run: ./app <input> <output> [start] [end] [seed] [-t N]"
echo " Run tests (if built): ctest"
echo " Install: sudo make install"
echo "========================================"
