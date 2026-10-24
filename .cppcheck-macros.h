/*
 * Macros cppcheck can't see, since lint-c.bash gives it no include paths.
 * It's force-included ahead of every file. A macro it doesn't know between two
 * string literals makes it give up on the whole file with unknownMacro, so a
 * new one fails the gate and goes here.
 *
 * These live in a header, not a cfg, because cppcheck 2.21's gtk.cfg defines
 * many of the GLib ones itself, and a cfg defining one again won't load. The
 * #ifndef lets whichever cppcheck has it first win. Values are GLib's on
 * 64-bit Linux, to match --platform=unix64.
 */

#ifndef NEMO_CPPCHECK_MACROS_H
#define NEMO_CPPCHECK_MACROS_H

/* clang's, for the sanitizer checks. gcc has none, and says so with
   __SANITIZE_ADDRESS__. No #ifndef, since 2.21 calls it defined and then
   stops on the #if that uses it. */
#define __has_feature(x) 0

/* eel/eel-analyzer.h, which cppcheck finds only where it's included in quotes.
   The same empty bodies, so a second definition there isn't a clash. */
#ifndef EEL_ANALYZER_SKIP
#define EEL_ANALYZER_SKIP(gcc_name)
#define EEL_ANALYZER_END
#endif

/* From the build: nemo-build-number.h and the -D flags in source/meson.build. */
#ifndef NEMO_APP_SLUG
#define NEMO_APP_SLUG "nemo-anywhere"
#endif
#ifndef NEMO_VERSION_STRING
#define NEMO_VERSION_STRING "v0.0.0 build 0"
#endif

/* glibconfig.h and gutils.h */
#ifndef G_DIR_SEPARATOR_S
#define G_DIR_SEPARATOR_S "/"
#endif
#ifndef G_GINT64_FORMAT
#define G_GINT64_FORMAT "li"
#endif
#ifndef G_GOFFSET_FORMAT
#define G_GOFFSET_FORMAT "li"
#endif
#ifndef G_GSIZE_FORMAT
#define G_GSIZE_FORMAT "lu"
#endif
#ifndef G_GUINT64_FORMAT
#define G_GUINT64_FORMAT "lu"
#endif
#ifndef G_SEARCHPATH_SEPARATOR_S
#define G_SEARCHPATH_SEPARATOR_S ":"
#endif

