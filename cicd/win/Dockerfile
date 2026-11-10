# nemo-winbuild: cross-compile the Windows target from Linux.
#   - mingw-w64 toolchain + meson/ninja
#   - native glib codegen tools, run on the build host during the cross build
#   - wine, to smoke-test the .exe as an exe_wrapper or by hand
#   - Xvfb + x11 utils to run the GUI headless under wine and grab a screenshot
#   - a baked-in MSYS2 GTK3 sysroot at /opt/win-sysroot (see fetch-sysroot.bash)
#   - make, which nothing here builds with: gcc's lto-wrapper shells out to it
#     to run the LTRANS jobs in parallel, and without it every link is serial
#   - nsis, for the setup exe (pack-setup.bash)
#   - clang 19, for the analyzer stage's Windows lane (utility/analyze.bash),
#     and clang-tools-19 for its cross-file run
# Source tree is mounted at /src (like nemo-build); builds go to /build-win,
# a cache volume (win-box.bash).
#
# Build it with cicd/ as the context, since fetch-sysroot.bash needs
# utility/include/run-temp.bash:
#   docker build -f cicd/win/Dockerfile cicd/
#
# Pinned: the base by digest (trixie, 2026-07-13), the base the old hand-made
# image was built on. apt reads the archive, the security archive included,
# from snapshot.debian.org as it stood at 2026-10-07 22:00 UTC, the last time
# the old image was updated. Move the snapshot on purpose, and the digest with
# it when the base moves. Snapshot Release files are past their Valid-Until,
# which apt would refuse; every index and package is still checked against the
# signed InRelease. The sysroot is the MSYS2 files in win/sysroot-pins.txt, by
# name and sha256.

FROM debian:trixie@sha256:fac46bff2e02f51425b6e33b0e1169f55dfb053d83511ca28aa50c09fd5ed7a4

ENV DEBIAN_FRONTEND=noninteractive

RUN sed -i -E 's#http://deb\.debian\.org/(debian(-security)?)$#http://snapshot.debian.org/archive/\1/20261007T220000Z#' /etc/apt/sources.list.d/debian.sources \
	&& echo 'Acquire::Check-Valid-Until "false";' > /etc/apt/apt.conf.d/80snapshot \
	&& apt-get update && apt-get install -y --no-install-recommends \
		gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64 binutils-mingw-w64-x86-64 mingw-w64-tools \
		meson ninja-build pkgconf make \
		libglib2.0-dev-bin libglib2.0-bin gettext intltool desktop-file-utils python3 \
		curl ca-certificates gawk tar zstd xz-utils \
		wine wine64 \
		xvfb x11-utils x11-apps imagemagick nsis \
	&& rm -rf /var/lib/apt/lists/*

COPY utility/include/run-temp.bash /opt/sysroot/utility/include/run-temp.bash
COPY win/fetch-sysroot.bash win/sysroot-pins.txt /opt/sysroot/win/
RUN bash /opt/sysroot/win/fetch-sysroot.bash /opt/win-sysroot

# Its own layer, after the sysroot, so a box with the layers above cached builds
# only this one. The same snapshot pins it. It finds the MinGW headers through
# the gcc above.
RUN apt-get update && apt-get install -y --no-install-recommends clang-19 \
	&& rm -rf /var/lib/apt/lists/*
# clang-extdef-mapping, for the cross-file clang analyzer (analyze.bash --ctu,
# --leakcheck). Its own layer, so the one above stays cached.
RUN apt-get update && apt-get install -y --no-install-recommends clang-tools-19 \
	&& rm -rf /var/lib/apt/lists/*

COPY win/win64.cross.txt /opt/win64.cross.txt

WORKDIR /src
