#!/usr/bin/env bash
# The Windows core in Ubuntu 24.04 as root: the loader's Windows build
# (linuxloader.exe and ll-deps/, with Ubuntu's 32-bit MinGW, whose
# libgcc_s_dw2-1.dll scripts/windows/windows-build.sh copies), zipped into
# the 64-bit core as system/lindbergh/
set -e
export DEBIAN_FRONTEND=noninteractive
# the template's share of the runner, at least one job
J=$(( ${NUMPROC:-0} > 0 ? NUMPROC : 1 ))
apt-get -o Acquire::Retries=3 update
apt-get -o Acquire::Retries=3 -y install --no-install-recommends mingw-w64 mingw-w64-i686-dev \
  cmake make zip unzip git ca-certificates
bash scripts/windows/windows-build.sh
mkdir -p dist/system/lindbergh
cp -a win-release/. dist/system/lindbergh/
git rev-parse --short HEAD > dist/system/lindbergh/build
(cd dist/system/lindbergh && zip -q -r -9 "$OLDPWD/system.zip" .)
make -f Makefile.libretro platform=win64 SYSTEM_ZIP=system.zip BUILD_STAMP="$(git rev-parse --short HEAD)" -j"$J"
cp build/lindbergh_libretro.dll lindbergh_libretro.dll
x86_64-w64-mingw32-strip --strip-unneeded lindbergh_libretro.dll
