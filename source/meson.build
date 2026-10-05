# Meson build file

# https://github.com/linuxmint/nemo
project('nemo-anywhere', 'c', version : '1.0.0-beta2', meson_version : '>=0.56.0',
  default_options : [ 'c_std=c17', 'warning_level=2' ])

# Install slug: co-install alongside upstream Nemo without file/bus/schema clashes.
appSlug = 'nemo-anywhere'

# 1. If the library code has changed at all since last release, then increment revision.
# 2. If any interfaces have been added, then increment current and set revision to 0.
# Interface break is not allowed.
nemo_extension_current  = 5
nemo_extension_revision = 0

# We need to decrement current by one in the calculation of the age because
# the library was started with version "1:0:0" instead of "0:0:0"
NEMO_EXTENSION_VERSION_INFO = '@0@:@1@:@2@'.format(
  nemo_extension_current, nemo_extension_revision, nemo_extension_current - 1
)

################################################################################
conf = configuration_data()

gnome = import('gnome')
pkgconfig = import('pkgconfig')

cc = meson.get_compiler('c')
prefix = get_option('prefix')
buildtype = get_option('buildtype')

# Surround the version in quotes to make it a C string
conf.set_quoted('VERSION', meson.project_version())

# Build number: minutes since 2000 in Crockford base32. Worked out at configure
# time, so a build in a tree that has moved on needs a reconfigure to catch up -
# both release lanes do one every run. It gets a header of its own rather than a
# place in config.h, which everything includes: the number moves on every
# reconfigure, and only a handful of files ever show it.
py = import('python').find_installation()
buildConf = configuration_data()
buildConf.set_quoted('NEMO_VERSION_STRING',
  'v' + meson.project_version() + ' build ' +
  run_command(py, files('build-number.py'), check: true).stdout().strip())
configure_file(output: 'nemo-build-number.h', configuration: buildConf)

check_headers = [
  'malloc.h',
  'sys/mount.h',
  'sys/param.h',
  'sys/vfs.h',
  'X11/XF86keysym.h',
]

foreach h : check_headers
  conf.set10('HAVE_' + h.underscorify().to_upper(), cc.has_header(h))
endforeach

conf.set10('HAVE_MALLOPT', cc.has_function('mallopt', prefix: '#include <malloc.h>'))

# The crash report's backtrace. In glibc it comes with libc; the BSDs keep it in
# a separate libexecinfo, and a musl build has neither. Testing the header alone
# would find it on FreeBSD and then fail to link.
execinfo = []
have_backtrace = cc.has_function('backtrace', prefix: '#include <execinfo.h>')
if not have_backtrace
  libexecinfo = cc.find_library('execinfo', required: false)
  if libexecinfo.found() and cc.has_function('backtrace',
      prefix: '#include <execinfo.h>', dependencies: libexecinfo)
    have_backtrace = true
    execinfo = libexecinfo
  endif
endif
conf.set10('HAVE_BACKTRACE', have_backtrace)


# Strict c17 turns off the POSIX and BSD calls glibc hands out by default, and the
# tree uses plenty (symlink, lstat, realpath). This asks for the same set
# gnu17 gave, with no GNU language extensions.
add_project_arguments('-D_DEFAULT_SOURCE', language: 'c')

# meson keeps a build dir's old defaults on --reconfigure, so one set up before
# the std was named would go on building gnu17 with fewer warnings.
if get_option('c_std') == 'none'
  error('this build dir predates c_std=c17. Run: meson setup --wipe <builddir> <srcdir>')
endif

if not get_option('deprecated_warnings')
  add_global_arguments([
      '-Wno-deprecated-declarations',
      '-Wno-deprecated',
      '-Wno-declaration-after-statement',
      '-DGLIB_DISABLE_DEPRECATION_WARNINGS',
    ],
    language: 'c',
  )
endif

if buildtype in ['debug', 'debugoptimized']
    conf.set('ENABLE_DEBUG', true)
endif

if buildtype == 'debug'
    add_global_arguments('-fno-omit-frame-pointer', language: 'c')
endif

################################################################################
# Find dependencies

glib_version = '>=2.45.7'
is_windows = host_machine.system() == 'windows'
is_macos = host_machine.system() == 'darwin'

# Targets unlikely to have a GTK theme installed carry their own set; Linux and
# the BSDs use whatever the desktop already provides.
bundledThemes = get_option('bundled_themes')
if bundledThemes == 'auto'
  bundledThemes = (is_windows or is_macos) ? 'true' : 'false'
endif

math    = cc.find_library('m', required: true)