/* gfileinfo.h, the ones this tree uses */
#ifndef G_FILE_ATTRIBUTE_ACCESS_CAN_DELETE
#define G_FILE_ATTRIBUTE_ACCESS_CAN_DELETE "access::can-delete"
#endif
#ifndef G_FILE_ATTRIBUTE_ACCESS_CAN_EXECUTE
#define G_FILE_ATTRIBUTE_ACCESS_CAN_EXECUTE "access::can-execute"
#endif
#ifndef G_FILE_ATTRIBUTE_ACCESS_CAN_READ
#define G_FILE_ATTRIBUTE_ACCESS_CAN_READ "access::can-read"
#endif
#ifndef G_FILE_ATTRIBUTE_ACCESS_CAN_RENAME
#define G_FILE_ATTRIBUTE_ACCESS_CAN_RENAME "access::can-rename"
#endif
#ifndef G_FILE_ATTRIBUTE_ACCESS_CAN_TRASH
#define G_FILE_ATTRIBUTE_ACCESS_CAN_TRASH "access::can-trash"
#endif
#ifndef G_FILE_ATTRIBUTE_ACCESS_CAN_WRITE
#define G_FILE_ATTRIBUTE_ACCESS_CAN_WRITE "access::can-write"
#endif
#ifndef G_FILE_ATTRIBUTE_DOS_REPARSE_POINT_TAG
#define G_FILE_ATTRIBUTE_DOS_REPARSE_POINT_TAG "dos::reparse-point-tag"
#endif
#ifndef G_FILE_ATTRIBUTE_FILESYSTEM_FREE
#define G_FILE_ATTRIBUTE_FILESYSTEM_FREE "filesystem::free"
#endif
#ifndef G_FILE_ATTRIBUTE_FILESYSTEM_READONLY
#define G_FILE_ATTRIBUTE_FILESYSTEM_READONLY "filesystem::readonly"
#endif
#ifndef G_FILE_ATTRIBUTE_FILESYSTEM_SIZE
#define G_FILE_ATTRIBUTE_FILESYSTEM_SIZE "filesystem::size"
#endif
#ifndef G_FILE_ATTRIBUTE_FILESYSTEM_TYPE
#define G_FILE_ATTRIBUTE_FILESYSTEM_TYPE "filesystem::type"
#endif
#ifndef G_FILE_ATTRIBUTE_FILESYSTEM_USED
#define G_FILE_ATTRIBUTE_FILESYSTEM_USED "filesystem::used"
#endif
#ifndef G_FILE_ATTRIBUTE_FILESYSTEM_USE_PREVIEW
#define G_FILE_ATTRIBUTE_FILESYSTEM_USE_PREVIEW "filesystem::use-preview"
#endif
#ifndef G_FILE_ATTRIBUTE_GVFS_BACKEND
#define G_FILE_ATTRIBUTE_GVFS_BACKEND "gvfs::backend"
#endif
#ifndef G_FILE_ATTRIBUTE_ID_FILE
#define G_FILE_ATTRIBUTE_ID_FILE "id::file"
#endif
#ifndef G_FILE_ATTRIBUTE_ID_FILESYSTEM
#define G_FILE_ATTRIBUTE_ID_FILESYSTEM "id::filesystem"
#endif
#ifndef G_FILE_ATTRIBUTE_MOUNTABLE_CAN_EJECT
#define G_FILE_ATTRIBUTE_MOUNTABLE_CAN_EJECT "mountable::can-eject"
#endif
#ifndef G_FILE_ATTRIBUTE_MOUNTABLE_CAN_MOUNT
#define G_FILE_ATTRIBUTE_MOUNTABLE_CAN_MOUNT "mountable::can-mount"
#endif
#ifndef G_FILE_ATTRIBUTE_MOUNTABLE_CAN_POLL
#define G_FILE_ATTRIBUTE_MOUNTABLE_CAN_POLL "mountable::can-poll"
#endif
#ifndef G_FILE_ATTRIBUTE_MOUNTABLE_CAN_START
#define G_FILE_ATTRIBUTE_MOUNTABLE_CAN_START "mountable::can-start"
#endif
#ifndef G_FILE_ATTRIBUTE_MOUNTABLE_CAN_START_DEGRADED
#define G_FILE_ATTRIBUTE_MOUNTABLE_CAN_START_DEGRADED "mountable::can-start-degraded"
#endif
#ifndef G_FILE_ATTRIBUTE_MOUNTABLE_CAN_STOP
#define G_FILE_ATTRIBUTE_MOUNTABLE_CAN_STOP "mountable::can-stop"
#endif
#ifndef G_FILE_ATTRIBUTE_MOUNTABLE_CAN_UNMOUNT
#define G_FILE_ATTRIBUTE_MOUNTABLE_CAN_UNMOUNT "mountable::can-unmount"
#endif
#ifndef G_FILE_ATTRIBUTE_MOUNTABLE_IS_MEDIA_CHECK_AUTOMATIC
#define G_FILE_ATTRIBUTE_MOUNTABLE_IS_MEDIA_CHECK_AUTOMATIC "mountable::is-media-check-automatic"
#endif
#ifndef G_FILE_ATTRIBUTE_MOUNTABLE_START_STOP_TYPE
#define G_FILE_ATTRIBUTE_MOUNTABLE_START_STOP_TYPE "mountable::start-stop-type"
#endif
#ifndef G_FILE_ATTRIBUTE_OWNER_GROUP
#define G_FILE_ATTRIBUTE_OWNER_GROUP "owner::group"
#endif
#ifndef G_FILE_ATTRIBUTE_OWNER_USER
#define G_FILE_ATTRIBUTE_OWNER_USER "owner::user"
#endif
#ifndef G_FILE_ATTRIBUTE_OWNER_USER_REAL
#define G_FILE_ATTRIBUTE_OWNER_USER_REAL "owner::user-real"
#endif
#ifndef G_FILE_ATTRIBUTE_PREVIEW_ICON
#define G_FILE_ATTRIBUTE_PREVIEW_ICON "preview::icon"
#endif
#ifndef G_FILE_ATTRIBUTE_SELINUX_CONTEXT
#define G_FILE_ATTRIBUTE_SELINUX_CONTEXT "selinux::context"
#endif
#ifndef G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE
#define G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE "standard::content-type"
#endif
#ifndef G_FILE_ATTRIBUTE_STANDARD_COPY_NAME
#define G_FILE_ATTRIBUTE_STANDARD_COPY_NAME "standard::copy-name"
#endif
#ifndef G_FILE_ATTRIBUTE_STANDARD_DESCRIPTION
#define G_FILE_ATTRIBUTE_STANDARD_DESCRIPTION "standard::description"
#endif
#ifndef G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME
#define G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME "standard::display-name"
#endif
#ifndef G_FILE_ATTRIBUTE_STANDARD_EDIT_NAME
#define G_FILE_ATTRIBUTE_STANDARD_EDIT_NAME "standard::edit-name"
#endif
#ifndef G_FILE_ATTRIBUTE_STANDARD_FAST_CONTENT_TYPE
#define G_FILE_ATTRIBUTE_STANDARD_FAST_CONTENT_TYPE "standard::fast-content-type"
#endif
#ifndef G_FILE_ATTRIBUTE_STANDARD_ICON
#define G_FILE_ATTRIBUTE_STANDARD_ICON "standard::icon"
#endif
#ifndef G_FILE_ATTRIBUTE_STANDARD_IS_BACKUP
#define G_FILE_ATTRIBUTE_STANDARD_IS_BACKUP "standard::is-backup"
#endif
#ifndef G_FILE_ATTRIBUTE_STANDARD_IS_HIDDEN
#define G_FILE_ATTRIBUTE_STANDARD_IS_HIDDEN "standard::is-hidden"
#endif
#ifndef G_FILE_ATTRIBUTE_STANDARD_IS_SYMLINK
#define G_FILE_ATTRIBUTE_STANDARD_IS_SYMLINK "standard::is-symlink"
#endif
#ifndef G_FILE_ATTRIBUTE_STANDARD_IS_VIRTUAL
#define G_FILE_ATTRIBUTE_STANDARD_IS_VIRTUAL "standard::is-virtual"
#endif
#ifndef G_FILE_ATTRIBUTE_STANDARD_NAME
#define G_FILE_ATTRIBUTE_STANDARD_NAME "standard::name"
#endif
#ifndef G_FILE_ATTRIBUTE_STANDARD_SIZE
#define G_FILE_ATTRIBUTE_STANDARD_SIZE "standard::size"
#endif
#ifndef G_FILE_ATTRIBUTE_STANDARD_SORT_ORDER
#define G_FILE_ATTRIBUTE_STANDARD_SORT_ORDER "standard::sort-order"
#endif
#ifndef G_FILE_ATTRIBUTE_STANDARD_SYMBOLIC_ICON
#define G_FILE_ATTRIBUTE_STANDARD_SYMBOLIC_ICON "standard::symbolic-icon"
#endif
#ifndef G_FILE_ATTRIBUTE_STANDARD_SYMLINK_TARGET
#define G_FILE_ATTRIBUTE_STANDARD_SYMLINK_TARGET "standard::symlink-target"
#endif
#ifndef G_FILE_ATTRIBUTE_STANDARD_TARGET_URI
#define G_FILE_ATTRIBUTE_STANDARD_TARGET_URI "standard::target-uri"
#endif
#ifndef G_FILE_ATTRIBUTE_STANDARD_TYPE
#define G_FILE_ATTRIBUTE_STANDARD_TYPE "standard::type"
#endif
#ifndef G_FILE_ATTRIBUTE_THUMBNAILING_FAILED
#define G_FILE_ATTRIBUTE_THUMBNAILING_FAILED "thumbnail::failed"
#endif
#ifndef G_FILE_ATTRIBUTE_THUMBNAIL_PATH
#define G_FILE_ATTRIBUTE_THUMBNAIL_PATH "thumbnail::path"
#endif
#ifndef G_FILE_ATTRIBUTE_TIME_ACCESS
#define G_FILE_ATTRIBUTE_TIME_ACCESS "time::access"
#endif
#ifndef G_FILE_ATTRIBUTE_TIME_CHANGED
#define G_FILE_ATTRIBUTE_TIME_CHANGED "time::changed"
#endif
#ifndef G_FILE_ATTRIBUTE_TIME_CREATED
#define G_FILE_ATTRIBUTE_TIME_CREATED "time::created"
#endif
#ifndef G_FILE_ATTRIBUTE_TIME_MODIFIED
#define G_FILE_ATTRIBUTE_TIME_MODIFIED "time::modified"
#endif
#ifndef G_FILE_ATTRIBUTE_TIME_MODIFIED_USEC
#define G_FILE_ATTRIBUTE_TIME_MODIFIED_USEC "time::modified-usec"
#endif
#ifndef G_FILE_ATTRIBUTE_TRASH_DELETION_DATE
#define G_FILE_ATTRIBUTE_TRASH_DELETION_DATE "trash::deletion-date"
#endif
#ifndef G_FILE_ATTRIBUTE_TRASH_ITEM_COUNT
#define G_FILE_ATTRIBUTE_TRASH_ITEM_COUNT "trash::item-count"
#endif
#ifndef G_FILE_ATTRIBUTE_TRASH_ORIG_PATH
#define G_FILE_ATTRIBUTE_TRASH_ORIG_PATH "trash::orig-path"
#endif
#ifndef G_FILE_ATTRIBUTE_UNIX_DEVICE
#define G_FILE_ATTRIBUTE_UNIX_DEVICE "unix::device"
#endif
#ifndef G_FILE_ATTRIBUTE_UNIX_GID
#define G_FILE_ATTRIBUTE_UNIX_GID "unix::gid"
#endif
#ifndef G_FILE_ATTRIBUTE_UNIX_INODE
#define G_FILE_ATTRIBUTE_UNIX_INODE "unix::inode"
#endif
#ifndef G_FILE_ATTRIBUTE_UNIX_IS_MOUNTPOINT
#define G_FILE_ATTRIBUTE_UNIX_IS_MOUNTPOINT "unix::is-mountpoint"
#endif
#ifndef G_FILE_ATTRIBUTE_UNIX_MODE
#define G_FILE_ATTRIBUTE_UNIX_MODE "unix::mode"
#endif
#ifndef G_FILE_ATTRIBUTE_UNIX_UID
#define G_FILE_ATTRIBUTE_UNIX_UID "unix::uid"
#endif

#endif
