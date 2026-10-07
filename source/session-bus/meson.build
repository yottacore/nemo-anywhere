# Shipped as gdbus.exe beside the gio dll, where GLib looks for its session
# bus. See the comment at the top of the source.
session_bus_exe = executable('gdbus',
  'nemo-session-bus-win32.c',
  dependencies: [ gio, glib ],
  install: false
)
