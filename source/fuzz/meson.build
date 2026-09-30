# Fuzz targets for the parsers that read text from outside the program.
#
# The same files build two ways. Ordinarily fuzz-main.c is compiled in and each
# target is registered as a test that replays its seed corpus, so the targets
# keep building and the seeds keep meaning something on every suite run - a
# fuzz target nobody runs is a file that rots. With -Dfuzzing=true, clang and
# libFuzzer take over: libFuzzer brings its own main, so the driver is left
# out, and cicd/linux/fuzz.bash runs the real search.

# Replaying seeds through a plain build only proves the targets still run: an
# over-read walks off the end of the buffer and nothing says so. Backing the
# drag parser's bookkeeping fix out was seen to pass clean without this and to
# be caught with it, so the corpus tests carry AddressSanitizer themselves
# rather than waiting for somebody to set -Db_sanitize for the whole tree. The
# instrumented target is enough: ASan's allocator sees the overflow wherever the
# read happens. Dropped where the toolchain has no ASan, which leaves the
# targets as compile-and-run checks there.
fuzzSan = []
if cc.has_link_argument('-fsanitize=address')
  fuzzSan = [ '-fsanitize=address', '-fno-omit-frame-pointer' ]
endif

fuzzCArgs    = fuzzSan
fuzzLinkArgs = fuzzSan
fuzzDriver   = [ 'fuzz-main.c' ]

if get_option('fuzzing')
  # libFuzzer's own flag replaces the above; fuzz.bash asks for ASan alongside it.
  fuzzCArgs    = [ '-fsanitize=fuzzer' ]
  fuzzLinkArgs = [ '-fsanitize=fuzzer' ]
  fuzzDriver   = []
endif

# The settings parser is vendored and header-only, so this one links nothing
# but libm, which its integer parsing reaches for.
fuzzShcl = executable('fuzz-shcl',
  [ 'fuzz-shcl.c' ] + fuzzDriver,
  include_directories: [ shclInclude, ],
  dependencies: [ math ],
  c_args:    fuzzCArgs,
  link_args: fuzzLinkArgs,
)

fuzzDnd = executable('fuzz-dnd',
  [ 'fuzz-dnd.c' ] + fuzzDriver,
  include_directories: [ rootInclude, ],
  dependencies: [ gtk, nemo_private ],
  c_args:    fuzzCArgs,
  link_args: fuzzLinkArgs,
)

fuzzCommandTemplate = executable('fuzz-command-template',
  [ 'fuzz-command-template.c' ] + fuzzDriver,
  include_directories: [ rootInclude, ],
  dependencies: [ gtk, nemo_private ],
  c_args:    fuzzCArgs,
  link_args: fuzzLinkArgs,
)

fuzzPsd = executable('fuzz-psd',
  [ 'fuzz-psd.c' ] + fuzzDriver,
  include_directories: [ rootInclude, ],
  dependencies: [ gtk, nemo_private ],
  c_args:    fuzzCArgs,
  link_args: fuzzLinkArgs,
)

fuzzRaw = executable('fuzz-raw',
  [ 'fuzz-raw.c' ] + fuzzDriver,
  include_directories: [ rootInclude, ],
  dependencies: [ gtk, nemo_private ],
  c_args:    fuzzCArgs,
  link_args: fuzzLinkArgs,
)

# The shortcut reader is left out of the Windows build, which asks the shell.
if not is_windows
  fuzzLnk = executable('fuzz-lnk',
    [ 'fuzz-lnk.c' ] + fuzzDriver,
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private ],
    c_args:    fuzzCArgs,
    link_args: fuzzLinkArgs,
  )
endif

# The three binary Office parsers. Each helper is a program of its own, so the
# target includes its source whole and renames the main out of the way rather
# than turning the parser into a library nothing else would use. The zip-of-xml
# helper is left out: libgsf does the parsing there, and fuzzing it would be
# fuzzing libgsf.
fuzzXls = executable('fuzz-xls',
  [ 'fuzz-xls.c' ] + fuzzDriver,
  dependencies: [ libgsf, gio, glib ],
  c_args:    fuzzCArgs,
  link_args: fuzzLinkArgs,
)

fuzzPpt = executable('fuzz-ppt',
  [ 'fuzz-ppt.c' ] + fuzzDriver,
  dependencies: [ libgsf, gio, glib ],
  c_args:    fuzzCArgs,
  link_args: fuzzLinkArgs,
)

fuzzDoc = executable('fuzz-doc',
  [ 'fuzz-doc.c' ] + fuzzDriver,
  dependencies: [ libgsf, gio, glib ],
  c_args:    fuzzCArgs,
  link_args: fuzzLinkArgs,
)

# Listed rather than globbed: a seed earns its place by standing for a case, so
# adding one is worth a line here saying it was meant.
fuzzShclCorpus = files(
  'corpus/shcl/empty',
  'corpus/shcl/basic.shcl',
  'corpus/shcl/junk.shcl',
  'corpus/shcl/repeats.shcl',
  'corpus/shcl/deep.shcl',
  'corpus/shcl/binary',
)

