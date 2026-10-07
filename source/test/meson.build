
# Scratch directories made through this are removed when the test exits.
test_scratch = declare_dependency(
  link_with: static_library('test-scratch',
    [ 'test-scratch.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private ],
  ),
)

# Heap growth over repeated operations, for the leak tests.
test_heap = declare_dependency(
  link_with: static_library('test-heap',
    [ 'test-heap.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ glib ],
  ),
)

test('5ethkhjr Search Engine test',
  executable('test-nemo-search-engine',
    [ 'test-nemo-search-engine.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: [],
  timeout: 180        # three searches, each with its own 30s ceiling
)

test('rgy4vnf0 Search folder test',
  executable('test-nemo-search-folder',
    [ 'test-nemo-search-folder.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: [],
  timeout: 120
)

test('rgep22n8 Directory monitor test',
  executable('test-nemo-directory-monitor',
    [ 'test-nemo-directory-monitor.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: [],
  timeout: 120
)

test('rhmkzde8 Folder time after a change inside it test',
  executable('test-nemo-folder-mtime',
    [ 'test-nemo-folder-mtime.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: [],
  timeout: 60
)

test('rhmmhmeg Small file copy test',
  executable('test-nemo-small-copy',
    [ 'test-nemo-small-copy.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('rhtmbdm8 Drop action from another program test',
  executable('test-nemo-drop-action',
    [ 'test-nemo-drop-action.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('rfzh7ygg Drag drop confirm test',
  executable('test-nemo-drag-confirm',
    [ 'test-nemo-drag-confirm.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('rg6a49ar Config defaults list test',
  executable('test-nemo-config-catalog',
    [ 'test-nemo-config-catalog.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('rdjjz89r Config store test',
  executable('test-nemo-config',
    [ 'test-nemo-config.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: [],
  timeout: 90         # a hundred external rewrites, each waiting on the file monitor
)

# Converting runs when the store opens, so the old file goes in before that.
test('rjc4dd8z Config format upgrade test',
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

test('rjcev513 Config old formats test',
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
  test('rj4jbn1b Allocations per read test',
    executable('test-nemo-read-allocations',
      [ 'test-nemo-read-allocations.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )
endif

test('rh325v70 Delete test guard arming test',
  executable('test-nemo-delete-testguard',
    [ 'test-nemo-delete-testguard.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('rhnpn188 Guard pauses the job test',
  executable('test-nemo-guard-pause',
    [ 'test-nemo-guard-pause.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('rhtq57n0 Job queue test',
  executable('test-nemo-job-queue',
    [ 'test-nemo-job-queue.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

# The heap is read through glibc, so elsewhere these skip.
if not is_windows
  leaks = executable('test-nemo-leaks',
    [ 'test-nemo-leaks.c', '../src/nemo-query-editor.c' ] + gresources,
    include_directories: [ rootInclude, include_directories('../src') ],
    dependencies: [ gtk, nemo_private, test_scratch, test_heap ],
    c_args: nemo_definitions,
  )

  test('rjbkzvdv Job progress leak test', leaks, args: [ 'progress' ])
  test('rjbkzwe7 Move job leak test', leaks, args: [ 'move' ])
  test('rjbmdd0s Clipboard drag check leak test', leaks, args: [ 'clipboard' ])
  test('rjbmh7g1 Stopped zip leak test', leaks, args: [ 'zip-cancel' ])
  test('rjbn18rq Search query leak test', leaks, args: [ 'query-editor' ])
  test('rjbn19rn Bookmarks load leak test', leaks, args: [ 'bookmarks' ])
  test('rjbn328w Bookmarks file load leak test', leaks, args: [ 'bookmarks-file' ])
  test('rjbpmcxy Extract job leak test', leaks, args: [ 'extract' ])
  test('rjbqagjf Stopped tar.gz leak test', leaks, args: [ 'tar.gz-cancel' ])
  test('rjbw0rkq Stopped 7z leak test', leaks, args: [ '7z-cancel' ])
  test('rjcth67d Leak test self-check', leaks, args: [ 'self-check' ])
endif

test('rhtq57n1 Cancelled drop leaves the files test',
  executable('test-nemo-drop-cancel',
    [ 'test-nemo-drop-cancel.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('rh3n43kg Empty trash asks test',
  executable('test-nemo-empty-trash',
    [ 'test-nemo-empty-trash.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('rh8w1q80 Delete from trash asks test',
  executable('test-nemo-delete-from-trash',
    [ 'test-nemo-delete-from-trash.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('redmwz7r Favorites test',
  executable('test-nemo-favorites',
    [ 'test-nemo-favorites.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('rhd1v448 File checksum test',
  executable('test-nemo-file-digest',
    [ 'test-nemo-file-digest.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('rhd29h28 File checksum attribute test',
  executable('test-nemo-file-xattr',
    [ 'test-nemo-file-xattr.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('rhd1cv38 File cache store test',
  executable('test-nemo-cache-db',
    [ 'test-nemo-cache-db.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch, sqlite ],
  ),
  args: []
)

test('rjch1a9a File cache opened by many at once test',
  executable('test-nemo-cache-open-race',
    [ 'test-nemo-cache-open-race.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch, sqlite ],
  ),
  args: [],
  timeout: 120
)

test('rhd69rjr File cache prune test',
  executable('test-nemo-cache-prune',
    [ 'test-nemo-cache-prune.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch, sqlite ],
  ),
  args: [],
  timeout: 120
)

test('rhd4gy78 Thumbnail store test',
  executable('test-nemo-thumbnail-store',
    [ 'test-nemo-thumbnail-store.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

# Off since the sweep of the shared freedesktop thumbnail cache was retired:
# nothing writes there any more, and the file cache prunes itself (see
# test-nemo-cache-prune.c). test-nemo-thumbnail-prune.c needs
# nemo-thumbnail-prune.h, which went with it.
# test('Thumbnail prune test',
#   executable('test-nemo-thumbnail-prune',
#     [ 'test-nemo-thumbnail-prune.c' ],
#     include_directories: [ rootInclude, ],
#     dependencies: [ gtk, nemo_private, test_scratch ],
#   ),
#   args: []
# )

test('redrqe60 Thumbnail factory test',
  executable('test-nemo-thumbnail',
    [ 'test-nemo-thumbnail.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: [],
  timeout: 180        # the hung-helper case waits out the thumbnailer timeout
)

test('rcgxemdg Metadata store test',
  executable('test-nemo-metadata-store',
    [ 'test-nemo-metadata-store.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

# Needs the slug define: the point of the test is the path the app really builds.
test('reqzgh4g Config root test',
  executable('test-nemo-config-root',
    [ 'test-nemo-config-root.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
    c_args: nemo_definitions,
  ),
  args: []
)

test('rj46m24q EEL string check test',
  executable('test-eel-string',
    [ 'test-eel-string.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private ],
  ),
  args: []
)

test('rj46pkw3 Conflict dialog test',
  executable('test-nemo-conflict-dialog',
    [ 'test-nemo-conflict-dialog.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('rcgzq4qg User input parse test',
  executable('test-eel-user-input',
    [ 'test-eel-user-input.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

# symlink(2) and the guard-page mmap below are POSIX; none of these cross-build.
if not is_windows
  test('rek3gd30 Symlink recurse premise test',
    executable('test-nemo-symlink-recurse',
      [ 'test-nemo-symlink-recurse.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )

  test('rgv89eeg Delete guard test',
    executable('test-nemo-delete-guard',
      [ 'test-nemo-delete-guard.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )

  test('rgy4vnf1 Scratch cleanup test',
    executable('test-nemo-scratch',
      [ 'test-nemo-scratch.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )
endif

test('rek3gd31 Search invalid regex test',
  executable('test-nemo-search-regex',
    [ 'test-nemo-search-regex.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

if is_windows
  test('rgy77abr Delete guard win32 test',
    executable('test-nemo-delete-guard-win32',
      [ 'test-nemo-delete-guard-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )
endif

test('rffw4xdg Treeview hit test',
  executable('test-eel-treeview-hit',
    [ 'test-eel-treeview-hit.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private ],
  ),
  args: []
)

test('rhtmbdm9 Clipped cell text test',
  executable('test-eel-clipped-cell',
    [ 'test-eel-clipped-cell.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private ],
  ),
  args: []
)

test('rhtmbdma Keyboard context menu test',
  executable('test-eel-context-menu',
    [ 'test-eel-context-menu.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('rjes67yy Rename field accessible test',
  executable('test-eel-editable-label-atk',
    [ 'test-eel-editable-label-atk.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('rjew8g59 Rename field sized before shown test',
  executable('test-eel-editable-label-size',
    [ 'test-eel-editable-label-size.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private ],
  ),
  args: []
)

test('rhtmbdmb Escape forgets the cursor test',
  executable('test-eel-forget-cursor',
    [ 'test-eel-forget-cursor.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private ],
  ),
  args: []
)

test('rgxy149r Focus guard test',
  executable('test-eel-focus-guard',
    [ 'test-eel-focus-guard.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private ],
  ),
  args: []
)

test('rhnv1n88 Row hover tint test',
  executable('test-nemo-row-hover',
    [ 'test-nemo-row-hover.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('rhgba078 Primary mask test',
  executable('test-eel-primary-mask',
    [ 'test-eel-primary-mask.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private ],
  ),
  args: []
)

test('rgeahzbr Entry context menu test',
  executable('test-nemo-entry-menu',
    [ 'test-nemo-entry-menu.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

# Its binary files are written from literals with NULs inside, and a wrong
# length reads past one with nothing to say so. ASan on this one executable
# sees that; leaks are left to other tests, since GTK's own would drown it.
testSan = []
if not is_windows and cc.has_link_argument('-fsanitize=address')
  testSan = [ '-fsanitize=address', '-fno-omit-frame-pointer' ]
endif

test('rffvm2bg Search content test',
  executable('test-nemo-search-content',
    [ 'test-nemo-search-content.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
    c_args: nemo_definitions + testSan,
    link_args: testSan,
  ),
  env: [ 'ASAN_OPTIONS=detect_leaks=0' ],
  args: []
)

if not is_windows
  test('rhaqte90 Scratch guard test',
    executable('test-scratch-guard',
      [ 'test-scratch-guard.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ glib, test_scratch ],
    ),
    args: []
  )

  test('rek3gd32 DnD selection parser test',
    executable('test-nemo-dnd',
      [ 'test-nemo-dnd.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private ],
    ),
    args: []
  )
endif

test('rhtjdp1g Rename selection test',
  executable('test-eel-rename-region',
    [ 'test-eel-rename-region.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('rhtjdp1h Message dialog text test',
  executable('test-eel-stock-dialogs',
    [ 'test-eel-stock-dialogs.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private ],
  ),
  args: []
)

test('rek3gd33 Terminal fallback test',
  executable('test-eel-terminal',
    [ 'test-eel-terminal.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('rek3gd34 Extension column test',
  executable('test-nemo-column',
    [ 'test-nemo-column.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_extension ],
  ),
  args: []
)

if is_windows
  # The recycle-bin monitor is polled, so the same-count case has to sit through
  # a few turns of it - well past the 30s meson allows by default. On a machine
  # with a full bin the whole run has been seen to take four and a half minutes.
  # It watches the real bin for a quiet spell, so nothing else may run beside
  # it: the archive combinations test recycles into that same bin.
  test('rcgzbyar Trash win32 test',
    executable('test-nemo-trash-win32',
      [ 'test-nemo-trash-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private ],
    ),
    args: [],
    timeout: 600,
    is_parallel: false,
  )
endif

test('rge1srj8 Crash reporter test',
  executable('test-nemo-crash',
    [ 'test-nemo-crash.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: [],
  timeout: 120
)

if is_windows
  test('rdcvb368 Shortcut win32 test',
    executable('test-nemo-shortcut-win32',
      [ 'test-nemo-shortcut-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, win_shell, test_scratch ],
    ),
    args: []
  )
endif

test('rch2h9g0 GIO fileops probe',
  executable('test-gio-fileops',
    [ 'test-gio-fileops.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

# Off since the file operations interface it exported was removed: nothing but a
# window may start a trash, delete or move. test-nemo-dbus-manager.c needs
# nemo-dbus-manager.h, which went with it.
# test('DBus manager no-bus test',
#   executable('test-nemo-dbus-manager',
#     [ 'test-nemo-dbus-manager.c' ],
#     include_directories: [ rootInclude, ],
#     dependencies: [ gtk, nemo_private ],
#   ),
#   args: []
# )

test('78k3mysg Copy test',
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

test('rhr6ggmr Link copy job, every answer', link_copy_job, args: [])

make_link_job = executable('test-nemo-make-link-job',
  [ 'test-nemo-make-link-job.c', 'test.c' ],
  include_directories: [ rootInclude, ],
  dependencies: [ gtk, nemo_private, test_scratch ],
  c_args: nemo_definitions,
)

test('rhmye3d0 Make link relative', make_link_job, args: [ 'relative' ])
test('rhmye3d1 Make link absolute', make_link_job, args: [ 'absolute' ])
test('rhmye3d2 Make link hardlink', make_link_job, args: [ 'hardlink' ])
test('rhn5ewrg Make link shortcut', make_link_job, args: [ 'shortcut' ])
# A shortcut always has every path since backlog 2026092813381440, so an
# absolute-only one can no longer be asked for.
# test('rhn92e10 Make link shortcut absolute', make_link_job, args: [ 'shortcut-absolute' ])
test('rhnb1z7g Make link shortcut portable', make_link_job, args: [ 'shortcut-portable' ])
test('rhnq7f38 Make link names', make_link_job, args: [ 'names' ])
test('rhr6ggms Make link, every answer', make_link_job, args: [ 'every' ])
test('rj05egmb Make link redo', make_link_job, args: [ 'redo' ])
if is_windows
  test('rhmye3d3 Make link junction', make_link_job, args: [ 'junction' ])
endif

link_delete_job = executable('test-nemo-link-delete-job',
  [ 'test-nemo-link-delete-job.c', 'test.c' ],
  include_directories: [ rootInclude, ],
  dependencies: [ gtk, nemo_private, test_scratch ],
  c_args: nemo_definitions,
)

test('rh5nme6g Link delete leaves the target', link_delete_job, args: [ 'delete' ])
test('rhtq57n2 Link delete, asked by the job', link_delete_job, args: [ 'delete-asked' ])
test('rh5nme6h Link move leaves the target', link_delete_job, args: [ 'move' ])

# Carries the resource for the emblem art, as the next test does.
test('rhtmbdmc Link emblems test',
  executable('test-nemo-emblems',
    [ 'test-nemo-emblems.c' ] + gresources,
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

# Carries its own copy of the resource, for the reason the next test explains.
test('rgak6kmg App resources test',
  executable('test-nemo-app-resources',
    [ 'test-nemo-app-resources.c' ] + gresources,
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private ],
  ),
  args: []
)

# The resource has to be compiled straight in for the same reason the exe does:
# nothing else references the self-registering object, so the linker drops it.
test('rf2w8yxr Preferences widgets test',
  executable('test-nemo-prefs-widgets',
    [ 'test-nemo-prefs-widgets.c' ] + gresources,
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private ],
  ),
  args: []
)

test('rhtmbdmd Preferences dialog test',
  executable('test-nemo-prefs-dialog',
    [ 'test-nemo-prefs-dialog.c', '../src/nemo-file-management-properties.c' ] + gresources,
    include_directories: [ rootInclude, include_directories('../src') ],
    dependencies: [ gtk, nemo_private, test_scratch ],
    c_args: nemo_definitions,
  ),
  args: []
)

test('rhtmbdme Search box Enter test',
  executable('test-nemo-query-editor',
    [ 'test-nemo-query-editor.c', '../src/nemo-query-editor.c' ] + gresources,
    include_directories: [ rootInclude, include_directories('../src') ],
    dependencies: [ gtk, nemo_private, test_scratch ],
    c_args: nemo_definitions,
  ),
  args: []
)

test('rhtmbdmf Preferences Current tab test',
  executable('test-nemo-prefs-current',
    [ 'test-nemo-prefs-current.c', '../src/nemo-prefs-current-folder.c' ] + gresources,
    include_directories: [ rootInclude, include_directories('../src') ],
    dependencies: [ gtk, nemo_private, test_scratch ],
    c_args: nemo_definitions,
  ),
  args: []
)

if is_windows
  test('rf2w8yxs Path separator test',
    executable('test-nemo-path-separator-win32',
      [ 'test-nemo-path-separator-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )
endif

test('rfwwdyvg Link copy test',
  executable('test-nemo-link-copy',
    [ 'test-nemo-link-copy.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('rhqxx81r Link edit test',
  executable('test-nemo-link-edit',
    [ 'test-nemo-link-edit.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('rff477f0 Shortcut name test',
  executable('test-nemo-shortcut-name',
    [ 'test-nemo-shortcut-name.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('rh2nkbar Folder settings test',
  executable('test-nemo-folder-settings',
    [ 'test-nemo-folder-settings.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
    c_args: nemo_definitions,
  ),
  args: []
)

if is_windows
  test('rf2v65mr Dot-file switch test',
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
  test('renyy630 Win32 argument quoting test',
    executable('test-nemo-view-win32',
      [ 'test-nemo-view-win32.c', '../src/nemo-view-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private ],
      c_args: nemo_definitions,
    ),
    args: []
  )
endif

if is_windows
  test('rexrjb60 DPI win32 test',
    executable('test-nemo-dpi-win32',
      [ 'test-nemo-dpi-win32.c', '../src/nemo-dpi-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private ],
      c_args: nemo_definitions,
    ),
    args: []
  )
endif

# Puts three windows on screen and needs the foreground, so it skips over ssh.
if is_windows
  test('rjf8db1q Tab move foreground right test',
    executable('test-nemo-raise-win32',
      [ 'test-nemo-raise-win32.c', '../src/nemo-window-at-point-win32.c' ],
      include_directories: [ rootInclude, include_directories('../src') ],
      dependencies: [ gtk ],
    ),
    args: []
  )
endif

if is_windows
  test('renz2428 Network win32 test',
    executable('test-nemo-network-win32',
      [ 'test-nemo-network-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private ],
    ),
    args: []
  )
endif

if is_windows
  test('reybtrrr Properties fallback win32 test',
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
  test('rep11s5r Drive root name test',
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
  test('rep11s5s File refresh churn test',
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
  test('rep167r8 Thumbnail win32 type test',
    executable('test-nemo-thumbnail-win32',
      [ 'test-nemo-thumbnail-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
      c_args: nemo_definitions,
    ),
    args: []
  )
endif

test('repkeg1g Appearance themes test',
  # The theme resource is linked into the app, not into libnemo-private, so a
  # test that checks it has to carry its own copy.
  executable('test-nemo-appearance',
    [ 'test-nemo-appearance.c' ] + gresources + themeGresources,
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('rexta5a8 Column layout test',
  executable('test-nemo-column-layout',
    [ 'test-nemo-column-layout.c', '../src/nemo-column-layout.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private ],
    c_args: nemo_definitions,
  ),
  args: []
)

test('rjm2rvjn Find mode columns test',
  executable('test-nemo-search-columns',
    [ 'test-nemo-search-columns.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
    c_args: nemo_definitions,
  ),
  args: []
)

test('rh0ktwj0 Pane layout test',
  executable('test-nemo-pane-layout',
    [ 'test-nemo-pane-layout.c', '../src/nemo-pane-layout.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private ],
    c_args: nemo_definitions,
  ),
  args: []
)

test('rh0ms87r Proportional paned test',
  executable('test-nemo-proportional-paned',
    [ 'test-nemo-proportional-paned.c', '../src/nemo-proportional-paned.c', '../src/nemo-pane-layout.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private ],
    c_args: nemo_definitions,
  ),
  args: []
)

test('rfzn5ttr Search group test',
  executable('test-nemo-search-group',
    [ 'test-nemo-search-group.c', '../src/nemo-list-model.c' ],
    include_directories: [ rootInclude, include_directories('../src') ],
    dependencies: [ gtk, nemo_private, egg, test_scratch ],
    c_args: nemo_definitions,
  ),
  args: []
)

test('rhbcgvhg Icon size test',
  executable('test-nemo-icon-size',
    [ 'test-nemo-icon-size.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private ],
    c_args: nemo_definitions,
  ),
  args: []
)

test('rhb624a0 List expander test',
  executable('test-nemo-list-expander',
    [ 'test-nemo-list-expander.c', '../src/nemo-list-model.c' ],
    include_directories: [ rootInclude, include_directories('../src') ],
    dependencies: [ gtk, nemo_private, egg, test_scratch ],
    c_args: nemo_definitions,
  ),
  args: []
)

test('rh2af9s0 Tree folders test',
  executable('test-nemo-tree-folders',
    [ 'test-nemo-tree-folders.c', '../src/nemo-tree-sidebar-model.c' ],
    include_directories: [ rootInclude, include_directories('../src') ],
    dependencies: [ gtk, nemo_private, test_scratch ],
    c_args: nemo_definitions,
  ),
  args: []
)

test('rhtmbdmg Status bar name test',
  executable('test-nemo-status-name',
    [ 'test-nemo-status-name.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('rhtmbdmh Tab strip test',
  executable('test-nemo-notebook',
    [ 'test-nemo-notebook.c', '../src/nemo-notebook.c' ],
    include_directories: [ rootInclude, include_directories('../src') ],
    dependencies: [ gtk, nemo_private, test_scratch ],
    c_args: nemo_definitions,
  ),
  args: []
)

test('rg3wt7d8 New process argv test',
  executable('test-nemo-new-process',
    [ 'test-nemo-new-process.c', '../src/nemo-new-process.c' ],
    include_directories: [ rootInclude, include_directories('../src') ],
    dependencies: [ gtk, nemo_private ],
    c_args: nemo_definitions,
  ),
  args: []
)

if not is_windows
  test('rhtjdp1j Clipboard drag collision test',
    executable('test-nemo-clipboard',
      [ 'test-nemo-clipboard.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )
endif

if not is_windows
  test('rhtjdp1k Desktop terminal setting test',
    executable('test-nemo-desktop-terminal',
      [ 'test-nemo-desktop-terminal.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )
endif

if not is_windows
  test('rgahvdsr Runtime environment test',
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
  test('rgahgzt8 Extensions list flag test',
    executable('test-nemo-extensions-list',
      [ 'test-nemo-extensions-list.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private ],
    ),
    args: [ nemo ],
  )
endif

cli_version = executable('test-nemo-cli-version',
  [ 'test-nemo-cli-version.c' ],
  include_directories: [ rootInclude, ],
  dependencies: [ glib ],
)

if is_windows
  test('rjgw21aw Version and option parse test',
    cli_version,
    args: [ nemo, meson.project_version() ],
  )
endif

if not is_windows
  test('rh3nba2g Version with no display test',
    cli_version,
    args: [ nemo, meson.project_version(),
            meson.current_source_dir() / '..' / 'build-number.py', py.full_path() ],
  )

  test('rhmr6qgr Window under the pointer test',
    executable('test-nemo-window-at-point',
      [ 'test-nemo-window-at-point.c', '../src/nemo-window-at-point.c' ],
      include_directories: [ rootInclude, include_directories('../src') ],
      dependencies: [ gtk, x11, test_scratch ],
    ),
    args: []
  )

  test('rhtq57n3 Action programs in their own folder test',
    executable('test-nemo-action-exec',
      [ 'test-nemo-action-exec.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )

  test('rjhbbg9n Share visits test',
    executable('test-nemo-share-visits',
      [ 'test-nemo-share-visits.c' ],
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
  test('rj4jkr22 List view work per row test',
    executable('test-nemo-list-view-work',
      [ 'test-nemo-list-view-work.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ glib, gio, test_scratch ],
    ),
    args: [ nemo, list_view_counts ],
    timeout: 180
  )

  # Preloaded too; it clicks from inside the program.
  places_focus_probe = shared_module('test-nemo-places-focus-probe',
    [ 'test-nemo-places-focus-probe.c' ],
    dependencies: [ gtk ],
  )
  test('rjedw75s Places focus test',
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
  test('rjhemba0 Places bookmarks heading after a settings edit test',
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
  test('rjefm41d Keyboard menu beside the item test',
    executable('test-nemo-menu-key-place',
      [ 'test-nemo-menu-key-place.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ glib, gio, test_scratch ],
    ),
    args: [ nemo, menu_key_place_probe ],
    timeout: 180
  )

  icon_atk_probe = shared_module('test-nemo-icon-atk-probe',
    [ 'test-nemo-icon-atk-probe.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk ],
  )
  test('rjerav6k Icon accessible extents test',
    executable('test-nemo-icon-atk',
      [ 'test-nemo-icon-atk.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ glib, gio, test_scratch ],
    ),
    args: [ nemo, icon_atk_probe ],
    timeout: 120
  )

  test('rhtq57n4 Clean start test',
    executable('test-nemo-startup-clean',
      [ 'test-nemo-startup-clean.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ glib, gio, x11, test_scratch ],
    ),
    args: [ nemo ],
    timeout: 120
  )

  test('rg3wt7d9 Instances test',
    executable('test-nemo-instances',
      [ 'test-nemo-instances.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: [ nemo ],
    timeout: 120
  )

  test('rj750n43 File cache written at quit test',
    executable('test-nemo-cache-quit',
      [ 'test-nemo-cache-quit.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, x11, test_scratch ],
    ),
    args: [ nemo ],
    timeout: 180
  )

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
  test('rjch1b9a Shortcut saved and notice withdrawn at quit test',
    quit_saves,
    args: [ nemo, quit_hooks ],
    timeout: 120
  )
  test('rjcscb0t Shortcut file beside settings and carried over once test',
    quit_saves,
    args: [ nemo, quit_hooks, 'carry-over' ],
    timeout: 240
  )

  # Preloaded into every copy the test starts; it links nothing of ours.
  tab_move_probe = shared_module('test-nemo-tab-move-probe',
    [ 'test-nemo-tab-move-probe.c' ],
    dependencies: [ gtk ],
  )
  test('rhmr6qgs Tab move between processes test',
    executable('test-nemo-tab-move',
      [ 'test-nemo-tab-move.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, test_scratch ],
    ),
    args: [ nemo, tab_move_probe ],
    timeout: 180
  )
  test('rjf00qfj Menus freed after closing test',
    executable('test-nemo-menus-freed',
      [ 'test-nemo-menus-freed.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ glib, test_scratch ],
    ),
    args: [ nemo, tab_move_probe ],
    timeout: 120
  )

  xtst = dependency('xtst', required: false)
  if xtst.found()
    test('rj04ta3n Tree menu key test',
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
    test('rjahhesy Held view settings handlers test',
      executable('test-nemo-held-view',
        [ 'test-nemo-held-view.c' ],
        include_directories: [ rootInclude, shclInclude, ],
        dependencies: [ glib, x11, xtst, math, test_scratch ],
      ),
      args: [ nemo, held_view_hooks ],
      timeout: 180
    )
  endif
endif

test('rezjn4m8 Filename extension test',
  executable('test-nemo-filename-extension',
    [ 'test-nemo-filename-extension.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private ],
  ),
  args: []
)

test('rh65bpp8 Owner columns test',
  executable('test-nemo-owner-columns',
    [ 'test-nemo-owner-columns.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch, win_shell ],
  ),
  args: []
)

test('rhtjdp1m Uri under another test',
  executable('test-nemo-uri-under',
    [ 'test-nemo-uri-under.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private ],
  ),
  args: []
)

test('rh648pjg Path forms test',
  executable('test-nemo-path-forms',
    [ 'test-nemo-path-forms.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private ],
  ),
  args: []
)

test('rh3bha78 Window title test',
  executable('test-nemo-window-title',
    [ 'test-nemo-window-title.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('rhdg25n0 Uptime test',
  executable('test-nemo-uptime',
    [ 'test-nemo-uptime.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private ],
  ),
  args: []
)

test('rjmpxtbg Windows paths in user text test',
  executable('test-nemo-user-text',
    [ 'test-nemo-user-text.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('rexkeyng Command template test',
  executable('test-nemo-command-template',
    [ 'test-nemo-command-template.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('rev86z08 Archive options test',
  executable('test-nemo-archive',
    [ 'test-nemo-archive.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('rhtmbdmj Compress dialog height test',
  executable('test-nemo-archive-dialog',
    [ 'test-nemo-archive-dialog.c', '../src/nemo-archive-dialog.c' ],
    include_directories: [ rootInclude, include_directories('../src') ],
    dependencies: [ gtk, nemo_private, x11, test_scratch ],
    c_args: nemo_definitions,
  ),
  args: []
)

test('rhae85g0 Archive settings test',
  executable('test-nemo-archive-settings',
    [ 'test-nemo-archive-settings.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

# Reads the archives it wrote back with libarchive, so it needs that library.
test('rewygsbg Archive job test',
  executable('test-nemo-archive-job',
    [ 'test-archive-job.c', 'test.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ archive, gtk, nemo_private, test_scratch ],
    c_args: nemo_definitions,
  ),
  args: []
)

test('rj4jewn6 Archive check cost test',
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
  test('rjbpyy28 Archive stop time test',
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
test('rhr6ggmt Archive option combinations',
  executable('test-nemo-archive-combos',
    [ 'test-nemo-archive-combos.c', 'test.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ archive, gtk, nemo_private, test_scratch ],
    c_args: nemo_definitions,
  ),
  args: [],
  timeout: 600,
)

test('reww9h2r Extract naming test',
  executable('test-nemo-extract',
    [ 'test-nemo-extract.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private ],
  ),
  args: []
)

test('rh62t0e0 Archive mount address test',
  executable('test-nemo-archive-mount',
    [ 'test-nemo-archive-mount.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private ],
  ),
  args: []
)

# Writes its own zip with libarchive, so it needs the library the app links.
test('reww9h2s Extract job test',
  executable('test-nemo-extract-job',
    [ 'test-extract-job.c', 'test.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ archive, gtk, nemo_private, test_scratch ],
    c_args: nemo_definitions,
  ),
  args: []
)

test('rg40vg48 Data dirs test',
  executable('test-nemo-data-dirs',
    [ 'test-nemo-data-dirs.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private ],
  ),
  args: []
)

test('ret6zctg Path list text test',
  executable('test-nemo-path-list',
    [ 'test-nemo-path-list.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private ],
  ),
  args: []
)

test('rhbe2n9g Image folder test',
  executable('test-nemo-image-folder',
    [ 'test-nemo-image-folder.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('rhe0xz31 Photoshop reader test',
  executable('test-nemo-psd',
    [ 'test-nemo-psd.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

testRaw = executable('test-nemo-raw',
  [ 'test-nemo-raw.c' ],
  include_directories: [ rootInclude, ],
  dependencies: [ gtk, nemo_private, test_scratch ],
)

test('rhg7vh28 Camera raw reader test', testRaw, args: [])

# Again with GLib clearing what a shrunk array gives up, so the fixtures can't
# lean on bytes they no longer own.
test('rj9v7n76 Camera raw reader test, cleared memory', testRaw,
  env: [ 'G_DEBUG=gc-friendly' ],
  args: []
)

# Windows reads shortcuts through the shell and never builds this reader.
if not is_windows
  test('rhmxm5ah Windows shortcut reader test',
    executable('test-nemo-lnk',
      [ 'test-nemo-lnk.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )

  test('rhnqqpm8 Folder shortcuts sort with folders test',
    executable('test-nemo-lnk-sort',
      [ 'test-nemo-lnk-sort.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )
endif

test('rhg8y5f0 ImageMagick thumbnail test',
  executable('test-nemo-magick',
    [ 'test-nemo-magick.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

# Runs on a copy of the loader list the system's gdk-pixbuf reads.
if not is_windows
  test('rjnz8zfz ImageMagick for missing pixbuf loaders test',
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

test('rhe0xz32 Thumbnail hold test',
  executable('test-nemo-thumbnail-hold',
    [ 'test-nemo-thumbnail-hold.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('rhfc3tt8 Thumbnail memory test',
  executable('test-nemo-thumbnail-memory',
    [ 'test-nemo-thumbnail-memory.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('rhfdeb5g Thumbnail jobs test',
  executable('test-nemo-thumbnail-jobs',
    [ 'test-nemo-thumbnail-jobs.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('rhf905br Thumbnail order test',
  executable('test-nemo-thumbnail-order',
    [ 'test-nemo-thumbnail-order.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('rj40hkdr Thumbnail of an edited file test',
  executable('test-nemo-thumbnail-edited',
    [ 'test-nemo-thumbnail-edited.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

# Its stand-in thumbnailer is matched on image/png, and Windows types a file
# by its extension instead.
if not is_windows
  test('rj043mnp Thumbnail zoom during render test',
    executable('test-nemo-thumbnail-zoom',
      [ 'test-nemo-thumbnail-zoom.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )
endif

test('rjffcm7d Thumbnail stopped once nothing wants it test',
  executable('test-nemo-thumbnail-stop',
    [ 'test-nemo-thumbnail-stop.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('rhdfp7t8 Menu paths test',
  executable('test-nemo-menu-paths',
    [ 'test-nemo-menu-paths.c' ] + gresources,
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private ],
  ),
  args: []
)

test('rhg6h4m0 Deep counts test',
  executable('test-nemo-deep-counts',
    [ 'test-nemo-deep-counts.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('rfaz67tr Directory load clean test',
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
  test('rhtwm2c8 Share check does no I/O', share_win32, args: [ 'share' ])
  test('rhtwm2c9 Junction to a missing folder lists', share_win32, args: [ 'dangling' ])

  # Maps a free drive letter for the run, so it waits for nothing else that
  # lists the drives.
  test('rjhvmm6f Mapped drive not asked',
    executable('test-nemo-drives-win32',
      [ 'test-nemo-drives-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private ],
    ),
    args: [],
    is_parallel: false,
  )

  test('rhtwm2ca Permissions source test',
    executable('test-nemo-perm-source-win32',
      [ 'test-nemo-perm-source-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, win_shell, test_scratch ],
    ),
    args: []
  )
endif

if is_windows
  test('rgaz5cn8 Metadata ready test',
    executable('test-nemo-metadata-ready-win32',
      [ 'test-nemo-metadata-ready-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: [],
    timeout: 300
  )
endif

test('rfazc870 Config schema drift test',
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

  test('rffdvgpg Long path enumeration test',
    executable('test-nemo-dir-enum-win32',
      [ 'test-nemo-dir-enum-win32.c' ] + longPathManifest,
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )

  test('rhtwm2cb Application manifest test',
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
  test('rffkjp10 First run test',
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
  test('rh5f9h18 Extension load test',
    executable('test-nemo-extension-load',
      [ 'test-nemo-extension-load.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, gmodule, nemo_extension ],
    ),
    args: [ test_extension_module ]
  )
endif

# The document converters, run on files written for the purpose.
test('rfhnaccg Search helpers test',
  executable('test-nemo-search-helpers',
    [ 'test-nemo-search-helpers.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, libgsf, test_scratch ],
    c_args: nemo_definitions,
  ),
  args: [],
  env: [ 'NEMO_SEARCH_HELPER_DIR=' + search_helper_dir ],
  depends: search_helper_exes,
)

test('rjm9n8sr Bookmark name and icon test',
  executable('test-nemo-bookmark-info',
    [ 'test-nemo-bookmark-info.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

if is_windows
  test('rfhnacch Windows Search test',
    executable('test-nemo-search-win32',
      [ 'test-nemo-search-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )

  test('rfhpkpn0 File associations test',
    executable('test-nemo-associations-win32',
      [ 'test-nemo-associations-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )

  test('rfnmh050 Program launch test',
    executable('test-nemo-launch-win32',
      [ 'test-nemo-launch-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )

  test('rfhr0zw0 Shell icon test',
    executable('test-nemo-shell-icon-win32',
      [ 'test-nemo-shell-icon-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, win_shell, test_scratch ],
    ),
    args: []
  )

  test('rfmxrdpg Link type test',
    executable('test-nemo-link-info-win32',
      [ 'test-nemo-link-info-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )

  test('rjehwjw7 Link end test',
    executable('test-nemo-link-end-win32',
      [ 'test-nemo-link-end-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )

  test('rfmxrdph Clipboard win32 test',
    executable('test-nemo-clipboard-win32',
      [ 'test-nemo-clipboard-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )

  test('rftjs1yr Drag source win32 test',
    executable('test-nemo-dnd-win32',
      [ 'test-nemo-dnd-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )

  test('rjkybspg ImageMagick start win32 test',
    executable('test-nemo-magick-win32',
      [ 'test-nemo-magick-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: [ executable('test-fake-magick', [ 'test-fake-magick.c' ]) ]
  )

  fake_tool = executable('test-fake-tool', [ 'test-fake-tool.c' ])

  test('rjm4ctwh Tool start win32 test',
    executable('test-nemo-tool-start-win32',
      [ 'test-nemo-tool-start-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
      c_args: nemo_definitions,
    ),
    args: [ fake_tool ],
    timeout: 120
  )

  test('rjmb3j8p Action start win32 test',
    executable('test-nemo-action-start-win32',
      [ 'test-nemo-action-start-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: [ fake_tool,
            executable('test-fake-tool-windowed', [ 'test-fake-tool.c' ], win_subsystem: 'windows') ],
    timeout: 60
  )

  test('rjkwtgrm Drop keys win32 test',
    executable('test-nemo-drop-keys-win32',
      [ 'test-nemo-drop-keys-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private ],
    ),
    args: []
  )
endif
