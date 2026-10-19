egg_lib = static_library('egg',
  'eggtreemultidnd.c',
  dependencies: [
    gtk,
  ],
  # Vendored, so it builds with -Werror like the rest but keeps upstream's habits:
  c_args: [
    # GTK callbacks that ignore some of their arguments
    '-Wno-unused-parameter',
    # GTypeInfo filled in by position, trailing fields left to zero
    '-Wno-missing-field-initializers',
    # gtk_tree_row_reference_free as a GFunc; the extra argument is ignored on every ABI built here
    '-Wno-cast-function-type',
  ],
  install: false,
)

egg = declare_dependency(
  include_directories: include_directories('..'),
  link_with: egg_lib,
  dependencies: [
    gtk,
  ],
)
