/**
 * PkgClip - Copyright (C) 2012 Olivier Brunel
 *
 * pkgclip.h
 * Copyright (C) 2012 Olivier Brunel <i.am.jack.mail@gmail.com>
 * 
 * This file is part of PkgClip.
 *
 * PkgClip is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * PkgClip is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * PkgClip. If not, see http://www.gnu.org/licenses/
 */

#ifndef _PKGCLIP_H
#define _PKGCLIP_H

/* gtk */
#include <gtk/gtk.h>

/* gio - for dbus */
#include <gio/gio.h>

/* alpm */
#include <alpm.h>
#include <alpm_list.h>

#if defined(GIT_VERSION)
#undef PACKAGE_VERSION
#define PACKAGE_VERSION GIT_VERSION
#endif
#define PACKAGE_TAG             "Cached Packages Trimmer Utility"

#define PACMAN_CONF             "/etc"
#define ROOT_PATH               "/"
#define DB_PATH                 "/var/lib/pacman/"
#define CACHE_PATH              "/var/cache/pacman/pkg/"

#define _UNUSED_                __attribute__ ((unused)) 

typedef enum {
    COL_PC_PKG,
    COL_PACKAGE,
    COL_VERSION,
    COL_SIZE,
    COL_REMOVE,
    COL_RECOMM,
    COL_REASON,
    COL_NB_OLD_VER,
    COL_NB_OLD_VER_TOTAL,
    COL_NB
} col_t;

typedef enum {
    RECOMM_KEEP,
    RECOMM_REMOVE
} recomm_t;

typedef enum {
    REASON_AS_INSTALLED,
    REASON_NEWER_THAN_INSTALLED,
    REASON_INSTALLED,
    REASON_OLDER_VERSION,
    REASON_ALREADY_OLDER_VERSION,
    REASON_OLDER_PKGREL,
    REASON_PKG_NOT_INSTALLED,
    NB_REASONS
} reason_t;

typedef enum {
    MARK_SELECTION,
    UNMARK_SELECTION,
    MARK_REASON,
    UNMARK_REASON,
    SELECT_RESTORE
} mark_t;

typedef struct _progress_win_t {
    GtkWidget   *window;
    GtkWidget   *label;
    GtkWidget   *pbar;

    unsigned int total_packages;
    off_t        total_size;
    unsigned int success_packages;
    off_t        success_size;
    unsigned int error_packages;
    off_t        error_size;

    char        *error_messages;
    unsigned int em_alloc;
    unsigned int em_len;
} progress_win_t;

typedef struct _prefs_win_t {
    GtkWidget    *window;
    GtkWidget    *filechooser;
    GtkWidget    *entry;
    GtkWidget    *chk_old_pkgrel;
    GtkWidget    *chk_sane_sort_indicator;
    GtkWidget    *chk_autoload;
    GtkWidget    *chk_show_pkg_info;
    GtkWidget    *entry_pkg_info;
    GtkTreeView  *tree_ai;
    GtkTreeModel *model_ai;
    gboolean      ai_updated;
    GtkWidget    *entry_ai;

    char         *pkg_info;
} prefs_win_t;

typedef enum {
    VAR_NAME,
    VAR_DESC,
    VAR_VERSION,
    VAR_FILE,
    VAR_SIZE,
    VAR_RECOMM,
    VAR_REASON,
} info_var_t;

typedef struct _pkgclip_t {
    /* config */
    char            *pacmanconf;
    char            *dbpath;
    char            *rootpath;
    alpm_list_t     *cachedirs;
    gboolean         sane_sort_indicator;
    gboolean         autoload;
    gboolean         old_pkgrel;
    recomm_t         recomm[NB_REASONS];
    int              nb_old_ver;
    alpm_list_t     *as_installed;
    int              nb_old_ver_ai;
    gboolean         show_pkg_info;
    char            *pkg_info;
    alpm_list_t     *pkg_info_extras;

    /* app/gui */
    gboolean         in_gtk_main;
    gboolean         is_loading;
    gboolean         abort;
    GtkWidget       *window;
    GtkListStore    *store;
    GtkWidget       *list;
    GtkWidget       *label;
    GtkWidget       *button;
    GtkWidget       *mnu_reload;
    GtkWidget       *mnu_remove;
    GtkWidget       *mnu_edit;
    GtkWidget       *sep_pkg_info;
    GtkWidget       *lbl_pkg_info;

    gulong           handler_pkg_info;
    GString         *str_info;

    prefs_win_t     *prefs;

    alpm_handle_t   *handle;
    alpm_list_t     *packages;

    unsigned int     total_packages;
    off_t            total_size;
    unsigned int     marked_packages;
    off_t            marked_size;

    gboolean         locked;
    progress_win_t  *progress_win;

    GDBusProxy      *proxy;
} pkgclip_t;

typedef struct _pc_pkg_t {
    char *file;
    off_t filesize;
    alpm_pkg_t *pkg;
    const char *name;
    const char *version;
    recomm_t recomm;
    reason_t reason;
    gboolean remove;
} pc_pkg_t;


#endif /* _PKGCLIP_H */
