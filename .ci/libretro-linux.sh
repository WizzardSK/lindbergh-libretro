#!/usr/bin/env bash
# The Linux core around the loader of the loader job (loader-x86.tar.gz,
# .ci/libretro-loader.sh): its AppDir zipped into the core as
# system/lindbergh/. On aarch64 box64, built here with BOX32, goes beside it
# and runs the 32-bit x86 loader and game.
set -e
export DEBIAN_FRONTEND=noninteractive
# the template's share of the runner, at least one job
J=$(( ${NUMPROC:-0} > 0 ? NUMPROC : 1 ))
apt-get -o Acquire::Retries=3 update
apt-get -o Acquire::Retries=3 -y install --no-install-recommends gcc libc6-dev make zip git ca-certificates
# the checkout may belong to another user than the job's
git config --global --add safe.directory "$PWD"
mkdir -p dist/system/lindbergh
tar -xzf loader-x86.tar.gz -C dist
cp -a dist/linuxloader.AppDir/. dist/system/lindbergh/
rm -rf dist/linuxloader.AppDir
if [ "$(uname -m)" = aarch64 ]; then
  apt-get -o Acquire::Retries=3 -y install --no-install-recommends g++ cmake python3
  git clone --depth 1 https://github.com/ptitSeb/box64.git box64
  cmake -S box64 -B box64/build -DARM_DYNAREC=ON -DBOX32=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
  cmake --build box64/build -j"$J"
  install -m 755 box64/build/box64 dist/system/lindbergh/box64
  git -C box64 rev-parse --short HEAD > dist/system/lindbergh/box64.version
fi
git rev-parse --short HEAD > dist/system/lindbergh/build
(cd dist/system/lindbergh && zip -q -r -y -9 "$OLDPWD/system.zip" .)
CC=gcc make -f Makefile.libretro SYSTEM_ZIP=system.zip BUILD_STAMP="$(git rev-parse --short HEAD)" -j"$J"
cp build/lindbergh_libretro.so lindbergh_libretro.so
strip --strip-unneeded lindbergh_libretro.so
