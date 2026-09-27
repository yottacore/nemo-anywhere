
# Scratch directories made through this are removed when the test exits.
test_scratch = declare_dependency(
  link_with: static_library('test-scratch',
    [ 'test-scratch.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private ],
  ),
)

test('Search Engine test',
  executable('test-nemo-search-engine',
    [ 'test-nemo-search-engine.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: [],
  timeout: 180        # three searches, each with its own 30s ceiling
)

test('Search folder test',
  executable('test-nemo-search-folder',
    [ 'test-nemo-search-folder.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: [],
  timeout: 120
)

test('Directory monitor test',
  executable('test-nemo-directory-monitor',
    [ 'test-nemo-directory-monitor.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: [],
  timeout: 120
)

test('Folder time after a change inside it test',
  executable('test-nemo-folder-mtime',
    [ 'test-nemo-folder-mtime.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: [],
  timeout: 60
)

test('Small file copy test',
  executable('test-nemo-small-copy',
    [ 'test-nemo-small-copy.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('Drop action from another program test',
  executable('test-nemo-drop-action',
    [ 'test-nemo-drop-action.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('Drag drop confirm test',
  executable('test-nemo-drag-confirm',
    [ 'test-nemo-drag-confirm.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('Config defaults list test',
  executable('test-nemo-config-catalog',
    [ 'test-nemo-config-catalog.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('Config store test',
  executable('test-nemo-config',
    [ 'test-nemo-config.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: [],
  timeout: 90         # a hundred external rewrites, each waiting on the file monitor
)

test('Delete test guard arming test',
  executable('test-nemo-delete-testguard',
    [ 'test-nemo-delete-testguard.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('Guard pauses the job test',
  executable('test-nemo-guard-pause',
    [ 'test-nemo-guard-pause.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('Job queue test',
  executable('test-nemo-job-queue',
    [ 'test-nemo-job-queue.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('Cancelled drop leaves the files test',
  executable('test-nemo-drop-cancel',
    [ 'test-nemo-drop-cancel.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('Empty trash asks test',
  executable('test-nemo-empty-trash',
    [ 'test-nemo-empty-trash.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('Delete from trash asks test',
  executable('test-nemo-delete-from-trash',
    [ 'test-nemo-delete-from-trash.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('Favorites test',
  executable('test-nemo-favorites',
    [ 'test-nemo-favorites.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('File checksum test',
  executable('test-nemo-file-digest',
    [ 'test-nemo-file-digest.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('File checksum attribute test',
  executable('test-nemo-file-xattr',
    [ 'test-nemo-file-xattr.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('File cache store test',
  executable('test-nemo-cache-db',
    [ 'test-nemo-cache-db.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('File cache prune test',
  executable('test-nemo-cache-prune',
    [ 'test-nemo-cache-prune.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch, sqlite ],
  ),
  args: []
)

test('Thumbnail store test',
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

test('Thumbnail factory test',
  executable('test-nemo-thumbnail',
    [ 'test-nemo-thumbnail.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: [],
  timeout: 180        # the hung-helper case waits out the thumbnailer timeout
)

test('Metadata store test',
  executable('test-nemo-metadata-store',
    [ 'test-nemo-metadata-store.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

# Needs the slug define: the point of the test is the path the app really builds.
test('Config root test',
  executable('test-nemo-config-root',
    [ 'test-nemo-config-root.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
    c_args: nemo_definitions,
  ),
  args: []
)

test('User input parse test',
  executable('test-eel-user-input',
    [ 'test-eel-user-input.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

# symlink(2) and the guard-page mmap below are POSIX; none of these cross-build.
if not is_windows
  test('Symlink recurse premise test',
    executable('test-nemo-symlink-recurse',
      [ 'test-nemo-symlink-recurse.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )

  test('Delete guard test',
    executable('test-nemo-delete-guard',
      [ 'test-nemo-delete-guard.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )

  test('Scratch cleanup test',
    executable('test-nemo-scratch',
      [ 'test-nemo-scratch.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )
endif

test('Search invalid regex test',
  executable('test-nemo-search-regex',
    [ 'test-nemo-search-regex.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

if is_windows
  test('Delete guard win32 test',
    executable('test-nemo-delete-guard-win32',
      [ 'test-nemo-delete-guard-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )
endif

test('Treeview hit test',
  executable('test-eel-treeview-hit',
    [ 'test-eel-treeview-hit.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private ],
  ),
  args: []
)

test('Clipped cell text test',
  executable('test-eel-clipped-cell',
    [ 'test-eel-clipped-cell.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private ],
  ),
  args: []
)

test('Keyboard context menu test',
  executable('test-eel-context-menu',
    [ 'test-eel-context-menu.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('Escape forgets the cursor test',
  executable('test-eel-forget-cursor',
    [ 'test-eel-forget-cursor.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private ],
  ),
  args: []
)

test('Focus guard test',
  executable('test-eel-focus-guard',
    [ 'test-eel-focus-guard.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private ],
  ),
  args: []
)

test('Row hover tint test',
  executable('test-nemo-row-hover',
    [ 'test-nemo-row-hover.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('Primary mask test',
  executable('test-eel-primary-mask',
    [ 'test-eel-primary-mask.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private ],
  ),
  args: []
)

test('Entry context menu test',
  executable('test-nemo-entry-menu',
    [ 'test-nemo-entry-menu.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('Search content test',
  executable('test-nemo-search-content',
    [ 'test-nemo-search-content.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
    c_args: nemo_definitions,
  ),
  args: []
)

if not is_windows
  test('Scratch guard test',
    executable('test-scratch-guard',
      [ 'test-scratch-guard.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ glib, test_scratch ],
    ),
    args: []
  )

  test('DnD selection parser test',
    executable('test-nemo-dnd',
      [ 'test-nemo-dnd.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private ],
    ),
    args: []
  )
endif

test('Rename selection test',
  executable('test-eel-rename-region',
    [ 'test-eel-rename-region.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('Message dialog text test',
  executable('test-eel-stock-dialogs',
    [ 'test-eel-stock-dialogs.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private ],
  ),
  args: []
)

test('Terminal fallback test',
  executable('test-eel-terminal',
    [ 'test-eel-terminal.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('Extension column test',
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
  test('Trash win32 test',
    executable('test-nemo-trash-win32',
      [ 'test-nemo-trash-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private ],
    ),
    args: [],
    timeout: 600
  )
endif

test('Crash reporter test',
  executable('test-nemo-crash',
    [ 'test-nemo-crash.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: [],
  timeout: 120
)

if is_windows
  test('Shortcut win32 test',
    executable('test-nemo-shortcut-win32',
      [ 'test-nemo-shortcut-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, win_shell, test_scratch ],
    ),
    args: []
  )
endif

test('GIO fileops probe',
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

test('Copy test',
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

test('Link copy job, every answer', link_copy_job, args: [])

make_link_job = executable('test-nemo-make-link-job',
  [ 'test-nemo-make-link-job.c', 'test.c' ],
  include_directories: [ rootInclude, ],
  dependencies: [ gtk, nemo_private, test_scratch ],
  c_args: nemo_definitions,
)

test('Make link relative', make_link_job, args: [ 'relative' ])
test('Make link absolute', make_link_job, args: [ 'absolute' ])
test('Make link hardlink', make_link_job, args: [ 'hardlink' ])
test('Make link shortcut', make_link_job, args: [ 'shortcut' ])
test('Make link shortcut absolute', make_link_job, args: [ 'shortcut-absolute' ])
test('Make link shortcut portable', make_link_job, args: [ 'shortcut-portable' ])
test('Make link names', make_link_job, args: [ 'names' ])
test('Make link, every answer', make_link_job, args: [ 'every' ])
if is_windows
  test('Make link junction', make_link_job, args: [ 'junction' ])
endif

link_delete_job = executable('test-nemo-link-delete-job',
  [ 'test-nemo-link-delete-job.c', 'test.c' ],
  include_directories: [ rootInclude, ],
  dependencies: [ gtk, nemo_private, test_scratch ],
  c_args: nemo_definitions,
)

test('Link delete leaves the target', link_delete_job, args: [ 'delete' ])
test('Link delete, asked by the job', link_delete_job, args: [ 'delete-asked' ])
test('Link move leaves the target', link_delete_job, args: [ 'move' ])

# Carries the resource for the emblem art, as the next test does.
test('Link emblems test',
  executable('test-nemo-emblems',
    [ 'test-nemo-emblems.c' ] + gresources,
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

# Carries its own copy of the resource, for the reason the next test explains.
test('App resources test',
  executable('test-nemo-app-resources',
    [ 'test-nemo-app-resources.c' ] + gresources,
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private ],
  ),
  args: []
)

# The resource has to be compiled straight in for the same reason the exe does:
# nothing else references the self-registering object, so the linker drops it.
test('Preferences widgets test',
  executable('test-nemo-prefs-widgets',
    [ 'test-nemo-prefs-widgets.c' ] + gresources,
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private ],
  ),
  args: []
)

test('Preferences dialog test',
  executable('test-nemo-prefs-dialog',
    [ 'test-nemo-prefs-dialog.c', '../src/nemo-file-management-properties.c' ] + gresources,
    include_directories: [ rootInclude, include_directories('../src') ],
    dependencies: [ gtk, nemo_private, test_scratch ],
    c_args: nemo_definitions,
  ),
  args: []
)

test('Search box Enter test',
  executable('test-nemo-query-editor',
    [ 'test-nemo-query-editor.c', '../src/nemo-query-editor.c' ] + gresources,
    include_directories: [ rootInclude, include_directories('../src') ],
    dependencies: [ gtk, nemo_private, test_scratch ],
    c_args: nemo_definitions,
  ),
  args: []
)

test('Preferences Current tab test',
  executable('test-nemo-prefs-current',
    [ 'test-nemo-prefs-current.c', '../src/nemo-prefs-current-folder.c' ] + gresources,
    include_directories: [ rootInclude, include_directories('../src') ],
    dependencies: [ gtk, nemo_private, test_scratch ],
    c_args: nemo_definitions,
  ),
  args: []
)

if is_windows
  test('Path separator test',
    executable('test-nemo-path-separator-win32',
      [ 'test-nemo-path-separator-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )
endif

test('Link copy test',
  executable('test-nemo-link-copy',
    [ 'test-nemo-link-copy.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('Link edit test',
  executable('test-nemo-link-edit',
    [ 'test-nemo-link-edit.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('Shortcut name test',
  executable('test-nemo-shortcut-name',
    [ 'test-nemo-shortcut-name.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('Folder settings test',
  executable('test-nemo-folder-settings',
    [ 'test-nemo-folder-settings.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
    c_args: nemo_definitions,
  ),
  args: []
)

if is_windows
  test('Dot-file switch test',
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
  test('Win32 argument quoting test',
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
  test('DPI win32 test',
    executable('test-nemo-dpi-win32',
      [ 'test-nemo-dpi-win32.c', '../src/nemo-dpi-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private ],
      c_args: nemo_definitions,
    ),
    args: []
  )
endif

if is_windows
  test('Network win32 test',
    executable('test-nemo-network-win32',
      [ 'test-nemo-network-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private ],
    ),
    args: []
  )
endif

if is_windows
  test('Properties fallback win32 test',
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
  test('Drive root name test',
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
  test('File refresh churn test',
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
  test('Thumbnail win32 type test',
    executable('test-nemo-thumbnail-win32',
      [ 'test-nemo-thumbnail-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
      c_args: nemo_definitions,
    ),
    args: []
  )
endif

test('Appearance themes test',
  # The theme resource is linked into the app, not into libnemo-private, so a
  # test that checks it has to carry its own copy.
  executable('test-nemo-appearance',
    [ 'test-nemo-appearance.c' ] + gresources + themeGresources,
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('Column layout test',
  executable('test-nemo-column-layout',
    [ 'test-nemo-column-layout.c', '../src/nemo-column-layout.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private ],
    c_args: nemo_definitions,
  ),
  args: []
)

test('Pane layout test',
  executable('test-nemo-pane-layout',
    [ 'test-nemo-pane-layout.c', '../src/nemo-pane-layout.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private ],
    c_args: nemo_definitions,
  ),
  args: []
)

test('Proportional paned test',
  executable('test-nemo-proportional-paned',
    [ 'test-nemo-proportional-paned.c', '../src/nemo-proportional-paned.c', '../src/nemo-pane-layout.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private ],
    c_args: nemo_definitions,
  ),
  args: []
)

test('Search group test',
  executable('test-nemo-search-group',
    [ 'test-nemo-search-group.c', '../src/nemo-list-model.c' ],
    include_directories: [ rootInclude, include_directories('../src') ],
    dependencies: [ gtk, nemo_private, egg, test_scratch ],
    c_args: nemo_definitions,
  ),
  args: []
)

test('Icon size test',
  executable('test-nemo-icon-size',
    [ 'test-nemo-icon-size.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private ],
    c_args: nemo_definitions,
  ),
  args: []
)

test('List expander test',
  executable('test-nemo-list-expander',
    [ 'test-nemo-list-expander.c', '../src/nemo-list-model.c' ],
    include_directories: [ rootInclude, include_directories('../src') ],
    dependencies: [ gtk, nemo_private, egg, test_scratch ],
    c_args: nemo_definitions,
  ),
  args: []
)

test('Tree folders test',
  executable('test-nemo-tree-folders',
    [ 'test-nemo-tree-folders.c', '../src/nemo-tree-sidebar-model.c' ],
    include_directories: [ rootInclude, include_directories('../src') ],
    dependencies: [ gtk, nemo_private, test_scratch ],
    c_args: nemo_definitions,
  ),
  args: []
)

test('Status bar name test',
  executable('test-nemo-status-name',
    [ 'test-nemo-status-name.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('Tab strip test',
  executable('test-nemo-notebook',
    [ 'test-nemo-notebook.c', '../src/nemo-notebook.c' ],
    include_directories: [ rootInclude, include_directories('../src') ],
    dependencies: [ gtk, nemo_private, test_scratch ],
    c_args: nemo_definitions,
  ),
  args: []
)

test('New process argv test',
  executable('test-nemo-new-process',
    [ 'test-nemo-new-process.c', '../src/nemo-new-process.c' ],
    include_directories: [ rootInclude, include_directories('../src') ],
    dependencies: [ gtk, nemo_private ],
    c_args: nemo_definitions,
  ),
  args: []
)

if not is_windows
  test('Clipboard drag collision test',
    executable('test-nemo-clipboard',
      [ 'test-nemo-clipboard.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )
endif

if not is_windows
  test('Desktop terminal setting test',
    executable('test-nemo-desktop-terminal',
      [ 'test-nemo-desktop-terminal.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )
endif

if not is_windows
  test('Runtime environment test',
    executable('test-nemo-runtime-env',
      [ 'test-nemo-runtime-env.c' ],
      include_directories: [ rootInclude, ],
      c_args: nemo_definitions,
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )
endif

if not is_windows
  test('Extensions list flag test',
    executable('test-nemo-extensions-list',
      [ 'test-nemo-extensions-list.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private ],
    ),
    args: [ nemo ],
  )
endif

if not is_windows
  test('Version with no display test',
    executable('test-nemo-cli-version',
      [ 'test-nemo-cli-version.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ glib ],
    ),
    args: [ nemo, meson.project_version(),
            meson.current_source_dir() / '..' / 'build-number.py', py.full_path() ],
  )

  test('Window under the pointer test',
    executable('test-nemo-window-at-point',
      [ 'test-nemo-window-at-point.c', '../src/nemo-window-at-point.c' ],
      include_directories: [ rootInclude, include_directories('../src') ],
      dependencies: [ gtk, x11, test_scratch ],
    ),
    args: []
  )

  test('Action programs in their own folder test',
    executable('test-nemo-action-exec',
      [ 'test-nemo-action-exec.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )

  test('Clean start test',
    executable('test-nemo-startup-clean',
      [ 'test-nemo-startup-clean.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ glib, gio, x11, test_scratch ],
    ),
    args: [ nemo ],
    timeout: 120
  )

  test('Instances test',
    executable('test-nemo-instances',
      [ 'test-nemo-instances.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: [ nemo ],
    timeout: 120
  )

  test('Tab move between processes test',
    executable('test-nemo-tab-move',
      [ 'test-nemo-tab-move.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, test_scratch ],
    ),
    args: [ nemo ],
    timeout: 120
  )
endif

test('Filename extension test',
  executable('test-nemo-filename-extension',
    [ 'test-nemo-filename-extension.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private ],
  ),
  args: []
)

test('Owner columns test',
  executable('test-nemo-owner-columns',
    [ 'test-nemo-owner-columns.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch, win_shell ],
  ),
  args: []
)

test('Uri under another test',
  executable('test-nemo-uri-under',
    [ 'test-nemo-uri-under.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private ],
  ),
  args: []
)

test('Path forms test',
  executable('test-nemo-path-forms',
    [ 'test-nemo-path-forms.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private ],
  ),
  args: []
)

test('Window title test',
  executable('test-nemo-window-title',
    [ 'test-nemo-window-title.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('Uptime test',
  executable('test-nemo-uptime',
    [ 'test-nemo-uptime.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private ],
  ),
  args: []
)

test('Command template test',
  executable('test-nemo-command-template',
    [ 'test-nemo-command-template.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('Archive options test',
  executable('test-nemo-archive',
    [ 'test-nemo-archive.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('Compress dialog height test',
  executable('test-nemo-archive-dialog',
    [ 'test-nemo-archive-dialog.c', '../src/nemo-archive-dialog.c' ],
    include_directories: [ rootInclude, include_directories('../src') ],
    dependencies: [ gtk, nemo_private, test_scratch ],
    c_args: nemo_definitions,
  ),
  args: []
)

test('Archive settings test',
  executable('test-nemo-archive-settings',
    [ 'test-nemo-archive-settings.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

# Reads the archives it wrote back with libarchive, so it needs that library.
test('Archive job test',
  executable('test-nemo-archive-job',
    [ 'test-archive-job.c', 'test.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ archive, gtk, nemo_private, test_scratch ],
    c_args: nemo_definitions,
  ),
  args: []
)

# Some hundreds of archives, so it gets longer than the default.
test('Archive option combinations',
  executable('test-nemo-archive-combos',
    [ 'test-nemo-archive-combos.c', 'test.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ archive, gtk, nemo_private, test_scratch ],
    c_args: nemo_definitions,
  ),
  args: [],
  timeout: 600,
)

test('Extract naming test',
  executable('test-nemo-extract',
    [ 'test-nemo-extract.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private ],
  ),
  args: []
)

test('Archive mount address test',
  executable('test-nemo-archive-mount',
    [ 'test-nemo-archive-mount.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private ],
  ),
  args: []
)

# Writes its own zip with libarchive, so it needs the library the app links.
test('Extract job test',
  executable('test-nemo-extract-job',
    [ 'test-extract-job.c', 'test.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ archive, gtk, nemo_private, test_scratch ],
    c_args: nemo_definitions,
  ),
  args: []
)

test('Data dirs test',
  executable('test-nemo-data-dirs',
    [ 'test-nemo-data-dirs.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private ],
  ),
  args: []
)

test('Path list text test',
  executable('test-nemo-path-list',
    [ 'test-nemo-path-list.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private ],
  ),
  args: []
)

test('Image folder test',
  executable('test-nemo-image-folder',
    [ 'test-nemo-image-folder.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('Photoshop reader test',
  executable('test-nemo-psd',
    [ 'test-nemo-psd.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('Camera raw reader test',
  executable('test-nemo-raw',
    [ 'test-nemo-raw.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

# Windows reads shortcuts through the shell and never builds this reader.
if not is_windows
  test('Windows shortcut reader test',
    executable('test-nemo-lnk',
      [ 'test-nemo-lnk.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )

  test('Folder shortcuts sort with folders test',
    executable('test-nemo-lnk-sort',
      [ 'test-nemo-lnk-sort.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )
endif

test('ImageMagick thumbnail test',
  executable('test-nemo-magick',
    [ 'test-nemo-magick.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('Thumbnail hold test',
  executable('test-nemo-thumbnail-hold',
    [ 'test-nemo-thumbnail-hold.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('Thumbnail memory test',
  executable('test-nemo-thumbnail-memory',
    [ 'test-nemo-thumbnail-memory.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('Thumbnail jobs test',
  executable('test-nemo-thumbnail-jobs',
    [ 'test-nemo-thumbnail-jobs.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('Thumbnail order test',
  executable('test-nemo-thumbnail-order',
    [ 'test-nemo-thumbnail-order.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('Menu paths test',
  executable('test-nemo-menu-paths',
    [ 'test-nemo-menu-paths.c' ] + gresources,
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private ],
  ),
  args: []
)

test('Deep counts test',
  executable('test-nemo-deep-counts',
    [ 'test-nemo-deep-counts.c' ],
    include_directories: [ rootInclude, ],
    dependencies: [ gtk, nemo_private, test_scratch ],
  ),
  args: []
)

test('Directory load clean test',
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
  test('Share check does no I/O', share_win32, args: [ 'share' ])
  test('Junction to a missing folder lists', share_win32, args: [ 'dangling' ])

  test('Permissions source test',
    executable('test-nemo-perm-source-win32',
      [ 'test-nemo-perm-source-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, win_shell, test_scratch ],
    ),
    args: []
  )
endif

if is_windows
  test('Metadata ready test',
    executable('test-nemo-metadata-ready-win32',
      [ 'test-nemo-metadata-ready-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: [],
    timeout: 300
  )
endif

test('Config schema drift test',
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

  test('Long path enumeration test',
    executable('test-nemo-dir-enum-win32',
      [ 'test-nemo-dir-enum-win32.c' ] + longPathManifest,
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )

  test('Application manifest test',
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
  test('First run test',
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
  test('Extension load test',
    executable('test-nemo-extension-load',
      [ 'test-nemo-extension-load.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, gmodule, nemo_extension ],
    ),
    args: [ test_extension_module ]
  )
endif

# The document converters, run on files written for the purpose.
test('Search helpers test',
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

if is_windows
  test('Windows Search test',
    executable('test-nemo-search-win32',
      [ 'test-nemo-search-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )

  test('File associations test',
    executable('test-nemo-associations-win32',
      [ 'test-nemo-associations-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )

  test('Program launch test',
    executable('test-nemo-launch-win32',
      [ 'test-nemo-launch-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )

  test('Shell icon test',
    executable('test-nemo-shell-icon-win32',
      [ 'test-nemo-shell-icon-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )

  test('Link type test',
    executable('test-nemo-link-info-win32',
      [ 'test-nemo-link-info-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )

  test('Clipboard win32 test',
    executable('test-nemo-clipboard-win32',
      [ 'test-nemo-clipboard-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )

  test('Drag source win32 test',
    executable('test-nemo-dnd-win32',
      [ 'test-nemo-dnd-win32.c' ],
      include_directories: [ rootInclude, ],
      dependencies: [ gtk, nemo_private, test_scratch ],
    ),
    args: []
  )
endif
