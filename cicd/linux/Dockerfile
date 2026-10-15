# nemo-build-jammy: the Linux RELEASE build box.
#
# Day-to-day Linux dev builds run in nemo-build (Debian 13, matches the host).
# Release tarballs must not: a binary linked against trixie's glibc 2.41 only
# runs on distros from 2025 onward. Ubuntu 22.04 LTS gives glibc 2.35, which
# covers Ubuntu 22.04+, Debian 12+, Mint 21+, Fedora 36+ - and a binary built
# against an older glibc/GTK keeps working on newer ones, not the reverse.
#
# The release prefix links against the distro's own GTK3 (thin prefix - the
# runtime is not bundled on Linux), so the GTK version here is the floor too:
# 3.24.33, and every 3.24.x since is compatible.
#
# make is there for gcc's link-time step, which runs its jobs through it.
#
# Source tree is mounted at /src (like nemo-build); builds go to /build-release.
#
# Pinned, so a box that has to build this image gets the same compiler and
# libraries as every other box, not whatever 22.04 updates are current that day.
# The base is jammy-20260731.1 by digest, and apt reads the archive as it stood
# on 2026-08-04 from snapshot.ubuntu.com. Together they give the package set the
# image was first built with. Move both on purpose, never one alone.
# The same file builds the arm64 image. Its base lists ports.ubuntu.com, and the
# snapshot host serves arm64 from the same /ubuntu/ path, so both go there.
# The snapshot host is https only, and the base has no CA certificates yet; the
# first install skips the TLS check, which costs nothing, since apt still checks
# every index and package against the archive's signed InRelease.

FROM ubuntu:22.04@sha256:3b06811b2afd352be909dd088a004166d665dc76d38b13eada33522a9d915c6f

ENV DEBIAN_FRONTEND=noninteractive

RUN sed -i -E 's#http://((archive|security)\.ubuntu\.com/ubuntu|ports\.ubuntu\.com/ubuntu-ports)/?#https://snapshot.ubuntu.com/ubuntu/20260804T200000Z/#' /etc/apt/sources.list \
	&& apt-get -o Acquire::https::Verify-Peer=false update \
	&& apt-get -o Acquire::https::Verify-Peer=false install -y --no-install-recommends ca-certificates \
	&& apt-get update && apt-get install -y --no-install-recommends \
		meson ninja-build gcc make pkg-config gettext intltool itstool \
		gobject-introspection libgirepository1.0-dev python3-gi \
		libgtk-3-dev libglib2.0-dev libglib2.0-bin libglib2.0-dev-bin \
		libpango1.0-dev libatk1.0-dev libgail-3-dev \
		libjson-glib-dev libexempi-dev libexif-dev libarchive-dev \
		libsqlite3-dev \
		libx11-dev libxext-dev libxrender-dev \
		desktop-file-utils tar xz-utils \
		xvfb \
	&& rm -rf /var/lib/apt/lists/*

WORKDIR /src
