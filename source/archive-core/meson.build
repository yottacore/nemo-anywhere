# The archive core. GLib and GIO only, with no GTK and nothing of the app, so
# it could leave for a project of its own. lint-c.bash holds it to that. See
# Modularity in project/design_docs/20260929-101432_compression.md.
arc_core_sources = [ 'arc-entry.c', 'arc-host.c', 'arc-link-options.c', 'arc-mounts.c', 'arc-path-list.c', 'arc-scan.c', 'arc-settings.c' ]
if is_windows
  arc_core_sources += [ 'arc-entry-win32.c', 'arc-mounts-win32.c' ]
endif

arc_core_lib = static_library('arc-core',
  arc_core_sources,
  dependencies: [ glib, gio, gio_unix ],
  c_args: [ '-DG_LOG_DOMAIN="Archive core"' ],
)

arc_core = declare_dependency(
  include_directories: include_directories('.'),
  link_with: [ arc_core_lib ],
  dependencies: [ glib, gio, gio_unix ],
)