gtk     = dependency('gtk+-3.0',                    version: '>=3.10.0')
gio     = dependency('gio-2.0',                     version: glib_version)
glib    = dependency('glib-2.0',                    version: glib_version)
gmodule = dependency('gmodule-no-export-2.0',       version: glib_version)
gobject = dependency('gobject-2.0',                 version: '>=2.0')
json    = dependency('json-glib-1.0',               version: '>=1.6')

# Archive creation. libarchive writes the tar, zip and 7z families; rar, and the
# options libarchive has no write support for (split volumes, solid blocks,
# encrypted headers), go out to a 7z or rar command when one is installed.
archive = dependency('libarchive',                  version: '>=3.2.0')

# Thumbnail cache. Asked for static so there is one fewer library to ship beside
# the Windows exe. It has to come through pkg-config rather than find_library:
# the cross sysroot is not on the compiler's own search path, and only the .pc
# file says where it is.
sqlite  = dependency('sqlite3',                     version: '>=3.20', static: true)

# Vendored code stays as it came, so it builds at -Wall rather than -Wextra.
vendorWarnings = [ 'warning_level=1' ]

# blake3, vendored under ../vendor. Checksums have to be cheap enough to compute
# while a folder is being listed, and this is the only option that both hits a
# few GB/s and fits in 100 KB of source - see vendor/README.md for the other two
# that were measured.
#
# Each instruction set is its own library, because each needs its own flag on
# the command line. Which of them actually runs is decided at run time by cpuid,
# so the flags never reach anything else. AVX-512 is left out: it is another
# 100 KB of code for a case nobody here hits, and on a good many parts it clocks
# the core down while it runs.
blake3Include = include_directories('../vendor/blake3')
blake3Defines = [ '-DBLAKE3_NO_AVX512' ]
blake3Simd = []

if host_machine.cpu_family() in [ 'x86', 'x86_64' ]
  foreach set : [ [ 'sse2', '-msse2' ], [ 'sse41', '-msse4.1' ], [ 'avx2', '-mavx2' ] ]
    blake3Simd += static_library('blake3-' + set[0],
      files('../vendor/blake3/blake3_' + set[0] + '.c'),
      c_args: blake3Defines + [ set[1] ],
      include_directories: blake3Include,
      override_options: vendorWarnings,
    )
  endforeach
else
  # No hand-written path for this machine, so ask for the plain C one. Without
  # the NEON line blake3 turns it on by itself on arm64 and then wants a source
  # file that is not vendored.
  blake3Defines += [ '-DBLAKE3_NO_SSE2', '-DBLAKE3_NO_SSE41', '-DBLAKE3_NO_AVX2',
                     '-DBLAKE3_USE_NEON=0' ]
endif

blake3 = declare_dependency(
  link_with: static_library('blake3',
    files('../vendor/blake3/blake3.c',
          '../vendor/blake3/blake3_dispatch.c',
          '../vendor/blake3/blake3_portable.c'),
    c_args: blake3Defines,
    include_directories: blake3Include,
    link_with: blake3Simd,
    override_options: vendorWarnings,
  ),
  include_directories: blake3Include,
)

gail    = dependency('gail-3.0')

# Unix/X11-only deps, absent from the Windows (mingw) sysroot. Empty placeholders on
# Windows keep the downstream dependency lists intact; the C code guards the actual
# API use with #ifdef (GDK_WINDOWING_X11, G_OS_UNIX).
if is_windows
  gio_unix = declare_dependency()
  x11      = declare_dependency()
  go_intr  = declare_dependency()
  # shell/COM libs for the Recycle Bin trash backend, mpr for network enumeration,
  # advapi32 for the registry (system light/dark theme follow), version for a
  # program's own description in the Open With menu, wbemuuid for starting a
  # program through the management service, netapi32 for an owner's full name
  win_shell = declare_dependency(link_args: ['-lshell32', '-lole32', '-loleaut32', '-lshlwapi', '-luuid', '-lwbemuuid', '-lmpr', '-ladvapi32', '-lversion', '-lnetapi32'])
else
  gio_unix = dependency('gio-unix-2.0',              version: glib_version)
  x11      = dependency('x11')
  go_intr  = dependency('gobject-introspection-1.0', version: '>=1.0')
  win_shell = declare_dependency()
endif

# Facultative dependencies

trackerChoice = get_option('tracker')
tracker_enabled = false
if trackerChoice != 'false'
  trackerRequired = (trackerChoice == 'true')
  # Check all the possible versions
    tracker_sparql = dependency('tracker-sparql-3.0',  required: false)
  if not tracker_sparql.found()
    tracker_sparql = dependency('tracker-sparql-2.0',  required: false)
  endif
  if not tracker_sparql.found()
    tracker_sparql = dependency('tracker-sparql-1.0',  required: false)
  endif
  if not tracker_sparql.found()
    tracker_sparql = dependency('tracker-sparql-0.18', required: false)
  endif
  if not tracker_sparql.found()
    tracker_sparql = dependency('tracker-sparql-0.16', required: trackerRequired)
  endif

  tracker_enabled = trackerRequired or tracker_sparql.found()
