# Goes in every Windows bundle as gdbus.exe, beside the gio dll, where GLib
# looks for its session bus. See the comment at the top of the source.
session_bus_exe = executable('gdbus',
  'nemo-session-bus-win32.c',
  dependencies: [ gio, glib ],
  # mingw's own startup objects bring DWARF even to a release build, and
  # nothing strips the bundle's bin folder.
  link_args: [ '-Wl,--strip-debug' ],
  install: false
)
