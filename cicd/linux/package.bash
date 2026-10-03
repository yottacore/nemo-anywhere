#!/usr/bin/env bash

##	- Purpose: Turn a finished Linux release tarball into distro packages - a .deb
##	  and a .rpm - alongside it in cicd/artifacts/release.
##	- Both packages install the same relocatable prefix the tarball contains, under
##	  /opt/<slug>, plus the three things a system install owes the desktop: a name
##	  on PATH, a menu entry, and the app icon in the shared icon theme.
##	- Shared-library dependencies for the .deb are read off the built binaries with
##	  dpkg-shlibdeps IN THE RELEASE CONTAINER, not on this box: the package has to
##	  claim the same floor the binary was built against, and this host is newer.
##	  rpmbuild works those out from the ELF itself, so the .rpm needs no help.
##	- A missing packaging tool warns and skips that format; it never fails the run.
##	- Syntax:
##	  cicd/linux/package.bash [options]
##	  Options:
##	   --from TARBALL   package this tarball (default: newest in the release dir)
##	   --no-deb         skip the .deb
##	   --no-rpm         skip the .rpm

##	History: At bottom of script.

##	Copyright (c) 2026 Bubbles
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT


set -Eeuo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${HERE}/../.." && pwd)"
SLUG="nemo-anywhere"
NAME="Nemo Anywhere"
OUT="${ROOT}/cicd/artifacts/release"
CONTAINER="${NEMO_RELEASE_CONTAINER:-nemo-build-jammy}"
MAINTAINER="t00mietum <t00mietum@users.noreply.github.com>"

# shellcheck source=../utility/include/echo.bash
source "${ROOT}/cicd/utility/include/echo.bash"
# shellcheck source=../utility/include/source-date.bash
source "${ROOT}/cicd/utility/include/source-date.bash"

tarball=""; do_deb=1; do_rpm=1
while (($#)); do case "$1" in
	--from)      tarball="${2:?--from needs a path}"; shift 2 ;;
	--from=*)    tarball="${1#*=}"; shift ;;
	--no-deb)    do_deb=0; shift ;;
	--no-rpm)    do_rpm=0; shift ;;
	-h|--help)   sed -n '/^##	- Purpose:/,/^##	History:/p' "${BASH_SOURCE[0]}" | sed '$d; s/^##	\{0,1\}//'; exit 0 ;;
	*)           fDie "unknown option: $1 (try --help)" ;;
esac; done

if [[ -z "$tarball" ]]; then
	## Newest matching tarball. The glob is guarded so an empty dir says so
	## rather than passing the literal pattern on to tar.
	shopt -s nullglob
	candidates=("${OUT}/${SLUG}"-*-linux-*.tar.gz)
	shopt -u nullglob
	((${#candidates[@]})) || fDie "no release tarball in cicd/artifacts/release - run cicd/linux/release.bash first"
	tarball="${candidates[0]}"
	for cand in "${candidates[@]}"; do
		if [[ "$cand" -nt "$tarball" ]]; then tarball="$cand"; fi
	done
fi
[[ -f "$tarball" ]] || fDie "no such tarball: ${tarball}"

## nemo-anywhere-<ver>-linux-<arch>.tar.gz
base="$(basename "$tarball" .tar.gz)"
ver="${base#"${SLUG}"-}"; ver="${ver%%-linux-*}"
arch="${base##*-linux-}"
[[ -n "$ver" && -n "$arch" ]] || fDie "cannot read version/arch out of $(basename "$tarball")"

## dpkg-deb and rpmbuild both read this and stamp it instead of the clock, so the
## same tarball packages to the same bytes twice.
fSetSourceDate "$ROOT"
fWarnIfSourceDateIsAGuess "$ROOT"

case "$arch" in
	x86_64) deb_arch="amd64"; rpm_arch="x86_64" ;;
	arm64)  deb_arch="arm64"; rpm_arch="aarch64" ;;
	*)      fDie "unknown architecture: ${arch}" ;;
esac

## RPM versions cannot contain '-'; it separates version from release.
rpm_ver="${ver//-/\~}"

work="$(mktemp -d)"
trap 'rm -rf "${work}"' EXIT

fEcho_Clean
fEcho "Packaging ${SLUG} ${ver} (${arch})"
tar -xzf "$tarball" -C "$work"
prefix="${work}/${base}"
[[ -x "${prefix}/bin/${SLUG}" ]] || fDie "tarball has no bin/${SLUG}: ${tarball}"


#••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
# Shared install tree

