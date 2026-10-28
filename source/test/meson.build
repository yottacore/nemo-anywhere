
# Refuses a delete or a move outside the test's own folders, from before main
# on. Linked whole, since nothing calls into it by name; every test executable
# has it, through test_scratch or on its own.
test_fence = declare_dependency(
  link_whole: static_library('test-fence',
    [ 'test-fence.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ cc.find_library('dl', required: false), dependency('threads') ],
  ),
)

# Scratch directories made through this are removed when the test exits.
test_scratch = declare_dependency(
  link_with: static_library('test-scratch',
    [ 'test-scratch.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private ],
  ),
  dependencies: [ test_fence ],
)

# The compound file the old Word, Excel and PowerPoint files are kept in,
# written with no library.
test_ole2 = declare_dependency(
  link_with: static_library('test-ole2-writer',
    [ 'test-ole2-writer.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ glib ],
  ),
)

# The heap in use and its growth over repeated operations, for the leak tests.
test_heap = declare_dependency(
  link_with: static_library('test-heap',
    [ 'test-heap.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ glib, gobject ],
  ),
)

test('2zfbv Search Engine test',
  executable('test-nemo-search-engine',
    [ 'test-nemo-search-engine.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: [],
  timeout: 180        # three searches, each with its own 30s ceiling
)

test('dcnvg Search folder test',
  executable('test-nemo-search-folder',
    [ 'test-nemo-search-folder.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: [],
  timeout: 120
)

test('dcdd9 Directory monitor test',
  executable('test-nemo-directory-monitor',
    [ 'test-nemo-directory-monitor.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: [],
  timeout: 120
)

test('dd248 Folder time after a change inside it test',
  executable('test-nemo-folder-mtime',
    [ 'test-nemo-folder-mtime.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: [],
  timeout: 60
)

test('dd24j Small file copy test',
  executable('test-nemo-small-copy',
    [ 'test-nemo-small-copy.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('dd5da Drop action from another program test',
  executable('test-nemo-drop-action',
    [ 'test-nemo-drop-action.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('dc54g Drag drop confirm test',
  executable('test-nemo-drag-confirm',
    [ 'test-nemo-drag-confirm.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('dc8tz Config defaults list test',
  executable('test-nemo-config-catalog',
    [ 'test-nemo-config-catalog.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('dav3s Config store test',
  executable('test-nemo-config',
    [ 'test-nemo-config.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch, test_heap ],
  ),
  args: [],
  timeout: 90         # a hundred external rewrites, each waiting on the file monitor
)

test('ddy4b Bulk rename default test',
  executable('test-nemo-bulk-rename',
    [ 'test-nemo-bulk-rename.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

# Converting runs when the store opens, so the old file goes in before that.
test('ddez6 Config format upgrade test',
  executable('test-nemo-config-format',
    [ 'test-nemo-config-format.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: [],
  timeout: 60
)

# One writer per SHCL release from before format 3, each built against that
# release's header alone, so the old files come from the old code.
shclOldWriters = []
foreach release : [ '1.2.0', '2.0.0' ]
  shclOldWriters += [ release, executable('shcl-old-writer-' + release,
    [ 'shcl-old-writer.c' ],
    include_directories: include_directories('../../vendor/shcl-old' / release),
    dependencies: [ math ],
    override_options: [ 'warning_level=0' ],
  ) ]
endforeach

test('ddf4w Config old formats test',
  executable('test-nemo-config-old-formats',
    [ 'test-nemo-config-old-formats.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: shclOldWriters,
  timeout: 60
)

# Counts allocations through its own malloc, which only glibc allows.
if not is_windows
  test('ddatz Allocations per read test',
    executable('test-nemo-read-allocations',
      [ 'test-nemo-read-allocations.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )
endif

test('dcrhe Delete test guard arming test',
  executable('test-nemo-delete-testguard',
    [ 'test-nemo-delete-testguard.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('dd2q6 Guard pauses the job test',
  executable('test-nemo-guard-pause',
    [ 'test-nemo-guard-pause.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('dd5ev Job queue test',
  executable('test-nemo-job-queue',
    [ 'test-nemo-job-queue.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

# The bus half of the link between copies. Windows has no bus; its pipes are
# ddqdq's.
if not is_windows
  test('ddmt8 Instance list test',
    executable('test-nemo-instance-list',
      [ 'test-nemo-instance-list.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: [],
    timeout: 60
  )

  test('ddqa7 Settings shared between copies test',
    executable('test-nemo-config-share',
      [ 'test-nemo-config-share.c' ],
      include_directories: [ rootInclude, shclInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: [],
    timeout: 120
  )
endif

# The heap is read through glibc or the Windows heap walk, so elsewhere these
# skip.
leaks = executable('test-nemo-leaks',
  [ 'test-nemo-leaks.c', '../src/nemo-query-editor.c', '../src/nemo-list-model.c' ] + gresources,
  include_directories: [ rootInclude, include_directories('../src') ],
  dependencies: [ gtk, nemo_private, egg, test_scratch, test_heap ],
  c_args: nemo_definitions,
)

# On Windows each reading waits for the heap to sit still, COM traffic from
# outside can keep it moving, and growth has to show 4 times there, so a case
# can take twice its usual 15 s (2026101913541101).
leakTimeout = is_windows ? 90 : 30
test('ddep6 Job progress leak test', leaks, args: [ 'progress' ],
  timeout: leakTimeout)
test('ddep7 Move job leak test', leaks, args: [ 'move' ],
  timeout: leakTimeout)
test('ddepe Clipboard drag check leak test', leaks, args: [ 'clipboard' ],
  timeout: leakTimeout)
test('ddepg Stopped zip leak test', leaks, args: [ 'zip-cancel' ],
  timeout: leakTimeout)
test('ddeps Search query leak test', leaks, args: [ 'query-editor' ],
  timeout: leakTimeout)
test('ddept Bookmarks load leak test', leaks, args: [ 'bookmarks' ],
  timeout: leakTimeout)
test('ddepv Bookmarks file load leak test', leaks, args: [ 'bookmarks-file' ],
  timeout: leakTimeout)
test('ddeqn Extract job leak test', leaks, args: [ 'extract' ],
  timeout: leakTimeout)
test('dder1 Stopped tar.gz leak test', leaks, args: [ 'tar.gz-cancel' ],
  timeout: leakTimeout)
test('ddetk Stopped 7z leak test', leaks, args: [ '7z-cancel' ],
  timeout: leakTimeout)
test('ddfb8 Leak test self-check', leaks, args: [ 'self-check' ],
  timeout: leakTimeout)
test('de5he Settings save and reload leak test', leaks, args: [ 'settings' ],
  timeout: leakTimeout)
test('de5hf File checksum leak test', leaks, args: [ 'checksum' ],
  timeout: leakTimeout)
test('de5hg Multi-row drag leak test', leaks, args: [ 'drag' ],
  timeout: 180)       # every round waits out GDK's snap-back, about half a second

if not is_windows
  # The sanitizer lane's suppressions, held to what they name. Whole stacks,
  # added to the lane's own options.
  sanitizerLeaksEnv = environment()
  sanitizerLeaksEnv.append('ASAN_OPTIONS', 'fast_unwind_on_malloc=0', separator: ':')
  test('de5hh Sanitizer leak suppressions test',
    executable('test-sanitizer-leaks',
      [ 'test-sanitizer-leaks.c' ] + gresources + themeGresources,
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch, dependency('fontconfig') ],
      c_args: [ '-DVENDOR_THEMES_DIR="@0@"'.format(meson.project_source_root() / '..' / 'vendor' / 'themes') ],
    ),
    env: sanitizerLeaksEnv,
    timeout: 120,
  )
endif

test('dd5ew Cancelled drop leaves the files test',
  executable('test-nemo-drop-cancel',
    [ 'test-nemo-drop-cancel.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('dcrvs Empty trash asks test',
  executable('test-nemo-empty-trash',
    [ 'test-nemo-empty-trash.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('dcvpy Delete from trash asks test',
  executable('test-nemo-delete-from-trash',
    [ 'test-nemo-delete-from-trash.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('db9wp Favorites test',
  executable('test-nemo-favorites',
    [ 'test-nemo-favorites.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('dcy00 File checksum test',
  executable('test-nemo-file-digest',
    [ 'test-nemo-file-digest.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

# Links the app's own blake3 routines, with the dispatcher built again around
# them, so it can't link nemo_private, which has the first copy.
test('de5gz blake3 routines test',
  executable('test-blake3-paths',
    [ 'test-blake3-paths.c' ],
    include_directories: [ rootInclude, blake3Include, ],
    dependencies: [ glib, test_fence ],
    link_with: static_library('test-blake3-dispatch',
      [ 'test-blake3-dispatch.c',
        files('../../vendor/blake3/blake3.c', '../../vendor/blake3/blake3_portable.c') ],
      c_args: blake3Defines,
      include_directories: blake3Include,
      link_with: blake3Simd,
    ),
  ),
  args: []
)

test('dcy08 File checksum attribute test',
  executable('test-nemo-file-xattr',
    [ 'test-nemo-file-xattr.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('dcxzr File cache store test',
  executable('test-nemo-cache-db',
    [ 'test-nemo-cache-db.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch, sqlite ],
  ),
  args: []
)

test('ddf62 File cache opened by many at once test',
  executable('test-nemo-cache-open-race',
    [ 'test-nemo-cache-open-race.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch, sqlite ],
  ),
  args: [],
  timeout: 120
)

test('dcy2e File cache prune test',
  executable('test-nemo-cache-prune',
    [ 'test-nemo-cache-prune.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch, sqlite ],
  ),
  args: [],
  timeout: 120
)

test('dcy1f Thumbnail store test',
  executable('test-nemo-thumbnail-store',
    [ 'test-nemo-thumbnail-store.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('db9ys Thumbnail factory test',
  executable('test-nemo-thumbnail',
    [ 'test-nemo-thumbnail.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: [],
  timeout: 180        # the hung-helper case waits out the thumbnailer timeout
)

test('da8qa Metadata store test',
  executable('test-nemo-metadata-store',
    [ 'test-nemo-metadata-store.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

# Needs the slug define: the point of the test is the path the app really builds.
test('dbfh8 Config root test',
  executable('test-nemo-config-root',
    [ 'test-nemo-config-root.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
    c_args: nemo_definitions,
  ),
  args: []
)

test('ddamj EEL string check test',
  executable('test-eel-string',
    [ 'test-eel-string.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_fence ],
  ),
  args: []
)

test('ddamm Conflict dialog test',
  executable('test-nemo-conflict-dialog',
    [ 'test-nemo-conflict-dialog.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('ddymm Bad name characters test',
  executable('test-nemo-bad-name',
    [ 'test-nemo-bad-name.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

# Owner and group are POSIX ids.
if not is_windows
  test('de1ba Undo record refs test',
    executable('test-nemo-undo-refs',
      [ 'test-nemo-undo-refs.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )
endif

test('da8rh User input parse test',
  executable('test-eel-user-input',
    [ 'test-eel-user-input.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

# symlink(2) and the guard-page mmap below are POSIX; none of these cross-build.
if not is_windows
  test('dbcw2 Symlink recurse premise test',
    executable('test-nemo-symlink-recurse',
      [ 'test-nemo-symlink-recurse.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )

  test('dcm8y Delete guard test',
    executable('test-nemo-delete-guard',
      [ 'test-nemo-delete-guard.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )

  test('dcnvh Scratch cleanup test',
    executable('test-nemo-scratch',
      [ 'test-nemo-scratch.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )

  test('ddmry Scratch cleanup waits for the font cache test',
    executable('test-nemo-scratch-fonts',
      [ 'test-nemo-scratch-fonts.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )
endif

test('dedme Scratch cleanup of held folders test',
  executable('test-nemo-scratch-held',
    [ 'test-nemo-scratch-held.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('de5kb Delete fence test',
  executable('test-nemo-fence',
    [ 'test-nemo-fence.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: [],
  # 10 s of loads racing the fence on Windows.
  timeout: 60
)

test('dbcw3 Search invalid regex test',
  executable('test-nemo-search-regex',
    [ 'test-nemo-search-regex.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

if is_windows
  test('dcnwt Delete guard win32 test',
    executable('test-nemo-delete-guard-win32',
      [ 'test-nemo-delete-guard-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )

  # Takes the focus, so alone.
  test('debgn Delete key after a window on another thread test',
    executable('test-nemo-delete-focus-win32',
      [ 'test-nemo-delete-focus-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_fence ],
    ),
    args: [],
    is_parallel: false,
  )
endif

test('dbwjv Treeview hit test',
  executable('test-eel-treeview-hit',
    [ 'test-eel-treeview-hit.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_fence ],
  ),
  args: []
)

test('dd5db Clipped cell text test',
  executable('test-eel-clipped-cell',
    [ 'test-eel-clipped-cell.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_fence ],
  ),
  args: []
)

# Holds a menu open. On Windows any test's window coming up beside it takes the
# focus, and the menu closes with it, so alone there.
test('dd5dc Keyboard context menu test',
  executable('test-eel-context-menu',
    [ 'test-eel-context-menu.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: [],
  is_parallel: not is_windows,
)

test('ddgdf Rename field accessible test',
  executable('test-eel-editable-label-atk',
    [ 'test-eel-editable-label-atk.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('ddgf5 Rename field sized before shown test',
  executable('test-eel-editable-label-size',
    [ 'test-eel-editable-label-size.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_fence ],
  ),
  args: []
)

test('ddwjs Rename field takes a new width while hidden test',
  executable('test-eel-editable-label-rewidth',
    [ 'test-eel-editable-label-rewidth.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_fence ],
  ),
  args: []
)

test('dd5dd Escape forgets the cursor test',
  executable('test-eel-forget-cursor',
    [ 'test-eel-forget-cursor.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_fence ],
  ),
  args: []
)

# Takes the foreground. Every test shares one desktop on Windows, so alone there.
test('dcnqs Focus guard test',
  executable('test-eel-focus-guard',
    [ 'test-eel-focus-guard.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_fence ],
  ),
  args: [],
  is_parallel: not is_windows,
)

# Same, it takes the foreground.
test('ddxet Tabs take no focus helper test',
  executable('test-eel-tab-keys',
    [ 'test-eel-tab-keys.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_fence ],
  ),
  args: [],
  is_parallel: not is_windows,
)

test('dd2sk Row hover tint test',
  executable('test-nemo-row-hover',
    [ 'test-nemo-row-hover.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('dczsk Primary mask test',
  executable('test-eel-primary-mask',
    [ 'test-eel-primary-mask.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_fence ],
  ),
  args: []
)

test('dcd70 Entry context menu test',
  executable('test-nemo-entry-menu',
    [ 'test-nemo-entry-menu.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

# Its binary files are written from literals with NULs inside, and a wrong
# length reads past one with nothing to say so. ASan on this one executable
# sees that. Its leaks are left to the sanitizer lane, which has the
# suppressions for GTK's own; there the whole build has ASan and the lane's
# options stand, leak checks included.
testSan = []
searchContentEnv = []
if not is_windows and cc.has_link_argument('-fsanitize=address')
  testSan = [ '-fsanitize=address', '-fno-omit-frame-pointer' ]
endif
if not get_option('b_sanitize').contains('address')
  searchContentEnv = [ 'ASAN_OPTIONS=detect_leaks=0' ]
endif

test('dbwjj Search content test',
  executable('test-nemo-search-content',
    [ 'test-nemo-search-content.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
    c_args: nemo_definitions + testSan,
    link_args: testSan,
  ),
  env: searchContentEnv,
  args: []
)

if not is_windows
  test('dcwqk Scratch guard test',
    executable('test-scratch-guard',
      [ 'test-scratch-guard.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ glib, test_scratch ],
    ),
    args: []
  )

  test('dbcw4 DnD selection parser test',
    executable('test-nemo-dnd',
      [ 'test-nemo-dnd.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_fence ],
    ),
    args: []
  )
endif

test('dd5c8 Rename selection test',
  executable('test-eel-rename-region',
    [ 'test-eel-rename-region.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('dd5c9 Message dialog text test',
  executable('test-eel-stock-dialogs',
    [ 'test-eel-stock-dialogs.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_fence ],
  ),
  args: []
)

test('dbcw5 Terminal fallback test',
  executable('test-eel-terminal',
    [ 'test-eel-terminal.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('dbcw6 Extension column test',
  executable('test-nemo-column',
    [ 'test-nemo-column.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_extension, test_fence ],
  ),
  args: []
)

if is_windows
  # The recycle-bin monitor is polled, so the same-count case has to sit through
  # a few turns of it - well past the 30s meson allows by default. On a machine
  # with a full bin the whole run has been seen to take four and a half minutes.
  # It watches the real bin for a quiet spell, so nothing else may run beside
  # it: the archive combinations test recycles into that same bin.
  test('da8rb Trash win32 test',
    executable('test-nemo-trash-win32',
      [ 'test-nemo-trash-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_fence ],
    ),
    args: [],
    timeout: 600,
    is_parallel: false,
  )
endif

test('dcd27 Crash reporter test',
  executable('test-nemo-crash',
    [ 'test-nemo-crash.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: [],
  timeout: 120
)

if is_windows
  test('daqzg Shortcut win32 test',
    executable('test-nemo-shortcut-win32',
      [ 'test-nemo-shortcut-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, win_shell, test_scratch ],
    ),
    args: []
  )
endif

test('da8t3 GIO fileops probe',
  executable('test-gio-fileops',
    [ 'test-gio-fileops.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('3z0xv Copy test',
  executable('test-nemo-copy',
    [ 'test-copy.c', 'test.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
    c_args: nemo_definitions,
  ),
  args: []
)

link_copy_job = executable('test-nemo-link-copy-job',
  [ 'test-nemo-link-copy-job.c', 'test.c' ],
  include_directories: [ rootInclude, ],
  dependencies: [ gtk, nemo_private, test_scratch ],
  c_args: nemo_definitions,
)

test('dd42s Link copy job, every answer', link_copy_job, args: [])

make_link_job = executable('test-nemo-make-link-job',
  [ 'test-nemo-make-link-job.c', 'test.c' ],
  include_directories: [ rootInclude, ],
  dependencies: [ gtk, nemo_private, test_scratch ],
  c_args: nemo_definitions,
)

new_folder_path = executable('test-nemo-new-folder-path',
  [ 'test-nemo-new-folder-path.c', 'test.c' ],
  include_directories: [ rootInclude, ],
  dependencies: [ gtk, nemo_private, test_scratch ],
  c_args: nemo_definitions,
)

test('ddwn7 New folder named with a path', new_folder_path)

test('dd29z Make link relative', make_link_job, args: [ 'relative' ])
test('dd2a0 Make link absolute', make_link_job, args: [ 'absolute' ])
test('dd2a1 Make link hardlink', make_link_job, args: [ 'hardlink' ])
test('dd2ds Make link shortcut', make_link_job, args: [ 'shortcut' ])
# A shortcut always has every path since backlog 2026092813381440, so an
# absolute-only one can no longer be asked for.
# test('dd2fr Make link shortcut absolute', make_link_job, args: [ 'shortcut-absolute' ])
test('dd2gv Make link shortcut portable', make_link_job, args: [ 'shortcut-portable' ])
test('dd2qg Make link names', make_link_job, args: [ 'names' ])
test('dec73 Make link shortcut to an upper-case .LNK', make_link_job, args: [ 'shortcut-upper' ])
test('dd42t Make link, every answer', make_link_job, args: [ 'every' ])
test('dd8e1 Make link redo', make_link_job, args: [ 'redo' ])
if is_windows
  test('dd2a2 Make link junction', make_link_job, args: [ 'junction' ])
endif

link_delete_job = executable('test-nemo-link-delete-job',
  [ 'test-nemo-link-delete-job.c', 'test.c' ],
  include_directories: [ rootInclude, ],
  dependencies: [ gtk, nemo_private, test_scratch ],
  c_args: nemo_definitions,
)

test('dcsz0 Link delete leaves the target', link_delete_job, args: [ 'delete' ])
test('dd5ex Link delete, asked by the job', link_delete_job, args: [ 'delete-asked' ])
test('dcsz1 Link move leaves the target', link_delete_job, args: [ 'move' ])

move_conflict = executable('test-nemo-move-conflict',
  [ 'test-nemo-move-conflict.c', 'test.c' ],
  include_directories: [ rootInclude, ],
  dependencies: [ gtk, nemo_private, test_scratch ],
  c_args: nemo_definitions,
)
test('ddzwv Move conflict answers test',
  move_conflict,
  timeout: 120,
)
test('de7as Move Replace asks the guard once test',
  move_conflict,
  args: [ 'guard' ],
)

# Carries the resource for the emblem art, as the next test does.
test('dd5de Link emblems test',
  executable('test-nemo-emblems',
    [ 'test-nemo-emblems.c' ] + gresources,
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

# Carries its own copy of the resource, for the reason the next test explains.
test('dcb5t App resources test',
  executable('test-nemo-app-resources',
    [ 'test-nemo-app-resources.c' ] + gresources,
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_fence ],
  ),
  args: []
)

# The resource has to be compiled straight in for the same reason the exe does:
# nothing else references the self-registering object, so the linker drops it.
test('dbnfq Settings widgets test',
  executable('test-nemo-prefs-widgets',
    [ 'test-nemo-prefs-widgets.c' ] + gresources,
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_fence ],
  ),
  args: []
)

test('de7aj Template rename and add errors test',
  executable('test-nemo-template-errors',
    [ 'test-nemo-template-errors.c', '../src/nemo-template-config-widget.c',
      '../src/nemo-config-base-widget.c', '../src/nemo-error-reporting.c' ] + gresources,
    include_directories: [ rootInclude, include_directories('../src') ],
    dependencies: [ gtk, nemo_private, test_scratch ],
    c_args: nemo_definitions,
  ),
  args: []
)

# Brings its dialog to the front, so alone on Windows too.
test('dd5df Settings dialog test',
  executable('test-nemo-prefs-dialog',
    [ 'test-nemo-prefs-dialog.c', '../src/nemo-file-management-properties.c' ] + gresources,
    include_directories: [ rootInclude, include_directories('../src') ],
    dependencies: [ gtk, nemo_private, test_scratch ],
    c_args: nemo_definitions,
  ),
  args: [],
  is_parallel: not is_windows,
)

test('dd5dg Search box Enter test',
  executable('test-nemo-query-editor',
    [ 'test-nemo-query-editor.c', '../src/nemo-query-editor.c' ] + gresources,
    include_directories: [ rootInclude, include_directories('../src') ],
    dependencies: [ gtk, nemo_private, test_scratch ],
    c_args: nemo_definitions,
  ),
  args: []
)

test('ddwqt Settings tab list test',
  executable('test-nemo-tab-list',
    [ 'test-nemo-tab-list.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_fence ],
  ),
  args: []
)

test('dd5dh Settings Current tab test',
  executable('test-nemo-prefs-current',
    [ 'test-nemo-prefs-current.c', '../src/nemo-prefs-current-folder.c' ] + gresources,
    include_directories: [ rootInclude, include_directories('../src') ],
    dependencies: [ gtk, nemo_private, test_scratch ],
    c_args: nemo_definitions,
  ),
  args: []
)

if is_windows
  test('dbnfr Path separator test',
    executable('test-nemo-path-separator-win32',
      [ 'test-nemo-path-separator-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )
endif

test('dc3p6 Link copy test',
  executable('test-nemo-link-copy',
    [ 'test-nemo-link-copy.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('dd3y3 Link edit test',
  executable('test-nemo-link-edit',
    [ 'test-nemo-link-edit.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('dbw5s Shortcut name test',
  executable('test-nemo-shortcut-name',
    [ 'test-nemo-shortcut-name.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('dcraj Folder settings test',
  executable('test-nemo-folder-settings',
    [ 'test-nemo-folder-settings.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
    c_args: nemo_definitions,
  ),
  args: []
)

if is_windows
  test('dbnf4 Dot-file switch test',
    executable('test-nemo-dot-files-win32',
      [ 'test-nemo-dot-files-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )
endif

# The win32 helper lives in src/, not a library, so it is compiled straight in.
if is_windows
  test('dbedz Win32 argument quoting test',
    executable('test-nemo-view-win32',
      [ 'test-nemo-view-win32.c', '../src/nemo-view-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_fence ],
      c_args: nemo_definitions,
    ),
    args: []
  )
endif

# Both platforms: on Windows icons follow the scale, elsewhere they must not.
test('ddmhv Icon display scale test',
  executable('test-nemo-ui-scale',
    [ 'test-nemo-ui-scale.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_fence ],
    c_args: nemo_definitions,
  ),
  args: []
)

if is_windows
  test('dbjpa DPI win32 test',
    executable('test-nemo-dpi-win32',
      [ 'test-nemo-dpi-win32.c', '../src/nemo-dpi-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_fence ],
      c_args: nemo_definitions,
    ),
    args: []
  )
endif

# Puts three windows on screen and needs the foreground, so it skips over ssh,
# and runs alone, since it takes the foreground from any test beside it.
if is_windows
  test('ddgns Tab move foreground right test',
    executable('test-nemo-raise-win32',
      [ 'test-nemo-raise-win32.c', '../src/nemo-window-at-point-win32.c' ],
      include_directories: [ rootInclude, include_directories('../src') ],
      dependencies: [ gtk, test_fence ],
    ),
    args: [],
    is_parallel: false,
  )
endif

if is_windows
  test('de7dh Console charset win32 test',
    executable('test-nemo-console-charset-win32',
      [ 'test-nemo-console-charset-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ glib, test_fence ],
    ),
    args: []
  )
endif

if is_windows
  test('dbee2 Network win32 test',
    executable('test-nemo-network-win32',
      [ 'test-nemo-network-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_fence ],
    ),
    args: []
  )
endif

if is_windows
  test('dbk0v Properties fallback win32 test',
    executable('test-nemo-properties-win32',
      [ 'test-nemo-properties-win32.c', '../src/nemo-properties-win32.c' ],
      include_directories: [ rootInclude, '../src' ],
      dependencies: [ gtk, nemo_private, win_shell, test_scratch ],
      c_args: nemo_definitions,
    ),
    args: []
  )
endif

if is_windows
  test('dbef4 Drive root name test',
    executable('test-nemo-drive-root-name',
      [ 'test-nemo-drive-root-name.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
      c_args: nemo_definitions,
    ),
    args: []
  )
endif

if is_windows
  test('dbef5 File refresh churn test',
    executable('test-nemo-file-win32-churn',
      [ 'test-nemo-file-win32-churn.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
      c_args: nemo_definitions,
    ),
    args: []
  )
endif

if is_windows
  test('dbef7 Thumbnail win32 type test',
    executable('test-nemo-thumbnail-win32',
      [ 'test-nemo-thumbnail-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
      c_args: nemo_definitions,
    ),
    args: []
  )

  test('dea70 Thumbnail upper-case extension win32 test',
    executable('test-nemo-thumbnail-case-win32',
      [ 'test-nemo-thumbnail-case-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
      c_args: nemo_definitions,
    ),
    args: []
  )
endif

test('dbes6 Appearance themes test',
  # The theme resource is linked into the app, not into libnemo-private, so a
  # test that checks it has to carry its own copy.
  executable('test-nemo-appearance',
    [ 'test-nemo-appearance.c' ] + gresources + themeGresources,
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

# XSETTINGS is how an X desktop names its themes. Linux and the BSDs.
if not is_windows and not is_macos
  test('de56s Desktop theme change test',
    executable('test-nemo-appearance-desktop',
      [ 'test-nemo-appearance-desktop.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, x11, test_scratch ],
    ),
    args: []
  )
endif

test('dbjq9 Column layout test',
  executable('test-nemo-column-layout',
    [ 'test-nemo-column-layout.c', '../src/nemo-column-layout.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_fence ],
    c_args: nemo_definitions,
  ),
  args: []
)

test('ddka3 Find mode columns test',
  executable('test-nemo-search-columns',
    [ 'test-nemo-search-columns.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
    c_args: nemo_definitions,
  ),
  args: []
)

test('dcq6m Pane layout test',
  executable('test-nemo-pane-layout',
    [ 'test-nemo-pane-layout.c', '../src/nemo-pane-layout.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_fence ],
    c_args: nemo_definitions,
  ),
  args: []
)

test('dcq75 Proportional paned test',
  executable('test-nemo-proportional-paned',
    [ 'test-nemo-proportional-paned.c', '../src/nemo-proportional-paned.c', '../src/nemo-pane-layout.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_fence ],
    c_args: nemo_definitions,
  ),
  args: []
)

test('dc56n Search group test',
  executable('test-nemo-search-group',
    [ 'test-nemo-search-group.c', '../src/nemo-list-model.c' ],
    include_directories: [ rootInclude, include_directories('../src') ],
    dependencies: [ gtk, nemo_private, egg, test_scratch ],
    c_args: nemo_definitions,
  ),
  args: []
)

test('dcx2w Icon size test',
  executable('test-nemo-icon-size',
    [ 'test-nemo-icon-size.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_fence ],
    c_args: nemo_definitions,
  ),
  args: []
)

test('dcwzb List expander test',
  executable('test-nemo-list-expander',
    [ 'test-nemo-list-expander.c', '../src/nemo-list-model.c' ],
    include_directories: [ rootInclude, include_directories('../src') ],
    dependencies: [ gtk, nemo_private, egg, test_scratch ],
    c_args: nemo_definitions,
  ),
  args: []
)

test('dcr4f Tree folders test',
  executable('test-nemo-tree-folders',
    [ 'test-nemo-tree-folders.c', '../src/nemo-tree-sidebar-model.c' ],
    include_directories: [ rootInclude, include_directories('../src') ],
    dependencies: [ gtk, nemo_private, test_scratch ],
    c_args: nemo_definitions,
  ),
  args: []
)

test('dd5dj Status bar name test',
  executable('test-nemo-status-name',
    [ 'test-nemo-status-name.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('dd5dk Tab strip test',
  executable('test-nemo-notebook',
    [ 'test-nemo-notebook.c', '../src/nemo-notebook.c' ],
    include_directories: [ rootInclude, include_directories('../src') ],
    dependencies: [ gtk, nemo_private, test_scratch ],
    c_args: nemo_definitions,
  ),
  args: []
)

test('dc7gq New process argv test',
  executable('test-nemo-new-process',
    [ 'test-nemo-new-process.c', '../src/nemo-new-process.c' ],
    include_directories: [ rootInclude, include_directories('../src') ],
    dependencies: [ gtk, nemo_private, test_fence ],
    c_args: nemo_definitions,
  ),
  args: []
)

if not is_windows
  test('dd5ca Clipboard drag collision test',
    executable('test-nemo-clipboard',
      [ 'test-nemo-clipboard.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )
endif

if not is_windows
  test('dd5cb Desktop terminal setting test',
    executable('test-nemo-desktop-terminal',
      [ 'test-nemo-desktop-terminal.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )
endif

# Desktop files and a URI handler from mimeapps.list, neither of which macOS has.
if not is_windows and not is_macos
  test('ddwp3 Launch environment test',
    executable('test-nemo-launch-env',
      [ 'test-nemo-launch-env.c' ],
      include_directories: [ rootInclude, ],
      # The copy it runs is out of reach of the build rpath.
      c_args: nemo_definitions + (extensionShared ? [
        '-DTEST_EXTENSION_LIB_DIR="@0@"'.format(meson.project_build_root() / 'libnemo-extension'),
      ] : []),
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )
endif

if not is_windows
  test('dcb52 Runtime environment test',
    executable('test-nemo-runtime-env',
      [ 'test-nemo-runtime-env.c' ],
      include_directories: [ rootInclude, ],
      # The copy it runs is out of reach of the build rpath.
      c_args: nemo_definitions + (extensionShared ? [
        '-DTEST_EXTENSION_LIB_DIR="@0@"'.format(meson.project_build_root() / 'libnemo-extension'),
      ] : []),
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )
endif

if not is_windows
  test('dcb4x Extensions list flag test',
    executable('test-nemo-extensions-list',
      [ 'test-nemo-extensions-list.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_fence ],
    ),
    args: [ nemo ],
  )
endif

cli_version = executable('test-nemo-cli-version',
  [ 'test-nemo-cli-version.c' ],
  include_directories: [ rootInclude, ],
  dependencies: [ glib, test_fence ],
)

if is_windows
  test('ddhj0 Version and option parse test',
    cli_version,
    args: [ nemo, meson.project_version() ],
  )
endif

if not is_windows
  test('dcrvx Version with no display test',
    cli_version,
    args: [ nemo, meson.project_version(),
            meson.current_source_dir() / '..' / 'build-number.py', py.full_path() ],
  )

  # X11 only. GTK on macOS has no X server to ask.
  if not is_macos
    test('dd26j Window under the pointer test',
      executable('test-nemo-window-at-point',
        [ 'test-nemo-window-at-point.c', '../src/nemo-window-at-point.c' ],
        include_directories: [ rootInclude, include_directories('../src') ],
        dependencies: [ gtk, x11, test_scratch ],
      ),
      args: []
    )
  endif

  test('dd5ey Action programs in their own folder test',
    executable('test-nemo-action-exec',
      [ 'test-nemo-action-exec.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )

  test('ddhtb Share visits test',
    executable('test-nemo-share-visits',
      [ 'test-nemo-share-visits.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )

  test('ddytk Share folder with a local path lists test',
    executable('test-nemo-dir-enum-remote',
      [ 'test-nemo-dir-enum-remote.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )

  # Preloaded into the program, so it links nothing of ours.
  list_view_counts = shared_module('test-nemo-list-view-counts',
    [ 'test-nemo-list-view-counts.c' ],
    dependencies: [ gtk, cc.find_library('dl', required: false) ],
  )
  test('ddav4 List view work per row test',
    executable('test-nemo-list-view-work',
      [ 'test-nemo-list-view-work.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ glib, gio, test_scratch ],
    ),
    args: [ nemo, list_view_counts ],
    timeout: 180
  )

  # Preloaded; it opens the expanders from inside the program.
  list_expand_time_probe = shared_module('test-nemo-list-expand-time-probe',
    [ 'test-nemo-list-expand-time-probe.c' ],
    dependencies: [ gtk ],
  )
  test('ddyv6 List view expander time test',
    executable('test-nemo-list-expand-time',
      [ 'test-nemo-list-expand-time.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ glib, gio, test_scratch ],
    ),
    args: [ nemo, list_expand_time_probe ],
    timeout: 120
  )

  # Preloaded too; it goes into the folders from inside the program.
  open_time_probe = shared_module('test-nemo-open-time-probe',
    [ 'test-nemo-open-time-probe.c' ],
    dependencies: [ gtk ],
  )
  test('de025 Folder open time test',
    executable('test-nemo-open-time',
      [ 'test-nemo-open-time.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ glib, gio, test_scratch ],
    ),
    args: [ nemo, open_time_probe ],
    timeout: 120
  )

  # Preloaded too; it clicks from inside the program.
  places_focus_probe = shared_module('test-nemo-places-focus-probe',
    [ 'test-nemo-places-focus-probe.c' ],
    dependencies: [ gtk ],
  )
  test('ddg79 Places focus test',
    executable('test-nemo-places-focus',
      [ 'test-nemo-places-focus.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ glib, gio, test_scratch ],
    ),
    args: [ nemo, places_focus_probe ],
    timeout: 120
  )

  places_bmhead_probe = shared_module('test-nemo-places-bookmarks-heading-probe',
    [ 'test-nemo-places-bookmarks-heading-probe.c' ],
    dependencies: [ gtk ],
  )
  test('ddhw4 Places bookmarks heading after a settings edit test',
    executable('test-nemo-places-bookmarks-heading',
      [ 'test-nemo-places-bookmarks-heading.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ glib, gio, test_scratch ],
    ),
    args: [ nemo, places_bmhead_probe ],
    timeout: 120
  )

  # Preloaded too; it opens the menus and measures from inside the program.
  menu_key_place_probe = shared_module('test-nemo-menu-key-place-probe',
    [ 'test-nemo-menu-key-place-probe.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk ],
  )
  test('ddg88 Keyboard menu beside the item test',
    executable('test-nemo-menu-key-place',
      [ 'test-nemo-menu-key-place.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ glib, gio, test_scratch ],
    ),
    args: [ nemo, menu_key_place_probe ],
    timeout: 180
  )

  # Preloaded as well; it presses and drags from inside the program.
  drag_select_probe = shared_module('test-nemo-drag-select-probe',
    [ 'test-nemo-drag-select-probe.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk ],
  )
  drag_select = executable('test-nemo-drag-select',
    [ 'test-nemo-drag-select.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ glib, gio, test_scratch ],
  )
  test('ddy5f Drag select test',
    drag_select,
    args: [ nemo, drag_select_probe ],
    timeout: 240
  )
  test('de8ec Drag select test, Nemo Classic',
    drag_select,
    args: [ nemo, drag_select_probe, 'nemo-classic' ],
    timeout: 240
  )
  test('dec8v Drag select test, Nemo Anywhere',
    drag_select,
    args: [ nemo, drag_select_probe, 'nemo-anywhere' ],
    timeout: 240
  )

  # Preloaded too; it clicks and types from inside the program.
  selection_rules_probe = shared_module('test-nemo-selection-rules-probe',
    [ 'test-nemo-selection-rules-probe.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk ],
  )
  selection_rules = executable('test-nemo-selection-rules',
    [ 'test-nemo-selection-rules.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ glib, gio, test_scratch ],
  )
  test('de8bd Selection rules test',
    selection_rules,
    args: [ nemo, selection_rules_probe ],
    timeout: 300
  )
  test('de8dm Selection rules test, Nemo Classic',
    selection_rules,
    args: [ nemo, selection_rules_probe, 'nemo-classic' ],
    timeout: 300
  )
  test('dec8w Selection rules test, Nemo Anywhere',
    selection_rules,
    args: [ nemo, selection_rules_probe, 'nemo-anywhere' ],
    timeout: 300
  )

  icon_atk_probe = shared_module('test-nemo-icon-atk-probe',
    [ 'test-nemo-icon-atk-probe.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk ],
  )
  test('ddgd0 Icon accessible extents test',
    executable('test-nemo-icon-atk',
      [ 'test-nemo-icon-atk.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ glib, gio, test_scratch ],
    ),
    args: [ nemo, icon_atk_probe ],
    timeout: 120
  )

  # Preloaded too; it presses F2 from inside the program.
  rename_photo_probe = shared_module('test-nemo-rename-photo-probe',
    [ 'test-nemo-rename-photo-probe.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk ],
  )
  test('ddwjw F2 in a folder of pictures test',
    executable('test-nemo-rename-photo',
      [ 'test-nemo-rename-photo.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ glib, gio, gtk, test_scratch ],
    ),
    args: [ nemo, rename_photo_probe ],
    timeout: 120
  )

  rename_busy_probe = shared_module('test-nemo-rename-busy-probe',
    [ 'test-nemo-rename-busy-probe.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk ],
  )
  test('ddzyz F2 while the folder keeps changing test',
    executable('test-nemo-rename-photo-busy',
      [ 'test-nemo-rename-photo.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ glib, gio, gtk, test_scratch ],
    ),
    args: [ nemo, rename_busy_probe, '11' ],
    timeout: 120
  )

  rename_moved_probe = shared_module('test-nemo-rename-moved-probe',
    [ 'test-nemo-rename-moved-probe.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk ],
  )
  test('ddzzq Rename stays open when the icons move test',
    executable('test-nemo-rename-photo-moved',
      [ 'test-nemo-rename-photo.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ glib, gio, gtk, test_scratch ],
    ),
    args: [ nemo, rename_moved_probe, '9' ],
    timeout: 120
  )

  # Preloaded too; it searches and presses Escape from inside the program.
  search_escape_probe = shared_module('test-nemo-search-escape-probe',
    [ 'test-nemo-search-escape-probe.c' ],
    dependencies: [ gtk ],
  )
  test('ddy3x Escape in find mode test',
    executable('test-nemo-search-escape',
      [ 'test-nemo-search-escape.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ glib, gio, test_scratch ],
    ),
    args: [ nemo, search_escape_probe ],
    timeout: 240
  )

  # Preloaded too; it selects the files and presses F2 from inside.
  bulk_rename_run_probe = shared_module('test-nemo-bulk-rename-run-probe',
    [ 'test-nemo-bulk-rename-run-probe.c' ],
    dependencies: [ gtk ],
  )
  test('ddy4e Bulk renamer gets paths test',
    executable('test-nemo-bulk-rename-run',
      [ 'test-nemo-bulk-rename-run.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ glib, gio, test_scratch ],
    ),
    args: [ nemo, bulk_rename_run_probe ],
    timeout: 120
  )

  # Preloaded too; it makes the folder and types its name from inside.
  new_folder_path_probe = shared_module('test-nemo-new-folder-path-probe',
    [ 'test-nemo-new-folder-path-probe.c' ],
    dependencies: [ gtk ],
  )
  new_folder_path_gui = executable('test-nemo-new-folder-path-gui',
    [ 'test-nemo-new-folder-path-gui.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ glib, gio, test_scratch ],
  )
  test('ddwn8 New folder named with a path, icon view',
    new_folder_path_gui, args: [ nemo, new_folder_path_probe, 'icon' ], timeout: 120)
  test('ddwn9 New folder named with a path, list view',
    new_folder_path_gui, args: [ nemo, new_folder_path_probe, 'list' ], timeout: 120)

  # Preloaded too; it opens Properties and presses the keys from inside.
  properties_tabs_probe = shared_module('test-nemo-properties-tabs-probe',
    [ 'test-nemo-properties-tabs-probe.c' ],
    dependencies: [ gtk ],
  )
  test('ddxes Properties tabs take no focus test',
    executable('test-nemo-properties-tabs',
      [ 'test-nemo-properties-tabs.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ glib, gio, test_scratch ],
    ),
    args: [ nemo, properties_tabs_probe ],
    timeout: 120
  )

  # Preloaded too; it closes Properties and presses Shift+Delete from inside.
  delete_after_dialog_probe = shared_module('test-nemo-delete-after-dialog-probe',
    [ 'test-nemo-delete-after-dialog-probe.c' ],
    dependencies: [ gtk ],
  )
  test('de55r Delete key after Properties test',
    executable('test-nemo-delete-after-dialog',
      [ 'test-nemo-delete-after-dialog.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ glib, gio, test_scratch ],
    ),
    args: [ nemo, delete_after_dialog_probe ],
    timeout: 120
  )

  # Preloaded too; it hovers and checks from inside the program.
  tooltip_focus_probe = shared_module('test-nemo-tooltip-focus-probe',
    [ 'test-nemo-tooltip-focus-probe.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk ],
  )
  test('ddwk9 No tooltip while another program has the focus test',
    executable('test-nemo-tooltip-focus',
      [ 'test-nemo-tooltip-focus.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ glib, gio, gtk, test_scratch ],
    ),
    args: [ nemo, tooltip_focus_probe ],
    timeout: 150
  )

  # Preloaded too; it sorts and resets from inside the program.
  reset_sort_probe = shared_module('test-nemo-reset-sort-probe',
    [ 'test-nemo-reset-sort-probe.c' ],
    dependencies: [ gtk ],
  )
  test('ddmb5 Reset view sort test',
    executable('test-nemo-reset-sort',
      [ 'test-nemo-reset-sort.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ glib, gio, test_scratch ],
    ),
    args: [ nemo, reset_sort_probe ],
    timeout: 180
  )

  # Preloaded too; it sorts and moves between views from inside the program.
  sort_views_probe = shared_module('test-nemo-sort-views-probe',
    [ 'test-nemo-sort-views-probe.c' ],
    dependencies: [ gtk ],
  )
  test('de55s List and icon views share a sort test',
    executable('test-nemo-sort-views',
      [ 'test-nemo-sort-views.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ glib, gio, test_scratch ],
    ),
    args: [ nemo, sort_views_probe ],
    timeout: 180
  )

  # Preloaded too; it searches, picks a view and leaves from inside the program.
  find_view_probe = shared_module('test-nemo-find-view-probe',
    [ 'test-nemo-find-view-probe.c' ],
    dependencies: [ gtk ],
  )
  test('ddy4d Find mode view test',
    executable('test-nemo-find-view',
      [ 'test-nemo-find-view.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ glib, gio, test_scratch ],
    ),
    args: [ nemo, find_view_probe ],
    timeout: 180
  )

  # Preloaded too; it goes Back and Forward over a search from inside the program.
  find_history_probe = shared_module('test-nemo-find-history-probe',
    [ 'test-nemo-find-history-probe.c' ],
    dependencies: [ gtk ],
  )
  test('ddy51 Search in history test',
    executable('test-nemo-find-history',
      [ 'test-nemo-find-history.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ glib, gio, test_scratch ],
    ),
    args: [ nemo, find_history_probe ],
    timeout: 180
  )

  # Preloaded too; it holds an smb:// mount and looks at the window meanwhile.
  connecting_probe = shared_module('test-nemo-connecting-probe',
    [ 'test-nemo-connecting-probe.c' ],
    dependencies: [ gtk, cc.find_library('dl', required: false) ],
  )
  test('ddq85 Connecting sign while a share mounts test',
    executable('test-nemo-connecting',
      [ 'test-nemo-connecting.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: [ nemo, connecting_probe ],
    timeout: 180
  )

  if not is_macos
    test('dd5ez Clean start test',
      executable('test-nemo-startup-clean',
        [ 'test-nemo-startup-clean.c' ],
        include_directories: [ rootInclude, ],
        dependencies: [ glib, gio, x11, test_scratch ],
      ),
      args: [ nemo ],
      timeout: 120
    )
  endif

  test('dc7gr Instances test',
    executable('test-nemo-instances',
      [ 'test-nemo-instances.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: [ nemo ],
    timeout: 120
  )

  if not is_macos
    test('ddc84 File cache written at quit test',
      executable('test-nemo-cache-quit',
        [ 'test-nemo-cache-quit.c' ],
        include_directories: [ rootInclude, ],
        dependencies: [ gtk, nemo_private, x11, test_scratch ],
      ),
      args: [ nemo ],
      timeout: 300
    )
  endif

  # Closes the window through the X server.
  if not is_macos
    # Preloaded into the program, so it links nothing of ours.
    quit_hooks = shared_module('test-nemo-quit-hooks',
      [ 'test-nemo-quit-hooks.c' ],
      dependencies: [ gtk ],
    )
    quit_saves = executable('test-nemo-quit-saves',
      [ 'test-nemo-quit-saves.c' ],
      c_args: nemo_definitions,
      include_directories: [ rootInclude, ],
      dependencies: [ glib, gio, x11, test_scratch ],
    )
    test('ddf63 Shortcut saved and notice withdrawn at quit test',
      quit_saves,
      args: [ nemo, quit_hooks ],
      timeout: 120
    )
    test('ddfam Shortcut file beside settings and carried over once test',
      quit_saves,
      args: [ nemo, quit_hooks, 'carry-over' ],
      timeout: 240
    )
  endif

  # Preloaded into every copy the test starts; it links nothing of ours.
  tab_move_probe = shared_module('test-nemo-tab-move-probe',
    [ 'test-nemo-tab-move-probe.c' ],
    dependencies: [ gtk ],
  )
  test('dd26k Tab move between processes test',
    executable('test-nemo-tab-move',
      [ 'test-nemo-tab-move.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, test_scratch ],
    ),
    args: [ nemo, tab_move_probe ],
    timeout: 180
  )
  test('ddgh6 Menus freed after closing test',
    executable('test-nemo-menus-freed',
      [ 'test-nemo-menus-freed.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ glib, test_scratch ],
    ),
    args: [ nemo, tab_move_probe ],
    timeout: 120
  )

  # A stray XQuartz copy on a Mac has no server behind it here.
  xtst = is_macos ? dependency('', required: false) : dependency('xtst', required: false)
  if xtst.found()
    test('dd8dp Tree menu key test',
      executable('test-nemo-tree-menu-key',
        [ 'test-nemo-tree-menu-key.c' ],
        include_directories: [ rootInclude, ],
        dependencies: [ glib, x11, xtst, test_scratch ],
      ),
      args: [ nemo ],
      timeout: 120
    )

    # Preloaded into the program, so it links nothing of ours.
    held_view_hooks = shared_module('test-nemo-held-view-hooks',
      [ 'test-nemo-held-view-hooks.c' ],
      dependencies: [ gtk, cc.find_library('dl', required: false) ],
    )
    test('dde3c Held view settings handlers test',
      executable('test-nemo-held-view',
        [ 'test-nemo-held-view.c' ],
        include_directories: [ rootInclude, shclInclude, ],
        dependencies: [ glib, x11, xtst, math, nemo_private, test_scratch ],
      ),
      args: [ nemo, held_view_hooks ],
      timeout: 180
    )

    # Preloaded too, and holds the button down through the X server.
    long_press_probe = shared_module('test-nemo-long-press-probe',
      [ 'test-nemo-long-press-probe.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, x11, xtst ],
    )
    test('ddy6j Long press menu test',
      executable('test-nemo-long-press',
        [ 'test-nemo-long-press.c' ],
        include_directories: [ rootInclude, ],
        dependencies: [ glib, gio, test_scratch ],
      ),
      args: [ nemo, long_press_probe ],
      timeout: 240
    )
  endif
endif

test('dbkp1 Filename extension test',
  executable('test-nemo-filename-extension',
    [ 'test-nemo-filename-extension.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_fence ],
  ),
  args: []
)

test('dct7k Owner columns test',
  executable('test-nemo-owner-columns',
    [ 'test-nemo-owner-columns.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch, win_shell ],
  ),
  args: []
)

test('dd5cc Uri under another test',
  executable('test-nemo-uri-under',
    [ 'test-nemo-uri-under.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_fence ],
  ),
  args: []
)

test('dct70 Path forms test',
  executable('test-nemo-path-forms',
    [ 'test-nemo-path-forms.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_fence ],
  ),
  args: []
)

test('dcrph Window title test',
  executable('test-nemo-window-title',
    [ 'test-nemo-window-title.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('dcy7s Uptime test',
  executable('test-nemo-uptime',
    [ 'test-nemo-uptime.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_fence ],
  ),
  args: []
)

test('ddkn3 Windows paths in user text test',
  executable('test-nemo-user-text',
    [ 'test-nemo-user-text.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('ddmrp Link file type test',
  executable('test-nemo-link-type',
    [ 'test-nemo-link-type.c', '../src/nemo-desktop-item-properties.c' ],
    include_directories: [ rootInclude, include_directories('../src') ],
    dependencies: [ gtk, nemo_private, test_scratch ],
    c_args: nemo_definitions,
  ),
  args: []
)

test('dbjkh Command template test',
  executable('test-nemo-command-template',
    [ 'test-nemo-command-template.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('dbhae Archive options test',
  executable('test-nemo-archive',
    [ 'test-nemo-archive.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('dd5dm Compress dialog height test',
  executable('test-nemo-archive-dialog',
    [ 'test-nemo-archive-dialog.c', '../src/nemo-archive-dialog.c' ],
    include_directories: [ rootInclude, include_directories('../src') ],
    dependencies: [ archive, gtk, nemo_private, x11, test_scratch ],
    c_args: nemo_definitions,
  ),
  args: []
)

test('dcwjb Archive settings test',
  executable('test-nemo-archive-settings',
    [ 'test-nemo-archive-settings.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

# Reads the archives it wrote back with libarchive, so it needs that library.
test('dbj83 Archive job test',
  executable('test-nemo-archive-job',
    [ 'test-archive-job.c', 'test.c', '../src/nemo-progress-info-widget.c' ],
    include_directories: [ rootInclude, include_directories('../src') ],
    dependencies: [ archive, gtk, nemo_private, test_scratch ],
    c_args: nemo_definitions,
  ),
  args: []
)

test('ddav1 Archive check cost test',
  executable('test-nemo-archive-verify-cost',
    [ 'test-nemo-archive-verify-cost.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ archive, gio, nemo_private, test_scratch ],
  ),
  args: [],
  timeout: 120
)

# Makes its big file sparse, which is a POSIX call.
if not is_windows
  test('ddeqt Archive stop time test',
    executable('test-nemo-archive-stop-time',
      [ 'test-nemo-archive-stop-time.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
      c_args: nemo_definitions,
    ),
    args: [],
    timeout: 240
  )
endif

# Some hundreds of archives, so it gets longer than the default.
test('dd42v Archive option combinations',
  executable('test-nemo-archive-combos',
    [ 'test-nemo-archive-combos.c', 'test.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ archive, gtk, nemo_private, test_scratch ],
    c_args: nemo_definitions,
  ),
  args: [],
  timeout: 600,
)

test('dbj6w Extract naming test',
  executable('test-nemo-extract',
    [ 'test-nemo-extract.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_fence ],
  ),
  args: []
)

test('dct66 Archive mount address test',
  executable('test-nemo-archive-mount',
    [ 'test-nemo-archive-mount.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_fence ],
  ),
  args: []
)

# Writes its own zip with libarchive, so it needs the library the app links.
test('dbj6x Extract job test',
  executable('test-nemo-extract-job',
    [ 'test-extract-job.c', 'test.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ archive, gtk, nemo_private, test_scratch ],
    c_args: nemo_definitions,
  ),
  args: []
)

# A rar with a link libarchive 3.8.9 and older can't read. Writes it with rar;
# the links it holds are POSIX ones. Reads one back with libarchive too.
if not is_windows
  test('ddx67 Extract rar long link test',
    executable('test-extract-rar-links',
      [ 'test-extract-rar-links.c', 'test.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ archive, gtk, nemo_private, test_scratch ],
      c_args: nemo_definitions,
    ),
    args: []
  )
endif

# The archive core with nothing of the app linked in, and no display to lean on.
test('ddn29 Archive core test',
  executable('test-arc-core',
    [ 'test-arc-core.c' ],
    dependencies: [ arc_core, test_fence ],
  ),
  env: [ 'DISPLAY=', 'WAYLAND_DISPLAY=' ],
  args: []
)

# What the core's build did: no GTK or app on its include path, and no call
# into them in the library.
arc_core_nm = find_program('nm', required: false)
test('ddx6z Archive core build test',
  py,
  args: [ files('check-arc-core-build.py'), meson.project_source_root(), meson.project_build_root(),
          arc_core_lib, arc_core_nm.found() ? arc_core_nm.full_path() : '' ],
)

# The scan behind the archive size totals on a scratch tree, with a host the
# test makes and with the app's own. The app is linked for the scratch folder
# and its host only; the scan never sees it.
test('ddn4x Archive scan test',
  executable('test-arc-scan',
    [ 'test-arc-scan.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ arc_core, gtk, nemo_private, test_scratch ],
  ),
  env: [ 'DISPLAY=', 'WAYLAND_DISPLAY=' ],
  args: []
)

test('dc7jy Data dirs test',
  executable('test-nemo-data-dirs',
    [ 'test-nemo-data-dirs.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_fence ],
  ),
  args: []
)

test('dbgr9 Path list text test',
  executable('test-nemo-path-list',
    [ 'test-nemo-path-list.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_fence ],
  ),
  args: []
)

test('dcx3q Image folder test',
  executable('test-nemo-image-folder',
    [ 'test-nemo-image-folder.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('dcyh0 Photoshop reader test',
  executable('test-nemo-psd',
    [ 'test-nemo-psd.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

# The stand-in thumbnailer and search helper are this program run again.
test('ddq92 Office files read in the app test',
  executable('test-nemo-office',
    [ 'test-nemo-office.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch, archive ],
    c_args: nemo_definitions,
  ),
  args: [],
  timeout: 120
)

# The .doc, .xls and .ppt are written here, with no library.
test('ddqhc Old office files read in the app test',
  executable('test-nemo-office-ole',
    [ 'test-nemo-office-ole.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch, test_ole2 ],
    c_args: nemo_definitions,
  ),
  args: [],
  timeout: 120
)

testRaw = executable('test-nemo-raw',
  [ 'test-nemo-raw.c' ],
  include_directories: [ rootInclude, ],
  dependencies: [ gtk, nemo_private, test_scratch ],
)

test('dczqq Camera raw reader test', testRaw, args: [])

# Again with GLib clearing what a shrunk array gives up, so the fixtures can't
# lean on bytes they no longer own.
test('dddq7 Camera raw reader test, cleared memory', testRaw,
  env: [ 'G_DEBUG=gc-friendly' ],
  args: []
)

# Windows reads shortcuts through the shell and never builds this reader.
if not is_windows
  test('dd29h Windows shortcut reader test',
    executable('test-nemo-lnk',
      [ 'test-nemo-lnk.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )

  test('dd2qs Folder shortcuts sort with folders test',
    executable('test-nemo-lnk-sort',
      [ 'test-nemo-lnk-sort.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )
endif

test('dczra ImageMagick thumbnail test',
  executable('test-nemo-magick',
    [ 'test-nemo-magick.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

# Runs on a copy of the loader list the system's gdk-pixbuf reads.
if not is_windows
  test('ddmb4 ImageMagick for missing pixbuf loaders test',
    executable('test-nemo-magick-extras',
      [ 'test-nemo-magick-extras.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
      c_args: [ '-DPIXBUF_LOADERS_CACHE="@0@"'.format(
        dependency('gdk-pixbuf-2.0').get_variable(pkgconfig: 'gdk_pixbuf_cache_file', default_value: '')) ],
    ),
    args: []
  )
endif

test('dcyh1 Thumbnail hold test',
  executable('test-nemo-thumbnail-hold',
    [ 'test-nemo-thumbnail-hold.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('dcz8j Thumbnail memory test',
  executable('test-nemo-thumbnail-memory',
    [ 'test-nemo-thumbnail-memory.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('dcz99 Thumbnail jobs test',
  executable('test-nemo-thumbnail-jobs',
    [ 'test-nemo-thumbnail-jobs.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('dcz6w Thumbnail order test',
  executable('test-nemo-thumbnail-order',
    [ 'test-nemo-thumbnail-order.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('ddah8 Thumbnail of an edited file test',
  executable('test-nemo-thumbnail-edited',
    [ 'test-nemo-thumbnail-edited.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('de7ad Thumbnail of a picture dated ahead test',
  executable('test-nemo-thumbnail-future',
    [ 'test-nemo-thumbnail-future.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

# It holds a listing by standing in for a GIO call, which only works where
# the loader looks in the program first.
if not is_windows
  test('de7cs Thumbnail lookup behind a slow count test',
    executable('test-nemo-thumbnail-count-wait',
      [ 'test-nemo-thumbnail-count-wait.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch, cc.find_library('dl', required: false) ],
    ),
    args: []
  )
endif

# Its stand-in thumbnailer is matched on image/png, and Windows types a file
# by its extension instead.
if not is_windows
  test('dd8d9 Thumbnail zoom during render test',
    executable('test-nemo-thumbnail-zoom',
      [ 'test-nemo-thumbnail-zoom.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )
endif

test('ddgsk Thumbnail stopped once nothing wants it test',
  executable('test-nemo-thumbnail-stop',
    [ 'test-nemo-thumbnail-stop.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('dcy7j Menu paths test',
  executable('test-nemo-menu-paths',
    [ 'test-nemo-menu-paths.c' ] + gresources,
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_fence ],
  ),
  args: []
)

test('dczq0 Deep counts test',
  executable('test-nemo-deep-counts',
    [ 'test-nemo-deep-counts.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('dbsx4 Directory load clean test',
  executable('test-nemo-directory-load-clean',
    [ 'test-nemo-directory-load-clean.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

if is_windows
  share_win32 = executable('test-nemo-share-win32',
    [ 'test-nemo-share-win32.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  )
  test('dd5ht Share check does no I/O', share_win32, args: [ 'share' ])
  test('dd5hv Junction to a missing folder lists', share_win32, args: [ 'dangling' ])

  drives_win32 = executable('test-nemo-drives-win32',
    [ 'test-nemo-drives-win32.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_fence ],
  )
  # Maps a free drive letter for the run, so it waits for nothing else that
  # lists the drives.
  test('ddj38 Mapped drive not asked', drives_win32,
    args: [],
    is_parallel: false,
  )
  test('de88t Drive names asked for only when shown', drives_win32, args: [ 'names' ])

  test('dd5hw Permissions source test',
    executable('test-nemo-perm-source-win32',
      [ 'test-nemo-perm-source-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, win_shell, test_scratch ],
    ),
    args: []
  )
endif

if is_windows
  test('dcbcb Metadata ready test',
    executable('test-nemo-metadata-ready-win32',
      [ 'test-nemo-metadata-ready-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: [],
    timeout: 300
  )
endif

test('dbsx7 Config schema drift test',
  executable('test-nemo-config-schema',
    [ 'test-nemo-config-schema.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: [ meson.current_source_dir() / '..' / 'data' / 'nemo-anywhere.schema.shcl' ]
)

if is_windows
  # The exe needs the app's own manifest or every long-path call fails on the
  # file not being there, which is not what is under test.
  testWinMod = import('windows')
  longPathManifest = testWinMod.compile_resources('test-manifest.rc',
    include_directories: include_directories('../src'),
    depend_files:        files('../src/nemo-anywhere.manifest'),
  )

  test('dbwb1 Long path enumeration test',
    executable('test-nemo-dir-enum-win32',
      [ 'test-nemo-dir-enum-win32.c' ] + longPathManifest,
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )

  test('dd5hx Application manifest test',
    executable('test-nemo-manifest-win32',
      [ 'test-nemo-manifest-win32.c' ] + longPathManifest,
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )
endif

# The bookmark list lives in src/, not a library, so it is compiled straight in.
if is_windows
  test('dbwe5 First run test',
    executable('test-nemo-first-run-win32',
      [ 'test-nemo-first-run-win32.c', '../src/nemo-bookmark-list.c' ],
      include_directories: [ rootInclude, include_directories('../src') ],
      dependencies: [ gtk, nemo_private, test_scratch ],
      c_args: nemo_definitions,
    ),
    args: []
  )
endif

# Windows loads no extensions. The module is left unlinked on purpose; see its
# header.
if not is_windows
  test_extension_module = shared_module('test-nemo-extension-module',
    [ 'test-nemo-extension-module.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ glib, gtk ],
  )
  test('dcsvh Extension load test',
    executable('test-nemo-extension-load',
      [ 'test-nemo-extension-load.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, gmodule, nemo_extension, test_fence ],
    ),
    args: [ test_extension_module ]
  )
endif

# The cases the old converter programs had, now read in the app, on files
# written for the purpose.
test('dbxj2 Office text and search helpers test',
  executable('test-nemo-search-helpers',
    [ 'test-nemo-search-helpers.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, archive, test_scratch, test_ole2 ],
    c_args: nemo_definitions,
  ),
  args: [],
)

test('ddkdv Bookmark name and icon test',
  executable('test-nemo-bookmark-info',
    [ 'test-nemo-bookmark-info.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('ddzxz Connect to Server address scheme test',
  executable('test-nemo-skip-scheme',
    [ 'test-nemo-skip-scheme.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_fence ],
  ),
  args: []
)

if is_windows
  test('dbxj3 Windows Search test',
    executable('test-nemo-search-win32',
      [ 'test-nemo-search-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )

  test('dbxjs File associations test',
    executable('test-nemo-associations-win32',
      [ 'test-nemo-associations-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )

  test('dbzqh Program launch test',
    executable('test-nemo-launch-win32',
      [ 'test-nemo-launch-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )

  test('dbxkh Shell icon test',
    executable('test-nemo-shell-icon-win32',
      [ 'test-nemo-shell-icon-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, win_shell, test_scratch ],
    ),
    args: []
  )

  test('dbzb3 Link type test',
    executable('test-nemo-link-info-win32',
      [ 'test-nemo-link-info-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )

  test('ddg9f Link end test',
    executable('test-nemo-link-end-win32',
      [ 'test-nemo-link-end-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )

  test('dbzb4 Clipboard win32 test',
    executable('test-nemo-clipboard-win32',
      [ 'test-nemo-clipboard-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )

  test('dc2dz Drag source win32 test',
    executable('test-nemo-dnd-win32',
      [ 'test-nemo-dnd-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )

  test('ddk7p ImageMagick start win32 test',
    executable('test-nemo-magick-win32',
      [ 'test-nemo-magick-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: [ executable('test-fake-magick', [ 'test-fake-magick.c' ]) ]
  )

  fake_tool = executable('test-fake-tool', [ 'test-fake-tool.c' ])
  # Explorer starts it with its own PATH, so it links nothing of the build's.
  env_dump = executable('test-env-dump', [ 'test-env-dump.c' ], link_args: [ '-lshell32' ])

  test('ddkaz Tool start win32 test',
    executable('test-nemo-tool-start-win32',
      [ 'test-nemo-tool-start-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
      c_args: nemo_definitions,
    ),
    args: [ fake_tool ],
    timeout: 120
  )

  test('ddqdq Copies reach each other with no bus win32 test',
    executable('test-nemo-copies-win32',
      [ 'test-nemo-copies-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: [ nemo ],
    timeout: 300
  )

  test('ddmhe Helper end win32 test',
    executable('test-nemo-helper-end-win32',
      [ 'test-nemo-helper-end-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: [ fake_tool ],
    timeout: 120
  )

  test('ddqm2 Launch environment win32 test',
    executable('test-nemo-launch-env-win32',
      [ 'test-nemo-launch-env-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: [ env_dump ],
    timeout: 120
  )

  test('ddqn8 New window start win32 test',
    executable('test-nemo-new-copy-win32',
      [ 'test-nemo-new-copy-win32.c', '../src/nemo-new-process.c' ],
      include_directories: [ rootInclude, include_directories('../src') ],
      dependencies: [ gtk, nemo_private, test_scratch ],
      c_args: nemo_definitions,
    ),
    args: [ env_dump ],
    timeout: 120
  )

  test('ddkem Action start win32 test',
    executable('test-nemo-action-start-win32',
      [ 'test-nemo-action-start-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: [ fake_tool,
            executable('test-fake-tool-windowed', [ 'test-fake-tool.c' ], win_subsystem: 'windows') ],
    timeout: 60
  )

  test('ddkf6 Thumbnailer type win32 test',
    executable('test-nemo-thumbnailer-type-win32',
      [ 'test-nemo-thumbnailer-type-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: [],
    timeout: 60
  )

  test('ddmbc Command launch win32 test',
    executable('test-nemo-command-launch-win32',
      [ 'test-nemo-command-launch-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: [ fake_tool ],
    timeout: 60
  )

  test('ddmdx Launchable win32 test',
    executable('test-nemo-launchable-win32',
      [ 'test-nemo-launchable-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )

  test('ddyn6 Drop from another program win32 test',
    executable('test-nemo-drop-win32',
      [ 'test-nemo-drop-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )

  test('ddk6v Drop keys win32 test',
    executable('test-nemo-drop-keys-win32',
      [ 'test-nemo-drop-keys-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_fence ],
    ),
    args: []
  )
endif
