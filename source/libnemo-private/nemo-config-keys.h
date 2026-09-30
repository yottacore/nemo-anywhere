/* Generated from the old gschema - the settings table.
   Edit this file directly; the gschema it came from is gone.

   Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu) */

#ifndef NEMO_CONFIG_KEYS_H
#define NEMO_CONFIG_KEYS_H

#include "nemo-config.h"
#include "nemo-archive-commands.h"

static const NemoConfigEnumValue enum_ActivationChoice[] = {
	{ "launch", 0 },
	{ "display", 1 },
	{ "ask", 2 },
	{ NULL, 0 }
};

static const NemoConfigEnumValue enum_AppearanceMode[] = {
	{ "system", 0 },
	{ "light", 1 },
	{ "dark", 2 },
	{ NULL, 0 }
};

static const NemoConfigEnumValue enum_ClickPolicy[] = {
	{ "single", 0 },
	{ "double", 1 },
	{ NULL, 0 }
};

static const NemoConfigEnumValue enum_DateFormat[] = {
	{ "locale", 0 },
	{ "iso", 1 },
	{ "informal", 2 },
	{ NULL, 0 }
};

static const NemoConfigEnumValue enum_FolderView[] = {
	{ "icon-view", 0 },
	{ "compact-view", 1 },
	{ "list-view", 2 },
	{ NULL, 0 }
};


static const NemoConfigEnumValue enum_PathSeparator[] = {
	{ "backslash", 0 },
	{ "slash", 1 },
	{ NULL, 0 }
};

static const NemoConfigEnumValue enum_SizePrefixes[] = {
	{ "base-10", 0 },
	{ "base-10-full", 1 },
	{ "base-2", 2 },
	{ "base-2-full", 3 },
	{ NULL, 0 }
};

static const NemoConfigEnumValue enum_SortOrder[] = {
	{ "manually", 0 },
	{ "name", 1 },
	{ "size", 2 },
	{ "type", 3 },
	{ "detailed_type", 4 },
	{ "mtime", 5 },
	{ "atime", 6 },
	{ "trash-time", 7 },
	{ NULL, 0 }
};

static const NemoConfigEnumValue enum_SpeedTradeoff[] = {
	{ "always", 0 },
	{ "local-only", 1 },
	{ "never", 2 },
	{ NULL, 0 }
};

static const NemoConfigEnumValue enum_TabPosition[] = {
	{ "after-current-tab", 0 },
	{ "end", 1 },
	{ NULL, 0 }
};

static const char *const deflist__favorites[] = { NULL };
static const char *const deflist__favorites_root_metadata[] = { NULL };
static const char *const deflist_icon_view_captions[] = { "none", "size", "date_modified", NULL };
static const char *const deflist_icon_view_text_ellipsis_limit[] = { "3", NULL };
#ifdef G_OS_WIN32
static const char *const deflist_list_view_default_column_order[] = { "name", "where", "size", "extension", "type", "date_modified", "owner", "owner_name", "owner_and_name", "permissions_source", NULL };
static const char *const deflist_list_view_default_visible_columns[] = { "name", "size", "extension", "type", "date_modified", "owner", NULL };
#else
static const char *const deflist_list_view_default_column_order[] = { "name", "where", "size", "extension", "type", "date_modified", "owner", "owner_name", "owner_and_name", "group", "permissions", NULL };
static const char *const deflist_list_view_default_visible_columns[] = { "name", "size", "extension", "type", "date_modified", "owner", "group", "permissions", NULL };
#endif
/* The actions that only do anything under Cinnamon: they call cinnamon-settings,
 * the desktop editor, or org.Cinnamon over the bus. Off unless someone turns them
 * on, and still listed in Preferences > Actions so a Cinnamon user can. */
static const char *const deflist_plugins_disabled_actions[] = {
	"90_new-launcher.nemo_action",
	"90_new-workspace.nemo_action",
	"91_delete-workspace.nemo_action",
	"92_show-expo.nemo_action",
	"add-desklets.nemo_action",
	"change-background.nemo_action",
	"set-as-background.nemo_action",
	"set-resolution.nemo_action",
	NULL
};
static const char *const deflist_plugins_disabled_extensions[] = { NULL };
static const char *const deflist_plugins_disabled_scripts[] = { NULL };
static const char *const deflist_preferences_image_viewers_with_external_sort[] = { "xviewer", "feh", "sxiv", NULL };
static const char *const deflist_search_disabled_search_helpers[] = { NULL };
static const char *const deflist_search_search_skip_folders[] = { "/dev", "/proc", "/sys", "dosdevices", ".git", NULL };
static const char *const deflist_search_search_visible_columns[] = { NULL };
static const char *const deflist_thumbnailers_disable[] = { NULL };
static const char *const deflist_windows_associations[] = { NULL };
/* "Open in Terminal": first one found on PATH wins. */
static const char *const deflist_windows_terminal_candidates[] = { "wt.exe", "pwsh.exe", "powershell.exe", "cmd.exe", NULL };

