
nemo_private_sources = [
  'nemo-action-manager.c',
  'nemo-appearance.c',
  'nemo-action.c',
  'nemo-archive.c',
  'nemo-archive-host.c',
  'nemo-bookmark.c',
  'nemo-cell-renderer-disk.c',
  'nemo-centered-placement-grid.c',
  'nemo-clipboard-monitor.c',
  'nemo-clipboard.c',
  'nemo-column-chooser.c',
  'nemo-column-utilities.c',
  'nemo-command-template.c',
  'nemo-crash.c',
  'nemo-debug.c',
  'nemo-default-file-icon.c',
  'nemo-delete-guard.c',
  'nemo-delete-testguard.c',
  'nemo-desktop-utils.c',
  'nemo-directory-async.c',
  'nemo-desktop-thumbnail.c',
  'nemo-directory.c',
  'nemo-dnd.c',
  'nemo-entry.c',
  'nemo-extract.c',
  'nemo-extract-conflict-dialog.c',
  'nemo-favorite-vfs-file.c',
  'nemo-favorite-vfs-file-enumerator.c',
  'nemo-favorite-vfs-file-monitor.c',
  'nemo-favorites.c',
  'nemo-file-changes-queue.c',
  'nemo-file-conflict-dialog.c',
  'nemo-file-dnd.c',
  'nemo-file-operations.c',
  'nemo-file-queue.c',
  'nemo-file-undo-manager.c',
  'nemo-file-undo-operations.c',
  'nemo-file-utilities.c',
  'nemo-file.c',
  'nemo-global-preferences.c',
  'nemo-icon-canvas-item.c',
  'nemo-icon-container.c',
  'nemo-icon-dnd.c',
  'nemo-icon-info.c',
  'nemo-job-queue.c',
  'nemo-lib-self-check-functions.c',
  'nemo-link.c',
  'nemo-merged-directory.c',
  'nemo-config.c',
  'nemo-desktop-settings.c',
  'nemo-dir-enum.c',
  'nemo-metadata.c',
  'nemo-metadata-store.c',
  'nemo-folder-settings.c',
  'nemo-network-win32.c',
  'nemo-security-win32.c',
  'nemo-share.c',
  'nemo-link-copy.c',
  'nemo-link-edit.c',
  'nemo-link-win32.c',
  'nemo-shortcut-win32.c',
  'nemo-trash-win32.c',
  'nemo-win32-appearance.c',
  'nemo-cache-db.c',
  'nemo-cache-db-prune.c',
  'nemo-file-digest.c',
  'nemo-file-xattr.c',
  'nemo-image-folders.c',
  'nemo-instances.c',
  'nemo-office.c',
  'nemo-psd.c',
  'nemo-raw.c',
  'nemo-magick.c',
  'nemo-thumbnail-pixbuf-utils.c',
  'nemo-mime-application-chooser.c',
  'nemo-module.c',
  'nemo-monitor.c',
  'nemo-placement-grid.c',
  'nemo-places-tree-view.c',
  'nemo-program-choosing.c',
  'nemo-progress-info-manager.c',
  'nemo-progress-info.c',
  'nemo-query.c',
  'nemo-row-hover.c',
  'nemo-recent.c',
  'nemo-search-directory-file.c',
  'nemo-search-directory.c',
  'nemo-search-engine-advanced.c',
  'nemo-search-engine.c',
  'nemo-selection-canvas-item.c',
  'nemo-separator-action.c',
  'nemo-signaller.c',
  'nemo-small-copy.c',
  'nemo-thumbnail-memory.c',
  'nemo-thumbnails.c',
  'nemo-tool-run.c',
  'nemo-trash-monitor.c',
  'nemo-tree-view-drag-dest.c',
  'nemo-ui-scale.c',
  'nemo-ui-utilities.c',
  'nemo-undo-manager.c',
  'nemo-undo-signal-handlers.c',
  'nemo-undo-transaction.c',
  'nemo-undo.c',
  'nemo-user-text.c',
  'nemo-vfs-directory.c',
  'nemo-vfs-file.c',
  'nemo-widget-action.c',
  'nemo-widget-menu-item.c',
]

nemo_private_deps = [
  arc_core, archive, blake3, eel, execinfo, gail, gio_unix, glib, gmodule, gtk, json, math,
  nemo_extension, sqlite, win_shell, x11
]

if libexif_enabled
  nemo_private_deps += libexif
endif

if tracker_enabled
  nemo_private_sources += 'nemo-search-engine-tracker.c'
  nemo_private_deps += tracker_sparql
endif

if is_windows
  nemo_private_sources += 'nemo-delete-guard-win32.c'
  nemo_private_sources += 'nemo-xattr-win32.c'
  nemo_private_sources += 'nemo-search-engine-win32.c'
  nemo_private_sources += 'nemo-associations-win32.c'
  nemo_private_sources += 'nemo-launch-win32.c'
  nemo_private_deps += dependency('gio-windows-2.0', version: glib_version)
  nemo_private_sources += 'nemo-clipboard-win32.c'
  nemo_private_sources += 'nemo-dnd-win32.c'
  nemo_private_sources += 'nemo-shell-icon-win32.c'
  nemo_private_sources += 'nemo-drives-win32.c'
  nemo_private_sources += 'nemo-instances-win32.c'
endif
nemo_private_sources += 'nemo-lnk.c'

if libselinux_enabled
  nemo_private_deps += libselinux
endif

nemo_private_lib = static_library('nemo-private',
  nemo_private_sources,
  dependencies: nemo_private_deps,
  include_directories: [ rootInclude, shclInclude, ],
  c_args: nemo_definitions,
)

nemo_private = declare_dependency(
  include_directories: include_directories('.'),
  link_with: [ nemo_private_lib ],
  dependencies: nemo_private_deps,
)

# Settings live in a plain SHCL file at runtime; the schema below is shipped
# only so `shcl check --schema` can validate a hand-edited config.
install_data('../data/nemo-anywhere.schema.shcl',
    install_dir: join_paths(get_option('datadir'), appSlug)
)
