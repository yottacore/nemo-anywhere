# Unused

Code nothing builds, kept for reference. Each file sits at the path it had under `source/`. The C lint and the static analyzers skip this folder, and the analyzer stage fails if a build ever compiles a file from it.

- `libnemo-private/nemo-search-engine-tracker.c`: upstream's search through the Tracker indexer, once the `-Dtracker` option. Search uses the built-in engine, or the Windows Search index on Windows.

- `src/nemo-empty-view.c`: upstream's blank view for debugging the view code, once the `-Dempty_view` option.

- `test/test-nemo-dbus-manager.c`: tested the file operations D-Bus interface, which is gone. Only a window may start a trash, delete or move.

- `test/test-nemo-thumbnail-prune.c`: tested the sweep of the shared thumbnail cache, which the file cache replaced.

- `test/test-nemo-session-bus-win32.c`: tested the session bus stand-in on Windows. Nothing starts a bus there now.