fuzzDndCorpus = files(
  'corpus/dnd/empty',
  'corpus/dnd/one-plain',
  'corpus/dnd/two-plain',
  'corpus/dnd/geometry',
  'corpus/dnd/truncated',
  'corpus/dnd/no-cr',
  'corpus/dnd/mixed',
)

fuzzCommandTemplateCorpus = files(
  'corpus/command-template/empty',
  'corpus/command-template/plain',
  'corpus/command-template/unbalanced-quote',
  'corpus/command-template/list-inside',
  'corpus/command-template/unknown-token',
  'corpus/command-template/drops-out',
  'corpus/command-template/backslashes',
)

fuzzPsdCorpus = files(
  'corpus/psd/empty',
  'corpus/psd/rgb-raw',
  'corpus/psd/rgb-packed-alpha',
  'corpus/psd/psb-16bit',
  'corpus/psd/gray-shrunk',
  'corpus/psd/indexed',
  'corpus/psd/cmyk',
  'corpus/psd/truncated',
  'corpus/psd/bad-count',
  'corpus/psd/short-literal',
  'corpus/psd/zero-rows',
)

fuzzRawCorpus = files(
  'corpus/raw/empty',
  'corpus/raw/tiff-le',
  'corpus/raw/tiff-be',
  'corpus/raw/tiff-loop',
  'corpus/raw/panasonic-magic',
  'corpus/raw/raf',
  'corpus/raw/cr3',
  'corpus/raw/truncated',
  'corpus/raw/bad-offset',
)

fuzzXlsCorpus = files(
  'corpus/xls/empty',
  'corpus/xls/bof-eof',
  'corpus/xls/label',
  'corpus/xls/sst-strings',
  'corpus/xls/sst-truncated',
  'corpus/xls/numbers',
  'corpus/xls/long-record',
  'corpus/xls/junk',
)

fuzzPptCorpus = files(
  'corpus/ppt/empty',
  'corpus/ppt/text-bytes',
  'corpus/ppt/text-chars',
  'corpus/ppt/nested',
  'corpus/ppt/deep',
  'corpus/ppt/truncated',
)

fuzzDocCorpus = files(
  'corpus/doc/empty',
  'corpus/doc/short',
  'corpus/doc/one-piece',
  'corpus/doc/utf16-piece',
  'corpus/doc/prop-blob',
  'corpus/doc/bad-offset',
  'corpus/doc/truncated',
)

fuzzLnkCorpus = files(
  'corpus/lnk/empty',
  'corpus/lnk/local',
  'corpus/lnk/folder',
  'corpus/lnk/unicode-info',
  'corpus/lnk/network',
  'corpus/lnk/ansi-strings',
  'corpus/lnk/relative-only',
  'corpus/lnk/truncated',
  'corpus/lnk/bad-info-size',
)

# Under -Dfuzzing there is no main to hand a file list to; the targets are
# driven by fuzz.bash instead.
#
# No colons in these names: meson reads "a:b" as suite:test, so a name carrying
# one cannot be picked out on the command line.
#
# GLib and GTK hold allocations for the life of the process by design, so leak
# checking is off - these targets are looking for a bad access, not a leak.
if not get_option('fuzzing')
  fuzzTestEnv = [ 'ASAN_OPTIONS=detect_leaks=0' ]

  test('rh0808b8 Settings parser fuzz corpus test',  fuzzShcl,            args: fuzzShclCorpus,            env: fuzzTestEnv)
  test('rh0808b9 Drag payload fuzz corpus test',     fuzzDnd,             args: fuzzDndCorpus,             env: fuzzTestEnv)
  test('rh0808ba Command template fuzz corpus test', fuzzCommandTemplate, args: fuzzCommandTemplateCorpus, env: fuzzTestEnv)
  test('rhe0xz30 Photoshop fuzz corpus test',        fuzzPsd,             args: fuzzPsdCorpus,             env: fuzzTestEnv)
  test('rhqmm0ar Camera raw fuzz corpus test',       fuzzRaw,             args: fuzzRawCorpus,             env: fuzzTestEnv)
  test('rhatwe0r Workbook fuzz corpus test',         fuzzXls,             args: fuzzXlsCorpus,             env: fuzzTestEnv)
  test('rhatwe0s Presentation fuzz corpus test',     fuzzPpt,             args: fuzzPptCorpus,             env: fuzzTestEnv)
  test('rhatwe0t Word document fuzz corpus test',    fuzzDoc,             args: fuzzDocCorpus,             env: fuzzTestEnv)
  if not is_windows
    test('rhmxm5ag Windows shortcut fuzz corpus test', fuzzLnk,           args: fuzzLnkCorpus,             env: fuzzTestEnv)
  endif
endif