## Both formats install the same thing, so build the tree once: the whole prefix
## under /opt, then the launcher, menu entry and icons in the system locations.
## The menu entry's Exec/Icon are rewritten to absolute paths - the tarball's copy
## points at wherever the folder happened to end up, which a package knows for sure.
build_tree(){
	local root="$1" size src
	mkdir -p "${root}/opt" "${root}/usr/bin" "${root}/usr/share/applications"
	cp -a "${prefix}" "${root}/opt/${SLUG}"
	ln -sf "/opt/${SLUG}/bin/${SLUG}" "${root}/usr/bin/${SLUG}"

	if [[ -f "${prefix}/share/applications/${SLUG}.desktop" ]]; then
		sed -e "s|^Exec=.*|Exec=/opt/${SLUG}/bin/${SLUG} %U|" \
		    -e "s|^Icon=.*|Icon=${SLUG}|" \
		    "${prefix}/share/applications/${SLUG}.desktop" \
		    > "${root}/usr/share/applications/${SLUG}.desktop"
	fi

	## Icons go in the shared theme as well as the prefix, so the menu entry
	## resolves without the app having to be running.
	for src in "${prefix}"/share/icons/hicolor/*/apps/"${SLUG}".*; do
		[[ -f "$src" ]] || continue
		size="$(basename "$(dirname "$(dirname "$src")")")"
		mkdir -p "${root}/usr/share/icons/hicolor/${size}/apps"
		cp -a "$src" "${root}/usr/share/icons/hicolor/${size}/apps/"
	done
}

## Runtime dependencies, read off the real binaries at the glibc floor they were
## built against. Falls back to a conservative list with a warning when the
## release container is not up, so a package still gets built.
## Only the dependency line reaches stdout - anything it says to the console goes
## to stderr, or it would end up inside the control file's Depends field.
deb_depends(){
	local line=""

	if docker exec "$CONTAINER" true 2>/dev/null; then
		## -l for the prefix's own lib dir, if the build has one, so a bundled
		## extension library resolves; --ignore-missing-info so it is then
		## skipped rather than treated as an unpackaged dependency and made fatal.
		line="$(docker exec -i "$CONTAINER" sh -c '
			set -e
			d=$(mktemp -d); cd "$d"
			tar -xzf - >/dev/null
			cd */
			mkdir -p debian
			printf "Source: pkg\n\nPackage: pkg\nArchitecture: any\n" > debian/control
			: > debian/pkg.substvars
			libargs=""
			for ld in lib/*/; do
				if [ -d "$ld" ]; then libargs="$libargs -l$PWD/$ld"; fi
			done
			dpkg-shlibdeps -O --ignore-missing-info $libargs \
				-Tdebian/pkg.substvars bin/* 2>/dev/null
		' < "$tarball" | sed -n 's/^shlibs:Depends=//p' | head -1)" || line=""
	fi

	if [[ -z "$line" ]]; then
		fEcho "WARNING: could not read dependencies from ${CONTAINER}; using a conservative list" >&2
		line="libc6, libglib2.0-0, libgtk-3-0, libgdk-pixbuf-2.0-0, libpango-1.0-0, libcairo2, libjson-glib-1.0-0, libarchive13, libexif12, libgsf-1-114"
	fi
	printf '%s' "$line"
}

## du reports allocated blocks, which this filesystem rounds hard; the apparent
## size is what a download is.
fSize(){ du -h --apparent-size "$1" | cut -f1; }

## Installed-Size the way dpkg-gencontrol counts it: each file or symlink rounded
## up to a KiB, a hardlink once, anything else 1. du counts disk blocks, so the
## same tree gave a different number, and a different .deb, on each filesystem.
fInstalledKb(){
	find "$1" -printf '%y %s %D:%i %n\n' | awk '
		$1 == "f" || $1 == "l" { if ($4 > 1 && seen[$3]++) next; kb += int(($2 + 1023) / 1024); next }
		{ kb += 1 }
		END { print kb + 0 }'
}


#••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
# .deb

made=0

if ((do_deb)); then
	if command -v dpkg-deb >/dev/null 2>&1; then
		fEcho_Clean
		fEcho "Building .deb"
		debroot="${work}/deb"
		build_tree "$debroot"
		mkdir -p "${debroot}/DEBIAN"
		depends="$(deb_depends)"
		installed_kb="$(fInstalledKb "$debroot")"
		{
			echo "Package: ${SLUG}"
			echo "Version: ${ver}"
			echo "Architecture: ${deb_arch}"
			echo "Maintainer: ${MAINTAINER}"
			echo "Installed-Size: ${installed_kb}"
			echo "Depends: ${depends}"
			## The action layout editor is a PyGObject script. Everything else
			## runs without it, so a missing python only costs that one window.
			echo "Recommends: python3-gi, python3-gi-cairo, gir1.2-gtk-3.0"
			echo "Section: utils"
			echo "Priority: optional"
			echo "Homepage: https://github.com/yottacore/${SLUG}"
			echo "Description: ${NAME} file manager"
			echo " A portable fork of Nemo that runs on any desktop, or none."
			echo " Installs alongside an existing Nemo without conflicting with it."
		} > "${debroot}/DEBIAN/control"

		## Refresh the caches the desktop reads, and only those - nothing here
		## touches the user's own settings.
		cat > "${debroot}/DEBIAN/postinst" <<-'EOF'
			#!/bin/sh
			set -e
			if [ "$1" = configure ]; then
				update-desktop-database -q /usr/share/applications 2>/dev/null || true
				gtk-update-icon-cache -qtf /usr/share/icons/hicolor 2>/dev/null || true
			fi
		EOF
		cp "${debroot}/DEBIAN/postinst" "${debroot}/DEBIAN/postrm"
		sed -i 's/= configure/= remove/' "${debroot}/DEBIAN/postrm"
		chmod 755 "${debroot}/DEBIAN/postinst" "${debroot}/DEBIAN/postrm"

		deb="${OUT}/${SLUG}-${ver}-linux-${arch}.deb"
		rm -f "$deb"
		if fakeroot dpkg-deb --build --root-owner-group "$debroot" "$deb" >/dev/null 2>&1 ||
		   dpkg-deb --build --root-owner-group "$debroot" "$deb" >/dev/null; then
			fEcho "OK: $(basename "$deb") ($(fSize "$deb"))"
			made=$((made + 1))
		else
			fEcho "WARNING: .deb build failed"
		fi
	else
		fEcho "WARNING: dpkg-deb not installed; .deb skipped"
	fi
fi


#••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••••
# .rpm

if ((do_rpm)); then
	if command -v rpmbuild >/dev/null 2>&1; then
		fEcho_Clean
		fEcho "Building .rpm"
		rpmroot="${work}/rpm"
		build_tree "$rpmroot"
		mkdir -p "${work}/rpmbuild/"{BUILD,RPMS,SOURCES,SPECS,SRPMS}
		spec="${work}/rpmbuild/SPECS/${SLUG}.spec"
		{
			echo "%global _build_id_links none"
			echo "%global __os_install_post %{nil}"   ## the binaries are already stripped
			## rpm reads SOURCE_DATE_EPOCH but ignores it unless told to. Without these
			## the header carries the clock and every file gets a fresh mtime.
			echo "%global source_date_epoch_from_changelog 0"
			echo "%global use_source_date_epoch_as_buildtime 1"
			echo "%global clamp_mtime_to_source_date_epoch 1"
			## Otherwise the header records the build container's hostname, which is
			## a fresh random string every time the container is recreated.
			echo "%global _buildhost ${SLUG}-build"
			echo "Name:           ${SLUG}"
			echo "Version:        ${rpm_ver}"
			echo "Release:        1"
			echo "Summary:        ${NAME} file manager"
			echo "License:        GPL-2.0-only"
			echo "URL:            https://github.com/yottacore/${SLUG}"
			echo "BuildArch:      ${rpm_arch}"
			echo ""
			echo "%description"
			echo "A portable fork of Nemo that runs on any desktop, or none."
			echo "Installs alongside an existing Nemo without conflicting with it."
			echo ""
			echo "%install"
			echo "cp -a ${rpmroot}/. %{buildroot}/"
			echo ""
			echo "%files"
			echo "/opt/${SLUG}"
			echo "/usr/bin/${SLUG}"
			echo "/usr/share/applications/${SLUG}.desktop"
			echo "/usr/share/icons/hicolor/*/apps/${SLUG}.*"
			echo ""
			echo "%post"
			echo "update-desktop-database -q /usr/share/applications 2>/dev/null || :"
			echo "gtk-update-icon-cache -qtf /usr/share/icons/hicolor 2>/dev/null || :"
			echo ""
			echo "%postun"
			echo "update-desktop-database -q /usr/share/applications 2>/dev/null || :"
			echo "gtk-update-icon-cache -qtf /usr/share/icons/hicolor 2>/dev/null || :"
		} > "$spec"

		if rpmbuild --define "_topdir ${work}/rpmbuild" \
		            --define "_rpmdir ${work}/rpmbuild/RPMS" \
		            --target "${rpm_arch}" -bb "$spec" >/dev/null 2>&1; then
			built="$(find "${work}/rpmbuild/RPMS" -name '*.rpm' -type f | head -1)"
			if [[ -n "$built" ]]; then
				rpm="${OUT}/${SLUG}-${ver}-linux-${arch}.rpm"
				mv -f "$built" "$rpm"
				fEcho "OK: $(basename "$rpm") ($(fSize "$rpm"))"
				made=$((made + 1))
			else
				fEcho "WARNING: rpmbuild produced no package"
			fi
		else
			fEcho "WARNING: .rpm build failed"
		fi
	else
		fEcho "WARNING: rpmbuild not installed; .rpm skipped"
	fi
fi

fEcho_Clean
fEcho "${made} package(s) in cicd/artifacts/release"
fEcho_Clean


##	History:
##		- 2026-08-04: Created. Replaces the cargo-shaped package stage the engine
##		  carried over, which never applied to a meson build.