endif
conf.set('ENABLE_TRACKER', tracker_enabled)


gtkdoc_enabled = get_option('gtk_doc')
if gtkdoc_enabled
  find_program('gtkdoc-scan', required: true)
endif


libexif_enabled = get_option('exif')
if libexif_enabled
  libexif = dependency('libexif',                   version: '>=0.6.20')
endif
conf.set('HAVE_EXIF', libexif_enabled)

exempi_enabled = get_option('xmp')
if exempi_enabled
  exempi = dependency('exempi-2.0',                 version: '>=2.2.0')
endif
conf.set('HAVE_EXEMPI', exempi_enabled)

libselinux_enabled = get_option('selinux')
if libselinux_enabled
  libselinux = dependency('libselinux',             version: '>=2.0')
endif
conf.set('HAVE_SELINUX', libselinux_enabled)

# make sure pango development files are installed
pango = dependency('pango', version: '>=1.40.0')
# check for newer pango for necessary workarounds
new_pango = dependency('pango', version: '>=1.44.0', required: false)
if new_pango.found()
  conf.set('HAVE_PANGO_144', true)
else
  message('................using pango @0@ instead'.format(new_pango.version()))
endif

enableEmptyView = get_option('empty_view')
conf.set10('ENABLE_EMPTY_VIEW', enableEmptyView)

conf.set_quoted('GETTEXT_PACKAGE', appSlug)
conf.set_quoted('LOCALE_DIR',  join_paths(get_option('prefix'), get_option('localedir')))
conf.set_quoted('LOCALEDIR',   join_paths(get_option('prefix'), get_option('localedir')))
conf.set('ENABLE_NLS', cc.has_header('libintl.h'))
conf.set('HAVE_GETTEXT', true)
conf.set('HAVE_LOCALE_H', cc.has_header('locale.h'))

configure_file(
  input : 'config.h.meson.in',
  output: 'config.h',
  configuration: conf
)

################################################################################

rootInclude = include_directories('.')
shclInclude = include_directories('../vendor/shcl')
nemoDataPath      = join_paths(get_option('prefix'), get_option('datadir'), appSlug)
nemoExtensionPath = join_paths(get_option('prefix'), get_option('libdir'), appSlug, 'extensions-3.0')

# The installed program has no wrapper script in front of it, so it has to find
# the extension library relative to itself. libdir carries the multiarch triplet
# on Debian and friends, which is why this is not a fixed '../lib'.
nemoRpath = join_paths('$ORIGIN', '..', get_option('libdir'))

nemo_definitions =  [
  '-DNEMO_DATADIR="@0@"'.format(nemoDataPath),
  '-DNEMO_EXTENSIONDIR="@0@"'.format(nemoExtensionPath),
  '-DNEMO_APP_SLUG="@0@"'.format(appSlug),
  '-DG_LOG_DOMAIN="Nemo Anywhere"'
]

po_subdir = join_paths(meson.project_source_root(), 'po')

subdir('install-scripts')
subdir('cut-n-paste-code/libegg')
subdir('data')
subdir('eel')
subdir('files')
subdir('gresources')
subdir('libnemo-extension')
subdir('libnemo-private')
# subdir('po')
subdir('src')
subdir('search-helpers')

# No test needs a session bus, and connecting to a stale one hangs the run - the
# address file outlives the bus it names. A copy hung here for 30s that way.
add_test_setup('nobus',
  is_default: true,
  env: [ 'DBUS_SESSION_BUS_ADDRESS=disabled:' ],
)

subdir('test')
subdir('fuzz')
subdir('docs')
# A /bin/sh launcher and a PyGObject script; neither ships on Windows.
if not is_windows
  subdir('action-layout-editor')
endif

message('\n'.join(['',
'        @0@-@1@'.format(meson.project_name(), meson.project_version()),
'',
'    prefix:                 @0@'.format(prefix),
'    source code location:   @0@'.format(meson.project_source_root()),
'    compiler:               @0@'.format(cc.get_id()),
'    libexif support:        @0@'.format(libexif_enabled),
'    exempi  support:        @0@'.format(exempi_enabled),
'    Tracker support:        @0@'.format(tracker_enabled),
'    Wayland support:        @0@'.format(cc.has_header('gdk/gdkwayland.h', dependencies: gtk)),
'',
'    nemo-extension documentation: @0@'.format(gtkdoc_enabled),
'    nemo-extension introspection: @0@'.format(true),
'',
'    debugging support:      @0@'.format(buildtype),
'    perf profiling support: @0@'.format(buildtype == 'debug'),
'',
]))
