#!/usr/bin/env bash
# The loader for the Linux cores, as the project's own AppImage build makes
# it (scripts/appimage/), in Ubuntu 22.04 as root: its AppDir becomes the
# cores' system/lindbergh/, written to loader-x86.tar.gz for the Linux jobs
# (.ci/libretro-linux.sh)
set -e
export DEBIAN_FRONTEND=noninteractive
# the scripts call sudo, which the image does not have
apt-get -o Acquire::Retries=3 update
apt-get -o Acquire::Retries=3 -y install --no-install-recommends sudo git ca-certificates \
  software-properties-common gpg-agent wget file make
# the checkout may belong to another user than the job's
git config --global --add safe.directory "$PWD"
# their apt and PPA steps; install-kisak.sh's snap and kernel holds have
# nothing to hold in a container and only print errors
bash scripts/appimage/install-packages.sh
bash scripts/appimage/id-libs.sh
bash scripts/appimage/install-kisak.sh || true
bash scripts/appimage/build-deps.sh
bash scripts/appimage/pack-libs.sh
make linux
bash scripts/appimage/make-appimage.sh
tar -czf loader-x86.tar.gz -C build-linux linuxloader.AppDir
