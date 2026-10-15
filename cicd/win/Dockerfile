# nemo-winbuild: cross-compile the Windows target from Linux.
#   - mingw-w64 toolchain + meson/ninja
#   - native glib codegen tools (run on the build host during the cross build)
#   - wine (smoke-test the .exe as an exe_wrapper / by hand)
#   - Xvfb + x11 utils to run the GUI headless under wine and grab a screenshot
#   - a baked-in MSYS2 GTK3 sysroot at /opt/win-sysroot (see fetch-sysroot.bash)
#   - make, which nothing here builds with: gcc's lto-wrapper shells out to it
#     to run the LTRANS jobs in parallel, and without it every link is serial
#   - nsis, for the setup exe (pack-setup.bash)
# Source tree is mounted at /src (like nemo-build); builds go to /build-win.

FROM debian:trixie

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
		gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64 binutils-mingw-w64-x86-64 mingw-w64-tools \
		meson ninja-build pkgconf make \
		libglib2.0-dev-bin libglib2.0-bin gettext intltool desktop-file-utils python3 \
		curl ca-certificates gawk tar zstd xz-utils \
		wine wine64 \
		xvfb x11-utils x11-apps imagemagick nsis \
	&& rm -rf /var/lib/apt/lists/*

COPY fetch-sysroot.bash /opt/fetch-sysroot.bash
RUN bash /opt/fetch-sysroot.bash /opt/win-sysroot

COPY win64.cross.txt /opt/win64.cross.txt

WORKDIR /src
