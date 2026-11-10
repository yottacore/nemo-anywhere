# ../data/icons is on the search list so the manifest can name nemo's own art
# by its own repo-relative path and alias it into the resource under a hicolor
# layout - see the note in nemo.gresource.xml.
gresources = gnome.compile_resources(
  'nemo-resources', 'nemo.gresource.xml',
  source_dir: ['.', '../data/icons'],
  c_name: 'nemo',
  install_header: false,
  install: false
)

# The bundled theme set, on the targets that bundle one. Loose theme files cost
# real launch time in the packed Windows exe - about 2.8 ms each, and the icon
# sets alone are a couple of thousand - so they ride inside the binary instead.
# Regenerate the manifest with cicd/utility/gen-theme-resources.py after
# changing anything under vendor/icons, vendor/themes or assets/icons.
if bundledThemes == 'true'
  themeGresources = gnome.compile_resources(
    'nemo-theme-resources', 'nemo-themes.gresource.xml',
    source_dir: '../..',
    c_name: 'nemo_themes',
    install_header: false,
    install: false
  )
else
  themeGresources = []
endif

# Only read by the layout editor, which isn't built on Windows.
if not is_windows
  layout_editor_gresources = gnome.compile_resources(
    'nemo-action-layout-editor-resources', 'nemo-action-layout-editor.gresource.xml',
    source_dir: '.',
    gresource_bundle: true,
    install: true,
    install_dir: nemoDataPath
  )
endif
