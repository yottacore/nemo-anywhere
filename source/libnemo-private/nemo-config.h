/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nemo-config.h - app-owned settings store.

   Copyright © 2026 t00mietum (CryptogID: ปʬϝღถɔ4რఠΔթะ9ƾǝu).

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; version 2 of the
   License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public
   License along with this program; if not, write to the
   Free Software Foundation, Inc., 51 Franklin Street, Suite 500,
   Boston, MA 02110-1335, USA.
*/

/* Replaces GSettings: every setting lives in one hand-editable SHCL file
 * under the app config dir, identically on every platform - no dconf, no
 * Windows registry, no compiled schema to install or ship.
 *
 * The API deliberately mirrors the slice of GSettings we used, down to the
 * detailed "changed::key" signal, so call sites read the same as before.
 * Types and defaults come from the table in nemo-config-keys.h, which is
 * also mirrored by the shipped .schema.shcl for `shcl check`.
 */

#ifndef NEMO_CONFIG_H
#define NEMO_CONFIG_H

#include <glib-object.h>

G_BEGIN_DECLS

typedef enum {
	NEMO_CONFIG_BOOL,
	NEMO_CONFIG_INT,
	NEMO_CONFIG_FLOAT,
	NEMO_CONFIG_STRING,
	NEMO_CONFIG_STRING_LIST,
	NEMO_CONFIG_ENUM
} NemoConfigType;

typedef struct {
	const char *nick;
	int         value;
} NemoConfigEnumValue;

typedef enum {
	NEMO_CONFIG_KEY_NONE  = 0,
	/* Written back by the app as it runs - a size, a position, the last state
	   of a toggle. Left out of the catalog of defaults, where it would read as
	   something to set and then be overwritten. */
	NEMO_CONFIG_KEY_STATE = 1 << 0
} NemoConfigKeyFlags;

/* One settable key. Defaults live here rather than at the call site, so a
 * key read from two places cannot disagree about what its default is. */
typedef struct {
	const char                *group;      /* "preferences"; "" is the file root */
	const char                *key;
	NemoConfigType             type;
	const char                *def;        /* scalar default, SHCL text; NULL for lists */
	const char *const         *def_list;   /* STRING_LIST default, NULL-terminated */
	const NemoConfigEnumValue *enum_values;/* ENUM only */
	const char                *summary;    /* comment above the key; NULL where the name says it */
	NemoConfigKeyFlags         flags;
} NemoConfigKey;

/* Carries a value through a bind mapping - the typed stand-in for the
 * GVariant the old mapping callbacks took. */
typedef struct {
	NemoConfigType type;
	gboolean       b;
	gint64         i;
	gdouble        d;
	char          *s;      /* STRING, and the nick for ENUM; owned by a set mapping */
	char         **sv;     /* STRING_LIST; owned by a set mapping */
} NemoConfigValue;

#define NEMO_TYPE_CONFIG_GROUP (nemo_config_group_get_type ())
G_DECLARE_FINAL_TYPE (NemoConfigGroup, nemo_config_group, NEMO, CONFIG_GROUP, GObject)

typedef enum {
	NEMO_CONFIG_BIND_DEFAULT        = 0,
	NEMO_CONFIG_BIND_GET            = 1 << 0,
	NEMO_CONFIG_BIND_SET            = 1 << 1,
	NEMO_CONFIG_BIND_INVERT_BOOLEAN = 1 << 2,
	NEMO_CONFIG_BIND_NO_SENSITIVITY = 1 << 3  /* accepted, ignored - we never desensitized */
} NemoConfigBindFlags;

typedef gboolean (*NemoConfigGetMapping) (GValue                *value,
                                          const NemoConfigValue *config_value,
                                          gpointer               user_data);
typedef gboolean (*NemoConfigSetMapping) (const GValue          *value,
                                          NemoConfigValue       *config_value,
                                          gpointer               user_data);

/* Load the file (or start empty) and hand out groups. Idempotent. */
void             nemo_config_init            (void);
/* Write any pending change out now, without tearing anything down. */
void             nemo_config_flush           (void);
/* Flush, then stop watching the file. Call before the process ends. */
void             nemo_config_shutdown        (void);
NemoConfigGroup *nemo_config_get_group       (const char *group);
/* Absolute path of the settings file, for diagnostics. Free with g_free. */
/* Whether nemo_config_init() has run - callers reached from local_command_line
 * can run before it and must not cache what they read. */
gboolean         nemo_config_is_ready        (void);
char            *nemo_config_get_path        (void);

/* A bool key's declared default. Safe before nemo_config_init(), for the
 * callers above that need an answer before there is a file to read. */
gboolean         nemo_config_get_default_boolean (const char *group, const char *key);

gboolean   nemo_config_get_boolean (NemoConfigGroup *group, const char *key);
gint       nemo_config_get_int     (NemoConfigGroup *group, const char *key);
/* Same key, full width. The store is 64-bit; the gint form is the convenience
 * one, and silently truncates anything that doesn't fit. */
gint64     nemo_config_get_int64   (NemoConfigGroup *group, const char *key);
gdouble    nemo_config_get_double  (NemoConfigGroup *group, const char *key);
char      *nemo_config_get_string  (NemoConfigGroup *group, const char *key);
char     **nemo_config_get_strv    (NemoConfigGroup *group, const char *key);
gint       nemo_config_get_enum    (NemoConfigGroup *group, const char *key);

void       nemo_config_set_boolean (NemoConfigGroup *group, const char *key, gboolean value);
void       nemo_config_set_int     (NemoConfigGroup *group, const char *key, gint value);
void       nemo_config_set_int64   (NemoConfigGroup *group, const char *key, gint64 value);
void       nemo_config_set_double  (NemoConfigGroup *group, const char *key, gdouble value);
void       nemo_config_set_string  (NemoConfigGroup *group, const char *key, const char *value);
void       nemo_config_set_strv    (NemoConfigGroup *group, const char *key, const char *const *value);
void       nemo_config_set_enum    (NemoConfigGroup *group, const char *key, gint value);

/* Drop the key so its default applies again. */
void       nemo_config_reset       (NemoConfigGroup *group, const char *key);
/* TRUE when the file has the key, so a read gives its value, not the default. */
gboolean   nemo_config_is_set      (NemoConfigGroup *group, const char *key);
/* Drop every stored key - what --reset does. */
void       nemo_config_reset_all   (void);
/* Drop stored values naming a POSIX absolute path. Windows only, and only for
   the first run, where such a value can only have come from another machine. */
void       nemo_config_drop_foreign_paths (void);
/* Every key declared for this group, NULL-terminated. Free with g_strfreev. */
char     **nemo_config_list_keys   (NemoConfigGroup *group);

void nemo_config_bind              (NemoConfigGroup      *group,
                                    const char           *key,
                                    gpointer              object,
                                    const char           *property,
                                    NemoConfigBindFlags   flags);
void nemo_config_bind_with_mapping (NemoConfigGroup      *group,
                                    const char           *key,
                                    gpointer              object,
                                    const char           *property,
                                    NemoConfigBindFlags   flags,
                                    NemoConfigGetMapping  get_mapping,
                                    NemoConfigSetMapping  set_mapping,
                                    gpointer              user_data,
                                    GDestroyNotify        destroy);

G_END_DECLS

#endif /* NEMO_CONFIG_H */
