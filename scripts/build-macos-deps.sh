#!/bin/bash
# Builds OpenSSL's libcrypto from the pinned release tarball as a universal
# (arm64 + x86_64) static archive for the Xcode project:
#   a/libcrypto.a       (lipo of darwin64-arm64-cc + darwin64-x86_64-cc)
#   include/openssl/*.h (installed headers; identical for both archs)
#
# no-shared no-module: static only, and the legacy provider (MDC2, RC2,
# the PKCS#12 encryption of older exports) is linked into libcrypto instead
# of being a loadable module; the plugin loads it at startup.
#
# usage: scripts/build-macos-deps.sh [root-dir]   (default: repository root)
# WORK (default build/deps) holds the download and the per-arch builds; a
# rerun reuses them. Needs: Xcode command line tools, perl, curl.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$ROOT}"
WORK="${WORK:-$ROOT/build/deps}"
MIN="${MACOSX_DEPLOYMENT_TARGET:-11.5}"
JOBS="$(sysctl -n hw.ncpu)"

OPENSSL_VERSION=3.5.9
OPENSSL_SHA256=603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a
OPENSSL_URL="https://github.com/openssl/openssl/releases/download/openssl-${OPENSSL_VERSION}/openssl-${OPENSSL_VERSION}.tar.gz"
OPENSSL_OPTIONS="no-shared no-module no-tests no-docs no-apps"

mkdir -p "$WORK"
TARBALL="$WORK/openssl-${OPENSSL_VERSION}.tar.gz"

if ! echo "$OPENSSL_SHA256  $TARBALL" | shasum -a 256 -c - >/dev/null 2>&1; then
  rm -f "$TARBALL"
  for i in 1 2 3; do
    curl -fsSL -o "$TARBALL" "$OPENSSL_URL" && break
    [ "$i" = 3 ] && exit 1
    echo "download failed, retrying"; sleep 10
  done
  echo "$OPENSSL_SHA256  $TARBALL" | shasum -a 256 -c -
fi

build_arch() {
  local ARCH="$1" TARGET
  case "$ARCH" in
    arm64) TARGET=darwin64-arm64-cc ;;
    x86_64) TARGET=darwin64-x86_64-cc ;;
  esac
  local D="$WORK/$ARCH"
  local PREFIX="$D/install"
  if [ -f "$PREFIX/lib/libcrypto.a" ]; then
    return 0
  fi
  echo "::group::OpenSSL $OPENSSL_VERSION $ARCH"
  rm -rf "$D"
  mkdir -p "$D"
  tar -xzf "$TARBALL" -C "$D"
  local SRC="$D/openssl-${OPENSSL_VERSION}"
  # unset so Homebrew headers/libs can't leak into the build
  (cd "$SRC" \
    && env -u CPATH -u LIBRARY_PATH -u CFLAGS -u LDFLAGS \
       ./Configure "$TARGET" $OPENSSL_OPTIONS \
         --prefix="$PREFIX" --libdir=lib --openssldir=/dev/null \
         "-mmacosx-version-min=$MIN" \
    && make -j"$JOBS" build_libs \
    && make install_dev)
  echo "::endgroup::"
}

build_arch arm64
build_arch x86_64

# the generated headers (configuration.h, ...) must not differ between archs
diff -r "$WORK/arm64/install/include/openssl" "$WORK/x86_64/install/include/openssl"

mkdir -p "$OUT/a" "$OUT/include"
lipo -create "$WORK/arm64/install/lib/libcrypto.a" "$WORK/x86_64/install/lib/libcrypto.a" \
  -output "$OUT/a/libcrypto.a"
rm -rf "$OUT/include/openssl"
cp -R "$WORK/arm64/install/include/openssl" "$OUT/include/openssl"

lipo -info "$OUT/a/libcrypto.a"
lipo "$OUT/a/libcrypto.a" -verify_arch arm64 x86_64
grep -q "OPENSSL_VERSION_TEXT.*\"OpenSSL $OPENSSL_VERSION " "$OUT/include/openssl/opensslv.h"
echo "OpenSSL $OPENSSL_VERSION -> $OUT/a/libcrypto.a, $OUT/include/openssl"