static const NemoConfigKey nemo_config_keys[] = {
	{ "", "favorites", NEMO_CONFIG_STRING_LIST, NULL, deflist__favorites, NULL, NULL, NEMO_CONFIG_KEY_STATE },
	{ "", "favorites-root-metadata", NEMO_CONFIG_STRING_LIST, NULL, deflist__favorites_root_metadata, NULL, NULL, NEMO_CONFIG_KEY_STATE },
	{ "appearance", "gtk-theme", NEMO_CONFIG_STRING, "", NULL, NULL, "Widget theme name, or empty for the platform's own" },
	{ "appearance", "icon-theme", NEMO_CONFIG_STRING, "", NULL, NULL, "Icon theme name, or empty for the platform's own" },
	{ "appearance", "mode", NEMO_CONFIG_ENUM, "system", NULL, enum_AppearanceMode, NULL },
	{ NEMO_ARCHIVE_COMMANDS_GROUP, NEMO_ARCHIVE_COMMAND_KEY_7Z, NEMO_CONFIG_STRING, NEMO_ARCHIVE_COMMAND_7Z_DEFAULT, NULL, NULL, "Command line 7-Zip is run with to create an archive. Each {{NAME}} stands for something the Compress dialog fills in; drop one and that option stops working. Base flags only, which the app adds to or changes as a job needs; edit only to suit a different version of the program. Empty the line for the built-in one." },
	{ NEMO_ARCHIVE_COMMANDS_GROUP, NEMO_ARCHIVE_COMMAND_KEY_RAR, NEMO_CONFIG_STRING, NEMO_ARCHIVE_COMMAND_RAR_DEFAULT, NULL, NULL, "Command line rar is run with to create an archive. Same placeholders as the 7-Zip line, plus {{DEDUPE}}, {{RECOVERY}} and {{LOCK}}, which only rar has. Base flags only, which the app adds to or changes as a job needs; edit only to suit a different version of the program." },
	{ NEMO_ARCHIVE_COMMANDS_GROUP, NEMO_EXTRACT_COMMAND_KEY_7Z, NEMO_CONFIG_STRING, NEMO_EXTRACT_COMMAND_7Z_DEFAULT, NULL, NULL, "Command line 7-Zip is run with to unpack an archive. Base flags only, which the app adds to or changes as a job needs; edit only to suit a different version of the program. Empty the line for the built-in one." },
	{ NEMO_ARCHIVE_COMMANDS_GROUP, NEMO_EXTRACT_COMMAND_KEY_RAR, NEMO_CONFIG_STRING, NEMO_EXTRACT_COMMAND_RAR_DEFAULT, NULL, NULL, "Command line rar or unrar is run with to unpack an archive. Base flags only, which the app adds to or changes as a job needs; edit only to suit a different version of the program. Empty the line for the built-in one." },
	{ NEMO_ARCHIVE_COMMANDS_GROUP, NEMO_ARCHIVE_STATE_KEY_EACH, NEMO_CONFIG_BOOL, "false", NULL, NULL, "What the Compress dialog starts from next time", NEMO_CONFIG_KEY_STATE },
	{ NEMO_ARCHIVE_COMMANDS_GROUP, NEMO_ARCHIVE_STATE_KEY_ENCRYPT_NAMES, NEMO_CONFIG_BOOL, "false", NULL, NULL, "What the Compress dialog starts from next time", NEMO_CONFIG_KEY_STATE },
	{ NEMO_ARCHIVE_COMMANDS_GROUP, NEMO_ARCHIVE_STATE_KEY_DEDUPE, NEMO_CONFIG_BOOL, "false", NULL, NULL, "What the Compress dialog starts from next time", NEMO_CONFIG_KEY_STATE },
	{ NEMO_ARCHIVE_COMMANDS_GROUP, NEMO_ARCHIVE_STATE_KEY_FOLLOW_LINKS, NEMO_CONFIG_BOOL, "false", NULL, NULL, "What the Compress dialog starts from next time", NEMO_CONFIG_KEY_STATE },
	{ NEMO_ARCHIVE_COMMANDS_GROUP, NEMO_ARCHIVE_STATE_KEY_FORMAT, NEMO_CONFIG_STRING, NEMO_ARCHIVE_STATE_DEFAULT_FORMAT, NULL, NULL, "What the Compress dialog starts from next time", NEMO_CONFIG_KEY_STATE },
	{ NEMO_ARCHIVE_COMMANDS_GROUP, NEMO_ARCHIVE_STATE_KEY_LEVEL, NEMO_CONFIG_INT, "5", NULL, NULL, "What the Compress dialog starts from next time", NEMO_CONFIG_KEY_STATE },
	{ NEMO_ARCHIVE_COMMANDS_GROUP, NEMO_ARCHIVE_STATE_KEY_LOCK, NEMO_CONFIG_BOOL, "false", NULL, NULL, "What the Compress dialog starts from next time", NEMO_CONFIG_KEY_STATE },
	{ NEMO_ARCHIVE_COMMANDS_GROUP, NEMO_ARCHIVE_STATE_KEY_RECOVERY, NEMO_CONFIG_BOOL, "true", NULL, NULL, "What the Compress dialog starts from next time", NEMO_CONFIG_KEY_STATE },
	{ NEMO_ARCHIVE_COMMANDS_GROUP, NEMO_ARCHIVE_STATE_KEY_SOLID, NEMO_CONFIG_BOOL, "false", NULL, NULL, "What the Compress dialog starts from next time", NEMO_CONFIG_KEY_STATE },
	{ NEMO_ARCHIVE_COMMANDS_GROUP, NEMO_ARCHIVE_STATE_KEY_SPLIT, NEMO_CONFIG_BOOL, "false", NULL, NULL, "What the Compress dialog starts from next time", NEMO_CONFIG_KEY_STATE },
	{ NEMO_ARCHIVE_COMMANDS_GROUP, NEMO_ARCHIVE_STATE_KEY_SPLIT_SIZE, NEMO_CONFIG_STRING, NEMO_ARCHIVE_STATE_DEFAULT_SPLIT_SIZE, NULL, NULL, "What the Compress dialog starts from next time", NEMO_CONFIG_KEY_STATE },
	{ NEMO_ARCHIVE_COMMANDS_GROUP, NEMO_ARCHIVE_STATE_KEY_STORE_LINKS, NEMO_CONFIG_BOOL, "true", NULL, NULL, "What the Compress dialog starts from next time", NEMO_CONFIG_KEY_STATE },
	{ "compact-view", "all-columns-have-same-width", NEMO_CONFIG_BOOL, "false", NULL, NULL, NULL },
	{ "compact-view", "default-icon-size", NEMO_CONFIG_INT, "100", NULL, NULL, "Icon size as a per cent of the standard 64 pixels" },
	{ "debug", "testguard-all-deletes", NEMO_CONFIG_BOOL, "true", NULL, NULL, "Ask before every trash and delete, naming the code that asked. Scaffolding for finding a removal nothing accounts for" },
	{ "file-cache", "max-size-gib", NEMO_CONFIG_FLOAT, "2", NULL, NULL, "Largest the file cache may get, in GiB. The least recently drawn thumbnails go first. 0 for no limit" },
	{ "file-cache", "memory-gib", NEMO_CONFIG_FLOAT, "1", NULL, NULL, "Most memory the thumbnails held ready to draw may take, in GiB. Past it only those near the view are held. 0 holds only what is on screen" },
	{ "file-cache", "max-age-days", NEMO_CONFIG_INT, "180", NULL, NULL, "Drop a thumbnail not drawn for this many days. 0 to keep them however old" },
	{ "file-cache", "drop-missing", NEMO_CONFIG_BOOL, "true", NULL, NULL, "Forget a local file that is gone from a folder that is still there. A folder that is missing entirely is left alone, since it may be on a drive that is not plugged in" },
	{ "file-cache", "save-checksum", NEMO_CONFIG_BOOL, "false", NULL, NULL, "Write each checksum onto the file it belongs to, as the extended attributes user.blake3.b64u, .bytes and .mtime. On Windows these are alternate data streams, which only local NTFS and ReFS drives keep" },
	{ "file-cache", "prune-min-hours", NEMO_CONFIG_INT, "4", NULL, NULL, "Soonest the file cache is cleaned up again. The next time is picked at random between this and prune-max-hours" },
	{ "file-cache", "prune-max-hours", NEMO_CONFIG_INT, "24", NULL, NULL, "Latest the file cache is cleaned up again" },
	{ "file-cache", "prune-idle-minutes", NEMO_CONFIG_INT, "5", NULL, NULL, "Wait until no thumbnails have been drawn for this many minutes before cleaning up. 0 to not wait" },
	{ "interface", "clock-use-24h", NEMO_CONFIG_BOOL, "true", NULL, NULL, NULL },
	{ "media-handling", "automount", NEMO_CONFIG_BOOL, "false", NULL, NULL, NULL },
	{ "media-handling", "automount-open", NEMO_CONFIG_BOOL, "false", NULL, NULL, "Open a window on media as soon as it is mounted" },
	{ "performance", "clone-min-kib", NEMO_CONFIG_INT, "64", NULL, NULL, "Files smaller than this are copied in full rather than cloned, on file systems that can clone. 0 to always clone, 1024 at most" },
	{ "performance", "cpu-percent", NEMO_CONFIG_INT, "50", NULL, NULL, "Share of the machine's CPU cores compression may use" },
	{ "privacy", "remember-recent-files", NEMO_CONFIG_BOOL, "true", NULL, NULL, NULL },
	{ "terminal", "exec", NEMO_CONFIG_STRING, "", NULL, NULL, "Terminal used for Open in Terminal, or empty to let the system pick" },
	{ "terminal", "exec-arg", NEMO_CONFIG_STRING, "-e", NULL, NULL, "Argument that terminal takes before a command" },
	{ "desktop", "show-desktop-icons", NEMO_CONFIG_BOOL, "true", NULL, NULL, NULL },
	{ "desktop", "text-ellipsis-limit", NEMO_CONFIG_INT, "2", NULL, NULL, "Lines of a name under a desktop icon before it is cut short" },
	{ "desktop", "use-desktop-grid", NEMO_CONFIG_BOOL, "true", NULL, NULL, NULL },
	{ "icon-view", "captions", NEMO_CONFIG_STRING_LIST, NULL, deflist_icon_view_captions, NULL, "Extra details shown under an icon" },
	{ "icon-view", "default-icon-size", NEMO_CONFIG_INT, "100", NULL, NULL, "Icon size as a per cent of the standard 64 pixels" },
	{ "icon-view", "default-image-icon-size", NEMO_CONFIG_INT, "500", NULL, NULL, "Icon size for a folder that is mostly images, as a per cent of the standard 64 pixels" },
	{ "icon-view", "image-folder-min-images", NEMO_CONFIG_INT, "2", NULL, NULL, "Fewest images a folder needs to count as mostly images" },
	{ "icon-view", "image-folder-min-percent", NEMO_CONFIG_INT, "50", NULL, NULL, "Share of a folder's files, sub-folders not counted, that must be images for it to count as mostly images" },
	{ "icon-view", "image-folder-switch", NEMO_CONFIG_BOOL, "true", NULL, NULL, "Show a folder that is mostly images in icon view, unless it has a view of its own" },
	{ "icon-view", "labels-beside-icons", NEMO_CONFIG_BOOL, "false", NULL, NULL, NULL },
	{ "icon-view", "text-ellipsis-limit", NEMO_CONFIG_STRING_LIST, NULL, deflist_icon_view_text_ellipsis_limit, NULL, "Lines of a name under an icon before it is cut short" },
	{ "icon-view", "thumbnail-size", NEMO_CONFIG_INT, "64", NULL, NULL, "Thumbnail size in pixels" },
	{ "list-view", "column-fit-percent", NEMO_CONFIG_INT, "90", NULL, NULL, "Share of the values Name and Location still show when the row is short of room, and the share other variable columns show by default. Name counts every file; the others count each distinct value once." },
	{ "list-view", "default-column-order", NEMO_CONFIG_STRING_LIST, NULL, deflist_list_view_default_column_order, NULL, NULL },
	{ "list-view", "default-visible-columns", NEMO_CONFIG_STRING_LIST, NULL, deflist_list_view_default_visible_columns, NULL, NULL },
	{ "list-view", "default-icon-size", NEMO_CONFIG_INT, "75", NULL, NULL, "Icon size as a per cent of the standard 64 pixels" },
	{ "list-view", "enable-folder-expansion", NEMO_CONFIG_BOOL, "true", NULL, NULL, "Let a folder be expanded in place to show what is in it" },
	{ "list-view", "row-shading", NEMO_CONFIG_BOOL, "false", NULL, NULL, "Shade every other row" },
	{ "list-view", "row-shading-color", NEMO_CONFIG_STRING, "", NULL, NULL, "Color of the shaded rows, in any form CSS takes. Empty uses the theme's nemo_row_shading color, or a faint tint of the text color if it has none" },
	{ "list-view", "row-hover-color", NEMO_CONFIG_STRING, "", NULL, NULL, "Color of the row under the pointer, in any form CSS takes. Empty uses the theme's nemo_row_hover color, or a faint tint of the selection color if it has none. transparent turns it off" },
	{ "plugins", "disabled-actions", NEMO_CONFIG_STRING_LIST, NULL, deflist_plugins_disabled_actions, NULL, "Action files that are switched off. The shipped Cinnamon-only ones start here" },
	{ "plugins", "disabled-extensions", NEMO_CONFIG_STRING_LIST, NULL, deflist_plugins_disabled_extensions, NULL, NULL },
	{ "plugins", "disabled-scripts", NEMO_CONFIG_STRING_LIST, NULL, deflist_plugins_disabled_scripts, NULL, NULL },
	{ "preferences", "always-show-tabs", NEMO_CONFIG_BOOL, "false", NULL, NULL, "Show the tab strip even with only one tab" },
	{ "preferences", "always-use-browser", NEMO_CONFIG_BOOL, "true", NULL, NULL, "Every window is a browser window, with a toolbar and history" },
	{ "preferences", "bulk-rename-tool", NEMO_CONFIG_STRING, "", NULL, NULL, "Program run for a bulk rename" },
	{ "preferences", "click-double-parent-folder", NEMO_CONFIG_BOOL, "false", NULL, NULL, "Double-clicking empty space goes to the parent folder" },
	{ "preferences", "click-policy", NEMO_CONFIG_ENUM, "double", NULL, enum_ClickPolicy, "One click or two to open a file" },
	{ "preferences", "close-device-view-on-device-eject", NEMO_CONFIG_BOOL, "false", NULL, NULL, "Close the window when its device is ejected, rather than going home" },
	{ "preferences", "confirm-drag-copy", NEMO_CONFIG_BOOL, "false", NULL, NULL, "Ask before a drop copies files" },
	{ "preferences", "confirm-drag-move", NEMO_CONFIG_BOOL, "true", NULL, NULL, "Ask before a drop moves files" },
	{ "preferences", "confirm-move-to-trash", NEMO_CONFIG_BOOL, "true", NULL, NULL, NULL },
	{ "preferences", "confirm-trash", NEMO_CONFIG_BOOL, "true", NULL, NULL, "Ask before deleting outright or emptying the Trash" },
	{ "preferences", "confirm-many-items", NEMO_CONFIG_INT, "20", NULL, NULL, "Ask before trashing or deleting this many items at once, whatever the two above say (0 for never)" },
	{ "preferences", "date-format", NEMO_CONFIG_ENUM, "iso", NULL, enum_DateFormat, NULL },
	{ "preferences", "default-folder-viewer", NEMO_CONFIG_ENUM, "list-view", NULL, enum_FolderView, NULL },
	{ "preferences", "default-sort-in-reverse-order", NEMO_CONFIG_BOOL, "false", NULL, NULL, NULL },
	{ "preferences", "default-sort-order", NEMO_CONFIG_ENUM, "name", NULL, enum_SortOrder, NULL },
	{ "preferences", "deferred-attribute-preload-limit", NEMO_CONFIG_INT, "150", NULL, NULL, "How many files a folder reads extra details for up front" },
	{ "preferences", "desktop-is-home-dir", NEMO_CONFIG_BOOL, "false", NULL, NULL, NULL },
	{ "preferences", "detect-content", NEMO_CONFIG_BOOL, "false", NULL, NULL, "Look inside mounted media to suggest a program for it" },
	{ "preferences", "disable-menu-warning", NEMO_CONFIG_BOOL, "false", NULL, NULL, "Skip the explanation shown when the menu bar is turned off" },
	{ "preferences", "enable-delete", NEMO_CONFIG_BOOL, "true", NULL, NULL, "Offer Delete, which skips the Trash" },
	{ "preferences", "enable-mime-actions-make-executable", NEMO_CONFIG_BOOL, "false", NULL, NULL, "Offer to make an unknown file executable and run it" },
	{ "preferences", "executable-text-activation", NEMO_CONFIG_ENUM, "ask", NULL, enum_ActivationChoice, "What opening an executable text file does" },
	{ "preferences", "expand-row-on-dnd-dwell", NEMO_CONFIG_BOOL, "true", NULL, NULL, "Hovering a folder during a drag opens it" },
	{ "preferences", "image-viewers-with-external-sort", NEMO_CONFIG_STRING_LIST, NULL, deflist_preferences_image_viewers_with_external_sort, NULL, "Image viewers that are told the current sort order" },
	{ "preferences", "inherit-view-settings", NEMO_CONFIG_BOOL, "true", NULL, NULL, "A folder with nothing saved uses the settings of the nearest parent folder that has some" },
	{ "preferences", "inherit-show-thumbnails", NEMO_CONFIG_BOOL, "true", NULL, NULL, "A folder shows thumbnails if its parent did" },
	{ "preferences", "last-server-connect-method", NEMO_CONFIG_INT, "2", NULL, NULL, NULL, NEMO_CONFIG_KEY_STATE },
	{ "preferences", "mouse-back-button", NEMO_CONFIG_INT, "8", NULL, NULL, "Mouse button that goes back" },
	{ "preferences", "mouse-forward-button", NEMO_CONFIG_INT, "9", NULL, NULL, "Mouse button that goes forward" },
	{ "preferences", "mouse-use-extra-buttons", NEMO_CONFIG_BOOL, "true", NULL, NULL, "Use the extra mouse buttons for back and forward" },
	{ "preferences", "never-queue-file-ops", NEMO_CONFIG_BOOL, "false", NULL, NULL, "Start every file operation at once rather than queueing them" },
	{ "preferences", "remember-folder-settings", NEMO_CONFIG_BOOL, "false", NULL, NULL, "Each folder keeps its own view, zoom, sort and columns" },
	{ "preferences", "rename-selects-whole-name", NEMO_CONFIG_BOOL, "true", NULL, NULL, "Renaming selects the whole name, extension included" },
	{ "preferences", "quick-renames-with-pause-in-between", NEMO_CONFIG_BOOL, "false", NULL, NULL, "Two slow clicks on a name start a rename" },
	{ "preferences", "show-advanced-permissions", NEMO_CONFIG_BOOL, "false", NULL, NULL, NULL },
	{ "preferences", "show-bookmarks-in-to-menus", NEMO_CONFIG_BOOL, "true", NULL, NULL, "List bookmarks in the Move To and Copy To menus" },
	{ "preferences", "show-compact-view-icon-toolbar", NEMO_CONFIG_BOOL, "true", NULL, NULL, NULL },
	{ "preferences", "show-computer-icon-toolbar", NEMO_CONFIG_BOOL, "false", NULL, NULL, NULL },
	{ "preferences", "show-directory-item-counts", NEMO_CONFIG_ENUM, "local-only", NULL, enum_SpeedTradeoff, "When to count what is in a folder" },
	{ "preferences", "show-edit-icon-toolbar", NEMO_CONFIG_BOOL, "true", NULL, NULL, "Show the button that swaps the path buttons for a typed location" },
	{ "preferences", "show-full-path-titles", NEMO_CONFIG_BOOL, "false", NULL, NULL, "Show the whole path in the title bar and on tabs" },
	{ "preferences", "show-hidden-files", NEMO_CONFIG_BOOL, "false", NULL, NULL, NULL },
	{ "preferences", "show-home-icon-toolbar", NEMO_CONFIG_BOOL, "false", NULL, NULL, NULL },
	{ "preferences", "show-icon-view-icon-toolbar", NEMO_CONFIG_BOOL, "true", NULL, NULL, NULL },
	{ "preferences", "show-image-thumbnails", NEMO_CONFIG_ENUM, "local-only", NULL, enum_SpeedTradeoff, "When to draw thumbnails" },
	{ "preferences", "show-list-view-icon-toolbar", NEMO_CONFIG_BOOL, "true", NULL, NULL, NULL },
	{ "preferences", "show-location-entry", NEMO_CONFIG_BOOL, "false", NULL, NULL, "Start with a typed location instead of path buttons" },
	{ "preferences", "show-new-folder-icon-toolbar", NEMO_CONFIG_BOOL, "false", NULL, NULL, NULL },
	{ "preferences", "show-next-icon-toolbar", NEMO_CONFIG_BOOL, "true", NULL, NULL, NULL },
	{ "preferences", "show-open-in-terminal-toolbar", NEMO_CONFIG_BOOL, "false", NULL, NULL, NULL },
	{ "preferences", "show-places-in-to-menus", NEMO_CONFIG_BOOL, "true", NULL, NULL, "List places in the Move To and Copy To menus" },
	{ "preferences", "show-previous-icon-toolbar", NEMO_CONFIG_BOOL, "true", NULL, NULL, NULL },
	{ "preferences", "show-reload-icon-toolbar", NEMO_CONFIG_BOOL, "false", NULL, NULL, NULL },
	{ "preferences", "show-root-warning", NEMO_CONFIG_BOOL, "true", NULL, NULL, "Warn before opening a window as root" },
	{ "preferences", "show-search-icon-toolbar", NEMO_CONFIG_BOOL, "true", NULL, NULL, NULL },
	{ "preferences", "show-shortcut-extension", NEMO_CONFIG_BOOL, "false", NULL, NULL, "Count a shortcut's .lnk or .desktop extension as part of its name" },
	{ "preferences", "show-show-thumbnails-toolbar", NEMO_CONFIG_BOOL, "false", NULL, NULL, NULL },
	{ "preferences", "show-toggle-extra-pane-toolbar", NEMO_CONFIG_BOOL, "false", NULL, NULL, NULL },
	{ "preferences", "show-up-icon-toolbar", NEMO_CONFIG_BOOL, "true", NULL, NULL, NULL },
	{ "preferences", "size-prefixes", NEMO_CONFIG_ENUM, "base-2", NULL, enum_SizePrefixes, "Whether sizes count in 1024s or 1000s" },
	{ "preferences", "sort-directories-first", NEMO_CONFIG_BOOL, "true", NULL, NULL, NULL },
	{ "preferences", "sort-favorites-first", NEMO_CONFIG_BOOL, "true", NULL, NULL, NULL },
	{ "preferences", "start-with-dual-pane", NEMO_CONFIG_BOOL, "false", NULL, NULL, NULL },
	{ "preferences", "swap-trash-delete", NEMO_CONFIG_BOOL, "false", NULL, NULL, "Swap the Trash and Delete keys" },
	{ "preferences", "tab-width-max-percent", NEMO_CONFIG_INT, "25", NULL, NULL, "Widest a tab may get, as a percentage of the tab strip" },
	{ "preferences", "tab-width-min-percent", NEMO_CONFIG_INT, "10", NULL, NULL, "Narrowest a tab may get, as a percentage of the tab strip" },
	{ "preferences", "tabs-open-position", NEMO_CONFIG_ENUM, "after-current-tab", NULL, enum_TabPosition, "Where a new tab goes" },
	{ "preferences", "thumbnail-limit", NEMO_CONFIG_INT, "104857600", NULL, NULL, "Largest image a thumbnail is made for, in bytes" },
	{ "preferences", "thumbnail-threads", NEMO_CONFIG_INT, "-1", NULL, NULL, "Threads used to make thumbnails, -1 for half the processors. Takes effect on restart." },
	{ "preferences", "tooltips-in-icon-view", NEMO_CONFIG_BOOL, "false", NULL, NULL, NULL },
	{ "preferences", "tooltips-in-list-view", NEMO_CONFIG_BOOL, "false", NULL, NULL, NULL },
	{ "preferences", "tooltips-show-access-date", NEMO_CONFIG_BOOL, "false", NULL, NULL, NULL },
	{ "preferences", "tooltips-show-birth-date", NEMO_CONFIG_BOOL, "false", NULL, NULL, "Show the creation date in a tooltip" },
	{ "preferences", "tooltips-show-file-type", NEMO_CONFIG_BOOL, "false", NULL, NULL, NULL },
	{ "preferences", "tooltips-show-mod-date", NEMO_CONFIG_BOOL, "false", NULL, NULL, NULL },
	{ "preferences", "tooltips-show-path", NEMO_CONFIG_BOOL, "false", NULL, NULL, NULL },
	{ "preferences", "treat-root-as-normal", NEMO_CONFIG_BOOL, "false", NULL, NULL, "Drop the safeguards that apply when running as root" },
	{ "preferences", "window-per-process", NEMO_CONFIG_BOOL, "true", NULL, NULL, "Each new window runs as its own process" },
	{ "preferences.menu-config", "background-menu-compress", NEMO_CONFIG_BOOL, "true", NULL, NULL, NULL },
	{ "preferences.menu-config", "background-menu-copy-path", NEMO_CONFIG_BOOL, "true", NULL, NULL, NULL },
	{ "preferences.menu-config", "background-menu-create-new-folder", NEMO_CONFIG_BOOL, "true", NULL, NULL, NULL },
	{ "preferences.menu-config", "background-menu-open-as-root", NEMO_CONFIG_BOOL, "true", NULL, NULL, NULL },
	{ "preferences.menu-config", "background-menu-open-in-terminal", NEMO_CONFIG_BOOL, "true", NULL, NULL, NULL },
	{ "preferences.menu-config", "background-menu-paste", NEMO_CONFIG_BOOL, "true", NULL, NULL, NULL },
	{ "preferences.menu-config", "background-menu-properties", NEMO_CONFIG_BOOL, "true", NULL, NULL, NULL },
	{ "preferences.menu-config", "background-menu-scripts", NEMO_CONFIG_BOOL, "true", NULL, NULL, NULL },
	{ "preferences.menu-config", "background-menu-show-hidden-files", NEMO_CONFIG_BOOL, "true", NULL, NULL, NULL },
	{ "preferences.menu-config", "iconview-menu-arrange-items", NEMO_CONFIG_BOOL, "true", NULL, NULL, NULL },
	{ "preferences.menu-config", "iconview-menu-organize-by-name", NEMO_CONFIG_BOOL, "true", NULL, NULL, NULL },
	{ "preferences.menu-config", "selection-menu-compress", NEMO_CONFIG_BOOL, "true", NULL, NULL, NULL },
	{ "preferences.menu-config", "selection-menu-extract", NEMO_CONFIG_BOOL, "true", NULL, NULL, NULL },
	{ "preferences.menu-config", "selection-menu-copy", NEMO_CONFIG_BOOL, "true", NULL, NULL, NULL },
	{ "preferences.menu-config", "selection-menu-copy-path", NEMO_CONFIG_BOOL, "true", NULL, NULL, NULL },
	{ "preferences.menu-config", "selection-menu-copy-to", NEMO_CONFIG_BOOL, "false", NULL, NULL, NULL },
	{ "preferences.menu-config", "selection-menu-cut", NEMO_CONFIG_BOOL, "true", NULL, NULL, NULL },
	{ "preferences.menu-config", "selection-menu-duplicate", NEMO_CONFIG_BOOL, "false", NULL, NULL, NULL },
	{ "preferences.menu-config", "selection-menu-favorite", NEMO_CONFIG_BOOL, "true", NULL, NULL, NULL },
	{ "preferences.menu-config", "selection-menu-make-link", NEMO_CONFIG_BOOL, "true", NULL, NULL, NULL },
	{ "preferences.menu-config", "selection-menu-move-to", NEMO_CONFIG_BOOL, "false", NULL, NULL, NULL },
	{ "preferences.menu-config", "selection-menu-move-to-trash", NEMO_CONFIG_BOOL, "true", NULL, NULL, NULL },
	{ "preferences.menu-config", "selection-menu-open", NEMO_CONFIG_BOOL, "true", NULL, NULL, NULL },
	{ "preferences.menu-config", "selection-menu-open-as-root", NEMO_CONFIG_BOOL, "true", NULL, NULL, NULL },
	{ "preferences.menu-config", "selection-menu-open-in-new-tab", NEMO_CONFIG_BOOL, "true", NULL, NULL, NULL },
	{ "preferences.menu-config", "selection-menu-open-in-new-window", NEMO_CONFIG_BOOL, "true", NULL, NULL, NULL },
	{ "preferences.menu-config", "selection-menu-open-in-terminal", NEMO_CONFIG_BOOL, "true", NULL, NULL, NULL },
	{ "preferences.menu-config", "selection-menu-paste", NEMO_CONFIG_BOOL, "true", NULL, NULL, NULL },
	{ "preferences.menu-config", "selection-menu-pin", NEMO_CONFIG_BOOL, "true", NULL, NULL, NULL },
	{ "preferences.menu-config", "selection-menu-properties", NEMO_CONFIG_BOOL, "true", NULL, NULL, NULL },
	{ "preferences.menu-config", "selection-menu-rename", NEMO_CONFIG_BOOL, "true", NULL, NULL, NULL },
	{ "preferences.menu-config", "selection-menu-scripts", NEMO_CONFIG_BOOL, "true", NULL, NULL, NULL },
	{ "search", "group-by-folder", NEMO_CONFIG_BOOL, "false", NULL, NULL, "Group results under the folder holding them instead of one flat list" },
	{ "search", "disabled-search-helpers", NEMO_CONFIG_STRING_LIST, NULL, deflist_search_disabled_search_helpers, NULL, "Content-search helpers to skip" },
	{ "search", "search-content-case-sensitive", NEMO_CONFIG_BOOL, "false", NULL, NULL, NULL, NEMO_CONFIG_KEY_STATE },
	{ "search", "search-content-use-raw", NEMO_CONFIG_BOOL, "false", NULL, NULL, "Match content as raw bytes rather than text" },
	{ "search", "search-content-use-regex", NEMO_CONFIG_BOOL, "false", NULL, NULL, NULL, NEMO_CONFIG_KEY_STATE },
	{ "search", "search-file-case-sensitive", NEMO_CONFIG_BOOL, "false", NULL, NULL, NULL, NEMO_CONFIG_KEY_STATE },
	{ "search", "search-files-recursively", NEMO_CONFIG_BOOL, "true", NULL, NULL, NULL },
	{ "search", "search-files-use-regex", NEMO_CONFIG_BOOL, "false", NULL, NULL, NULL, NEMO_CONFIG_KEY_STATE },
	{ "search", "search-regex-format", NEMO_CONFIG_STRING, "pcre", NULL, NULL, "Regex flavor: pcre or javascript" },
	{ "search", "search-reverse-sort", NEMO_CONFIG_BOOL, "false", NULL, NULL, NULL, NEMO_CONFIG_KEY_STATE },
	{ "search", "search-skip-folders", NEMO_CONFIG_STRING_LIST, NULL, deflist_search_search_skip_folders, NULL, "Paths or folder names a search never enters" },
	{ "search", "search-sort-column", NEMO_CONFIG_STRING, "", NULL, NULL, NULL, NEMO_CONFIG_KEY_STATE },
	{ "search", "search-visible-columns", NEMO_CONFIG_STRING_LIST, NULL, deflist_search_search_visible_columns, NULL, NULL, NEMO_CONFIG_KEY_STATE },
	{ "state", "first-run-done", NEMO_CONFIG_BOOL, "false", NULL, NULL, "Clear this to get the platform's default bookmarks back on the next start.", NEMO_CONFIG_KEY_STATE },
	{ "thumbnailers", "disable", NEMO_CONFIG_STRING_LIST, NULL, deflist_thumbnailers_disable, NULL, "Mime types not to use an external thumbnailer for" },
	{ "thumbnailers", "disable-all", NEMO_CONFIG_BOOL, "false", NULL, NULL, NULL },
	{ "windows", "path-separator", NEMO_CONFIG_ENUM, "backslash", NULL, enum_PathSeparator, "Which separator paths are shown with" },
	{ "windows", "allow-slash-input", NEMO_CONFIG_BOOL, "true", NULL, NULL, "Accept a forward slash as a separator in a typed location" },
	{ "windows", "show-dot-files", NEMO_CONFIG_BOOL, "false", NULL, NULL, "Show files whose name starts with a dot. Separate from the Windows hidden flag." },
	{ "windows", "use-search-index", NEMO_CONFIG_BOOL, "false", NULL, NULL, "Answer a search from the Windows Search index where the folder is indexed" },
	{ "windows", "associations", NEMO_CONFIG_STRING_LIST, NULL, deflist_windows_associations, NULL, "The program to open a type with, as <extension>=<command line> with %1 for the file" },
	{ "windows", "terminal-candidates", NEMO_CONFIG_STRING_LIST, NULL, deflist_windows_terminal_candidates, NULL, "Terminals to try for Open in Terminal, in order" },
	{ "window-state", "bookmarks-expanded", NEMO_CONFIG_BOOL, "true", NULL, NULL, NULL, NEMO_CONFIG_KEY_STATE },
	{ "window-state", "devices-expanded", NEMO_CONFIG_BOOL, "true", NULL, NULL, NULL, NEMO_CONFIG_KEY_STATE },
	{ "window-state", "geometry", NEMO_CONFIG_STRING, "", NULL, NULL, NULL, NEMO_CONFIG_KEY_STATE },
	{ "window-state", "maximized", NEMO_CONFIG_BOOL, "false", NULL, NULL, NULL, NEMO_CONFIG_KEY_STATE },
	{ "window-state", "my-computer-expanded", NEMO_CONFIG_BOOL, "true", NULL, NULL, NULL, NEMO_CONFIG_KEY_STATE },
	{ "window-state", "network-expanded", NEMO_CONFIG_BOOL, "true", NULL, NULL, NULL, NEMO_CONFIG_KEY_STATE },
	{ "window-state", "sidebar-bookmark-breakpoint", NEMO_CONFIG_INT, "-1", NULL, NULL, "Bookmark index where the dedicated sidebar section starts", NEMO_CONFIG_KEY_STATE },
	{ "window-state", "sidebar-tree-width", NEMO_CONFIG_INT, "480", NULL, NULL, "Width of the tree view pane", NEMO_CONFIG_KEY_STATE },
	{ "window-state", "sidebar-width", NEMO_CONFIG_INT, "240", NULL, NULL, "Width of the places pane", NEMO_CONFIG_KEY_STATE },
	{ "window-state", "start-with-location-bar", NEMO_CONFIG_BOOL, "true", NULL, NULL, "Show the location bar in new windows" },
	{ "window-state", "start-with-menu-bar", NEMO_CONFIG_BOOL, "true", NULL, NULL, "Show the menu bar in new windows" },
	{ "window-state", "start-with-places", NEMO_CONFIG_BOOL, "true", NULL, NULL, "Show the places pane in new windows" },
	{ "window-state", "start-with-status-bar", NEMO_CONFIG_BOOL, "true", NULL, NULL, "Show the status bar in new windows" },
	{ "window-state", "start-with-toolbar", NEMO_CONFIG_BOOL, "true", NULL, NULL, "Show the toolbar in new windows" },
	{ "window-state", "start-with-tree", NEMO_CONFIG_BOOL, "false", NULL, NULL, "Show the tree view pane in new windows" },
	{ NULL, NULL, 0, NULL, NULL, NULL, NULL }
};

#endif /* NEMO_CONFIG_KEYS_H */
