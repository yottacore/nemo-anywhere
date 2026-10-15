
nemo_i18n_header = [
  'nemo-extension-i18n.h'
]

nemo_extension_sources = [
  'nemo-column-provider.c',
  'nemo-column.c',
  'nemo-extension-private.h',
  'nemo-file-info.c',
  'nemo-info-provider.c',
  'nemo-location-widget-provider.c',
  'nemo-menu-item.c',
  'nemo-menu-provider.c',
  'nemo-name-and-desc-provider.c',
  'nemo-property-page-provider.c',
  'nemo-property-page.c',
  'nemo-menu.c',
  'nemo-simple-button.c',
]

nemo_extension_headers = [
  'nemo-column-provider.h',
  'nemo-column.h',
  'nemo-extension-types.h',
  'nemo-file-info.h',
  'nemo-info-provider.h',
  'nemo-location-widget-provider.h',
  'nemo-menu-item.h',
  'nemo-menu-provider.h',
  'nemo-name-and-desc-provider.h',
  'nemo-property-page-provider.h',
  'nemo-property-page.h',
  'nemo-menu.h',
  'nemo-simple-button.h',
]

nemo_extension_incdir = include_directories('.')

nemo_extension_deps = [ glib, gtk ]

# On Windows nemo loads no external plugins (see nemo-module.c), so the exe is the
# only consumer of these symbols - fold them straight in as a static lib. Linux
# ships the shared API that third-party extensions link against, unless
# extension_library=static, where the exe carries the whole API and exports it.
extensionShared = not is_windows and get_option('extension_library') == 'shared'
extensionExported = not is_windows and not extensionShared

if extensionShared
  nemo_extension_lib = shared_library('nemo-anywhere-extension',
    nemo_extension_sources + nemo_i18n_header,
    dependencies: nemo_extension_deps,
    include_directories: [ rootInclude, ],
    install: true,
    version: '1.4.0',
    # A fuzzing build instruments every object, but only the final fuzz binary
    # gets the sanitizer runtimes, so this library is left with __asan_* and
    # __sancov_* unresolved. Nothing loads it there - the fuzz targets link the
    # static libnemo-private and never touch it - so the check comes off for
    # that build flavor rather than the shipped one.
    # ld64 has no -no-undefined, and errors on an undefined symbol anyway.
    link_args: (get_option('fuzzing') or is_macos) ? [] : [ '-Wl,-no-undefined' ],
  )
else
  nemo_extension_lib = static_library('nemo-anywhere-extension',
    nemo_extension_sources + nemo_i18n_header,
    dependencies: nemo_extension_deps,
    include_directories: [ rootInclude, ],
    install: false,
  )
endif

# Public dev artifacts (headers for third-party extensions) - Linux only; Windows
# has no external plugins to build against.
if not is_windows
  install_headers(nemo_extension_headers,
    subdir: join_paths(appSlug, 'libnemo-extension')
  )
endif

if extensionExported
  # Every member goes in, since an extension may call what nemo itself never
  # does. Only the API is exported, not the rest of the exe's symbols, which
  # would cost more than the shared library it replaces.
  # ld64 takes a plain list of names instead of a dynamic list.
  nemo_extension_dynlist = custom_target('nemo-extension-dynlist',
    input: nemo_extension_lib,
    output: 'nemo-extension.dynlist',
    command: [ py, files('dynamic-list.py'), find_program('nm'), '@INPUT@', '@OUTPUT@' ]
      + (is_macos ? [ 'ld64' ] : []),
  )
  nemo_extension = declare_dependency(
    include_directories: include_directories('.'),
    link_whole: [ nemo_extension_lib ],
    link_args: [ is_macos
      ? '-Wl,-exported_symbols_list,' + nemo_extension_dynlist.full_path()
      : '-Wl,--dynamic-list=' + nemo_extension_dynlist.full_path() ],
    sources: [ nemo_extension_dynlist ],
    dependencies: nemo_extension_deps,
  )
else
  nemo_extension = declare_dependency(
    include_directories: include_directories('.'),
    link_with: [ nemo_extension_lib ],
    dependencies: nemo_extension_deps,
  )
endif

# GObject-introspection is Linux-only here: the mingw sysroot ships no g-ir-scanner
# and cross introspection isn't worth it. Skip GIR/typelib generation on Windows.
if extensionShared
  typelibdir = go_intr.get_variable(pkgconfig: 'typelibdir',
    pkgconfig_define: ['libdir', join_paths(get_option('prefix'), get_option('libdir'))]
  )

  # GIR/typelib deferred: a distinct GI namespace would rename the introspection
  # API. Build it but don't install, so nothing clashes with upstream Nemo-3.0.typelib.
  gnome.generate_gir(nemo_extension_lib,
    sources: nemo_extension_sources + nemo_extension_headers,
    nsversion: '3.0',
    namespace: 'Nemo',
    export_packages: 'libnemo-anywhere-extension',
    includes: [ 'Gtk-3.0', 'Gio-2.0', 'GLib-2.0', ],
    include_directories: [ rootInclude, ],
    install_dir_typelib: typelibdir,
    install: false,
  )
endif

if extensionShared
  pkgconfig.generate(filebase: 'libnemo-anywhere-extension',
    name: 'libnemo-anywhere-extension',
    description: 'A library to create Nemo Anywhere view extensions',
    version: meson.project_version(),
    requires: [ 'gio-2.0', 'glib-2.0', 'gtk+-3.0' ],
    libraries: nemo_extension_lib,
    subdirs: appSlug,
    variables: 'extensiondir=${libdir}/@0@/@1@'.format(appSlug, 'extensions-3.0'),
  )
endif

