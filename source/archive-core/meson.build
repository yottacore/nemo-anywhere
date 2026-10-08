# The archive core. GLib and GIO only, with no GTK and nothing of the app, so
# it could leave for a project of its own. lint-c.bash holds it to that. See
# Modularity in project/design_docs/20260929-101432_compression.md.
arc_core_lib = static_library('arc-core',
  [ 'arc-host.c', 'arc-settings.c' ],
  dependencies: [ glib, gio ],
  c_args: [ '-DG_LOG_DOMAIN="Archive core"' ],
)

arc_core = declare_dependency(
  include_directories: include_directories('.'),
  link_with: [ arc_core_lib ],
  dependencies: [ glib, gio ],
)
