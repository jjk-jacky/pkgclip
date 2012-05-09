/**
 * PkgClip - Copyright (C) 2012 Olivier Brunel
 *
 * main.c
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

#define _BSD_SOURCE /* for strdup w/ -std=c99 */

/* C */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>

/* pkgclip */
#include "pkgclip.h"
#include "util.h"
#include "xpm.h"

#define FREEPCPKGLIST(p)                                                    \
            do                                                              \
            {                                                               \
                alpm_list_free_inner (p, (alpm_list_fn_free) free_pc_pkg);  \
                alpm_list_free (p);                                         \
                p = NULL;                                                   \
            } while(0)

static const char *recomm_label[] = {
    "Keep",
    "Remove"
};

static const char *reason_label[] = {
    "Treated as if it was installed",
    "More recent than installed version",
    "Installed on system",
    "Recent older version (%d/%d)",
    "Older version (keep only %d old %s)",
    "Previous package release",
    "Package not installed on system (any version)"
};

static void
free_pc_pkg (pc_pkg_t *pc_pkg)
{
    free (pc_pkg->file);
    alpm_pkg_free (pc_pkg->pkg);
    free (pc_pkg);
}

static void
quit (pkgclip_t *pkgclip)
{
    if (pkgclip->proxy != NULL)
    {
        g_object_unref (pkgclip->proxy);
        pkgclip->proxy = NULL;
    }
    
    if (!pkgclip->is_loading)
    {
        gtk_main_quit ();
    }
    else
    {
        pkgclip->abort = TRUE;
    }
}

static void
rend_size (GtkTreeViewColumn *column _UNUSED_, GtkCellRenderer *renderer _UNUSED_,
    GtkTreeModel *store, GtkTreeIter *iter, col_t col)
{
    guint size;
    double new_size;
    const char *unit;
    char buf[23];
    
    gtk_tree_model_get (store, iter, col, &size, -1);
    new_size = humanize_size (size, '\0', &unit);
    snprintf (buf, 23, "%.2f %s", new_size, unit);
    g_object_set (renderer, "text", buf, NULL);
}

static void
rend_recomm (GtkTreeViewColumn *column _UNUSED_, GtkCellRenderer *renderer,
    GtkTreeModel *store, GtkTreeIter *iter, gpointer data _UNUSED_)
{
    recomm_t recomm;
    
    gtk_tree_model_get (store, iter, COL_RECOMM, &recomm, -1);
    g_object_set (renderer, "text", recomm_label[recomm], NULL);
}

static void
rend_reason (GtkTreeViewColumn *column _UNUSED_, GtkCellRenderer *renderer,
    GtkTreeModel *store, GtkTreeIter *iter, gpointer data _UNUSED_)
{
    reason_t reason;
    int nb_old_ver, nb_old_ver_total;
    
    gtk_tree_model_get (store, iter, COL_REASON, &reason, -1);
    if (reason == REASON_OLDER_VERSION)
    {
        char buf[255];
        gtk_tree_model_get (store, iter,
            COL_NB_OLD_VER,         &nb_old_ver,
            COL_NB_OLD_VER_TOTAL,   &nb_old_ver_total,
            -1);
        snprintf (buf, 255, reason_label[reason], nb_old_ver, nb_old_ver_total);
        g_object_set (renderer, "text", buf, NULL);
    }
    else if (reason == REASON_ALREADY_OLDER_VERSION)
    {
        char buf[255];
        gtk_tree_model_get (store, iter,
            COL_NB_OLD_VER_TOTAL,   &nb_old_ver_total,
            -1);
        snprintf (buf, 255, reason_label[reason], nb_old_ver_total,
            (nb_old_ver_total > 1) ? "versions" : "version");
        g_object_set (renderer, "text", buf, NULL);
    }
    else
    {
        g_object_set (renderer, "text", reason_label[reason], NULL);
    }
}

static int
init_alpm (pkgclip_t *pkgclip)
{
    enum _alpm_errno_t err;
    
	pkgclip->handle = alpm_initialize(pkgclip->rootpath, pkgclip->dbpath, &err);
	if (!pkgclip->handle)
    {
        show_error ("Failed to initialize alpm library", alpm_strerror (err),
            pkgclip);
		return -1;
	}
    
    alpm_list_t *i;
    for (i = pkgclip->cachedirs; i; i = alpm_list_next (i))
    {
        alpm_option_add_cachedir (pkgclip->handle, i->data);
    }
    
    return 0;
}

static int
pc_pkg_cmp (const pc_pkg_t *pkg1, const pc_pkg_t *pkg2)
{
    int ret;
    
    ret = strcmp (pkg1->name, pkg2->name);
    if (ret == 0)
    {
        /* same package, compare version */
        /* when ASC, we want pkg-2.0 first, then pkg-1.0 -- that way the most
         * recent versions are first */
        ret = 0 - alpm_pkg_vercmp (pkg1->version, pkg2->version);
    }
    return ret;
}

static void
set_locked (gboolean locked, pkgclip_t *pkgclip)
{
    pkgclip->locked = locked;
    gtk_widget_set_sensitive (pkgclip->button, !locked);
    gtk_widget_set_sensitive (pkgclip->mnu_reload, !locked);
    gtk_widget_set_sensitive (pkgclip->mnu_edit, !locked);
}

static void
update_label (pkgclip_t *pkgclip)
{
    char buf[255];
    double total_size, marked_size;
    const char *total_unit, *marked_unit;

    total_size = humanize_size (pkgclip->total_size, '\0', &total_unit);
    marked_size = humanize_size (pkgclip->marked_size, '\0', &marked_unit);
    snprintf (buf, 255, "Total: %d packages (%.2f %s) \t To be removed: %d packages (%.2f %s)",
        pkgclip->total_packages, total_size, total_unit,
        pkgclip->marked_packages, marked_size, marked_unit);
    gtk_label_set_text (GTK_LABEL (pkgclip->label), buf);
    
    gtk_widget_set_sensitive (pkgclip->button,
        (!pkgclip->locked && pkgclip->marked_packages > 0));
}

static void
clear_packages (gboolean full, pkgclip_t *pkgclip)
{
    if (full)
    {
        FREEPCPKGLIST (pkgclip->packages);
        pkgclip->total_packages = 0;
        pkgclip->total_size = 0;
    }
    pkgclip->marked_packages = 0;
    pkgclip->marked_size = 0;
    gtk_list_store_clear (pkgclip->store);
    update_label (pkgclip);
}

static void
refresh_list (gboolean from_reloading, pkgclip_t *pkgclip)
{
    if (!from_reloading)
    {
        set_locked (TRUE, pkgclip);
        pkgclip->is_loading = TRUE;
        clear_packages (FALSE, pkgclip);
    }
    
    gtk_label_set_text (GTK_LABEL (pkgclip->label), "Refreshing list; Please wait...");
    
    GtkTreeIter iter;
    alpm_list_t *i;
    alpm_db_t *db_local = alpm_option_get_localdb (pkgclip->handle);
    const char *last_pkg = NULL;
    const char *inst_ver = NULL;
    int old_ver, nb_old_ver;
    char *pkgrel = NULL;
    int is_installed = 0;
    
    for (i = pkgclip->packages; i; i = alpm_list_next (i))
    {
        pc_pkg_t *pc_pkg = i->data;
        
        /* is this a new package? */
        if (NULL == last_pkg || strcmp (last_pkg, pc_pkg->name) != 0)
        {
            last_pkg = pc_pkg->name;
            old_ver = 0;
            nb_old_ver = pkgclip->nb_old_ver;
            
            /* is it installed? */
            alpm_pkg_t *pkg = alpm_db_get_pkg (db_local, pc_pkg->name);
            if (NULL != pkg)
            {
                is_installed = 1;
                inst_ver = alpm_pkg_get_version (pkg);
                pkgrel = strrchr(inst_ver, '-');
            }
            else
            {
                is_installed = 0;
                inst_ver = NULL;
                pkgrel = NULL;
            }
        }
        
        if (is_installed > 0)
        {
            int cmp = alpm_pkg_vercmp (pc_pkg->version, inst_ver);
            if (cmp == 0)
            {
                /* installed version */
                pc_pkg->reason = REASON_INSTALLED;
            }
            else if (cmp < 0)
            {
                /* older version, we only keep a certain amount */
                ++old_ver;
                /* not yet reached? */
                if (old_ver <= nb_old_ver)
                {
                    /* but: should we check if it's not just an old pkgrel? */
                    if (pkgclip->old_pkgrel)
                    {
                        char *s = strrchr(pc_pkg->version, '-');
                        if (s)
                        {
                            /* "remove" the pkgrel from version */
                            *s = '\0';
                            /* "remove" the pkgrel from installed version */
                            if (pkgrel)
                            {
                                *pkgrel = '\0';
                            }
                            /* are they the same? */
                            if (alpm_pkg_vercmp (pc_pkg->version, inst_ver) == 0)
                            {
                                /* same version, older pkgrel */
                                --old_ver;
                                pc_pkg->reason = REASON_OLDER_PKGREL;
                            }
                            else
                            {
                                /* older version */
                                pc_pkg->reason = REASON_OLDER_VERSION;
                            }
                            /* restore */
                            *s = '-';
                            if (pkgrel)
                            {
                                *pkgrel = '-';
                            }
                        }
                        else
                        {
                            /* no pkgrel, so it is an older version */
                            pc_pkg->reason = REASON_OLDER_VERSION;
                        }
                    }
                    else
                    {
                        /* older version */
                        pc_pkg->reason = REASON_OLDER_VERSION;
                    }
                }
                else
                {
                    /* we already have our stock of old versions */
                    pc_pkg->reason = REASON_ALREADY_OLDER_VERSION;
                }
            }
            else
            {
                /* newer than installed */
                pc_pkg->reason = REASON_NEWER_THAN_INSTALLED;
            }
        }
        else if (NULL != alpm_list_find_str (pkgclip->as_installed, pc_pkg->name))
        {
            /* treat as if installed */
            pc_pkg->reason = REASON_AS_INSTALLED;
            is_installed = 2;
            nb_old_ver = pkgclip->nb_old_ver_ai;
            inst_ver = pc_pkg->version;
            pkgrel = strrchr(inst_ver, '-');
        }
        else
        {
            /* no such package (any version) installed */
            pc_pkg->reason = REASON_PKG_NOT_INSTALLED;
        }
        
        /* set recomm */
        pc_pkg->recomm = pkgclip->recomm[pc_pkg->reason];
        
        if (pc_pkg->recomm == RECOMM_REMOVE)
        {
            pc_pkg->remove = TRUE;
            ++(pkgclip->marked_packages);
            pkgclip->marked_size += pc_pkg->filesize;
        }
        else
        {
            pc_pkg->remove = FALSE;
        }
        
        gtk_list_store_append (pkgclip->store, &iter);
        gtk_list_store_set (pkgclip->store, &iter,
            COL_PC_PKG,             pc_pkg,
            COL_PACKAGE,            pc_pkg->name,
            COL_VERSION,            pc_pkg->version,
            COL_SIZE,               pc_pkg->filesize,
            COL_RECOMM,             pc_pkg->recomm,
            COL_REMOVE,             pc_pkg->remove,
            COL_REASON,             pc_pkg->reason,
            COL_NB_OLD_VER,         old_ver,
            COL_NB_OLD_VER_TOTAL,   nb_old_ver,
            -1);
    }

    if (!from_reloading)
    {
        pkgclip->is_loading = FALSE;
        set_locked (FALSE, pkgclip);
        update_label (pkgclip);
    }
}

static int
reload_list (pkgclip_t *pkgclip)
{
    char buf[255];
    int ret = 0;
    alpm_list_t *cachedirs = alpm_option_get_cachedirs (pkgclip->handle);
    alpm_list_t *i;
    
    clear_packages (TRUE, pkgclip);
    set_locked (TRUE, pkgclip);
    pkgclip->is_loading = TRUE;
    
    for (i = cachedirs; i; i = alpm_list_next (i))
    {
		const char *cachedir = i->data;
		DIR *dir = opendir (cachedir);
		struct dirent *ent;

		if (dir == NULL)
        {
            snprintf (buf, 255, "Could not access cache directory %s", cachedir);
            show_error (buf, "Other cache directories (if any) will still be processed.", pkgclip);
			++ret;
			continue;
		}

		rewinddir (dir);
		/* step through the directory one file at a time */
		while ((ent = readdir(dir)) != NULL)
        {
			char path[PATH_MAX];
			size_t pathlen;
            off_t filesize = 0;
            struct stat statbuf;
			alpm_pkg_t *pkg = NULL;

			if (strcmp (ent->d_name, ".") == 0 || strcmp (ent->d_name, "..") == 0)
            {
				continue;
			}
			/* build the full filepath */
			snprintf (path, PATH_MAX, "%s%s", cachedir, ent->d_name);

			/* we handle .sig files with packages, not separately */
			pathlen = strlen (path);
			if (strcmp (path + pathlen - 4, ".sig") == 0)
            {
				continue;
			}

			/* attempt to load the package (just the metadata) to ensure it's
             * a valid package. */
			if (alpm_pkg_load (pkgclip->handle, path, 0, 0, &pkg) != 0
                    || pkg == NULL)
            {
                if (pkg)
                {
                    alpm_pkg_free (pkg);
                }
				continue;
			}
            ++(pkgclip->total_packages);
            
            /* get file size */
            if (stat (path, &statbuf) == 0)
            {
                filesize = statbuf.st_size;
                pkgclip->total_size += filesize;
            }
            
            /* label */
            if (!pkgclip->abort && pkgclip->total_packages % 10 == 0)
            {
                snprintf (buf, 255, "Loading packages (%d); Please wait...",
                    pkgclip->total_packages);
                gtk_label_set_text (GTK_LABEL (pkgclip->label), buf);
            }
            
            /* new pc_pkg */
            pc_pkg_t *pc_pkg;
            pc_pkg = calloc (1, sizeof (*pc_pkg));
            pc_pkg->file = strdup (path);
            pc_pkg->filesize = filesize;
            pc_pkg->pkg = pkg;
            pc_pkg->name = alpm_pkg_get_name (pkg);
            pc_pkg->version = alpm_pkg_get_version (pkg);
            /* add it, sorted */
            pkgclip->packages = alpm_list_add_sorted (pkgclip->packages, pc_pkg,
                (alpm_list_fn_cmp) pc_pkg_cmp);
            
            if (pkgclip->abort)
            {
                closedir (dir);
                if (pkgclip->in_gtk_main)
                {
                    gtk_main_quit ();
                }
                return -1;
            }
            else
            {
                gtk_main_iteration_do (FALSE);
            }
		}
		closedir (dir);
	}
    
    refresh_list (TRUE, pkgclip);
    
    pkgclip->is_loading = FALSE;
    set_locked (FALSE, pkgclip);
    update_label (pkgclip);
    return ret;
}

static void
window_destroy_cb (GtkWidget *window _UNUSED_, pkgclip_t *pkgclip)
{
    quit (pkgclip);
}

static gint
list_sort_package (GtkTreeModel *model, GtkTreeIter *iter1, GtkTreeIter *iter2,
                   gpointer data _UNUSED_)
{
    pc_pkg_t *pc_pkg1;
    pc_pkg_t *pc_pkg2;
    
    gtk_tree_model_get (model, iter1, COL_PC_PKG, &pc_pkg1, -1);
    gtk_tree_model_get (model, iter2, COL_PC_PKG, &pc_pkg2, -1);
    
    return pc_pkg_cmp (pc_pkg1, pc_pkg2);
}

static void
column_clicked_cb (GtkTreeViewColumn *column, pkgclip_t *pkgclip)
{
    if (pkgclip->sane_sort_indicator)
    {
        /* reverse the sort indicator, because when DESCending we should point to
         * the bottom, not the top; and vice versa */
        gtk_tree_view_column_set_sort_order (column,
            !gtk_tree_view_column_get_sort_order (column));
    }
}

static void
renderer_toggle_cb (GtkCellRendererToggle *renderer _UNUSED_, gchar *path, pkgclip_t *pkgclip)
{
    GtkTreeModel *store = GTK_TREE_MODEL (pkgclip->store);
    GtkTreeIter iter;
    pc_pkg_t *pc_pkg;
    
    if (pkgclip->locked)
        return;
    
    gtk_tree_model_get_iter_from_string (store, &iter, path);
    gtk_tree_model_get (store, &iter, COL_PC_PKG, &pc_pkg, -1);
    pc_pkg->remove = !pc_pkg->remove;
    if (pc_pkg->remove)
    {
        ++(pkgclip->marked_packages);
        pkgclip->marked_size += pc_pkg->filesize;
    }
    else
    {
        --(pkgclip->marked_packages);
        pkgclip->marked_size -= pc_pkg->filesize;
    }
    gtk_list_store_set (GTK_LIST_STORE (store), &iter, COL_REMOVE, pc_pkg->remove, -1);
    update_label (pkgclip);
}

static gboolean
list_query_tooltip_cb (GtkWidget *widget, gint x, gint y, gboolean keyboard _UNUSED_,
                       GtkTooltip *tooltip, pkgclip_t *pkgclip)
{
    gint bx, by;
    GtkTreeView *treeview = GTK_TREE_VIEW (widget);
    GtkTreeModel *model = GTK_TREE_MODEL (pkgclip->store);
    GtkTreePath *path;
    GtkTreeViewColumn *column;
    GtkTreeIter iter;
    gboolean ret = FALSE;
    
    gtk_tree_view_convert_widget_to_bin_window_coords (treeview, x, y, &bx, &by);
    if (gtk_tree_view_get_path_at_pos (treeview, bx, by, &path,
            &column, NULL, NULL))
    {
        if (gtk_tree_model_get_iter (model, &iter, path))
        {
            pc_pkg_t *pc_pkg;
            
            off_t size;
            char buf[128];
            size_t l, i;
            char *s;
            
            gint col = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (column), "col-id"));
            
            switch (col)
            {
                case COL_PACKAGE:
                case COL_VERSION:
                    gtk_tree_model_get (model, &iter, COL_PC_PKG, &pc_pkg, -1);
                    gtk_tooltip_set_text (tooltip, pc_pkg->file);
                    ret = TRUE;
                    break;
                
                case COL_SIZE:
                    gtk_tree_model_get (model, &iter, col, &size, -1);
                    snprintf (buf, 128, "%d", (unsigned int) size);
                    /* add thousand sep */
                    l = strlen (buf);
                    /* point to the future end of the string */
                    s = buf + l + (unsigned int)(l / 3);
                    /* when a multiple of 3, that's one less space (6 char. == 1 space) */
                    if (l % 3 == 0)
                    {
                        --s;
                    }
                    *s-- = '\0';
                    for (i = l - 1; i > 0; --i)
                    {
                        *s-- = buf[i];
                        if ((l - i) % 3 == 0)
                        {
                            *s-- = ' ';
                        }
                    }
                    strcat (buf, " B");
                    gtk_tooltip_set_text (tooltip, buf);
                    ret = TRUE;
                    break;
            }
        }
        gtk_tree_path_free (path);
    }
    return ret;
}

static void
show_progress_window (pkgclip_t *pkgclip)
{
    progress_win_t *progress_win;
    progress_win = calloc (1, sizeof (*(pkgclip->progress_win)));
    
    /* the window */
    GtkWidget *window;
    window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    progress_win->window = window;
    gtk_window_set_transient_for (GTK_WINDOW(window), GTK_WINDOW(pkgclip->window));
    gtk_window_set_destroy_with_parent (GTK_WINDOW(window), TRUE);
    gtk_window_set_decorated (GTK_WINDOW (window), FALSE);
    gtk_window_set_resizable (GTK_WINDOW (window), FALSE);
    gtk_window_set_modal (GTK_WINDOW (window), TRUE);
    gtk_container_set_border_width (GTK_CONTAINER (window), 5);
    gtk_window_set_skip_taskbar_hint (GTK_WINDOW(window), TRUE);
    gtk_window_set_skip_pager_hint (GTK_WINDOW(window), TRUE);
    gtk_window_set_has_resize_grip (GTK_WINDOW (window), FALSE);
    gtk_widget_set_size_request (window, 420, -1);
    
    /* everything in a vbox */
    GtkWidget *vbox;
    vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add (GTK_CONTAINER (window), vbox);
    gtk_widget_show (vbox);
    
    /* label */
    GtkWidget *label;
    label = gtk_label_new ("Removing packages; Please wait...");
    progress_win->label = label;
    gtk_box_pack_start (GTK_BOX(vbox), label, FALSE, FALSE, 0);
    gtk_widget_show (label);

    /* progress bar */
    GtkWidget *pbar;
    pbar = gtk_progress_bar_new ();
    progress_win->pbar = pbar;
    gtk_box_pack_start (GTK_BOX(vbox), pbar, TRUE, TRUE, 0);
    gtk_widget_show (pbar);
    
    /* done */
    gtk_widget_show (window);
    pkgclip->progress_win = progress_win;
}

static void
show_results (guint processed _UNUSED_, pkgclip_t *pkgclip)
{
    GtkMessageType type;
    const char *title;
    char subtitle[1024];
    
    double success_size;
    const char *success_unit;
    success_size = humanize_size (pkgclip->progress_win->success_size, '\0', &success_unit);
    
    if (pkgclip->progress_win->error_packages == 0)
    {
        /* no errors */
        type = GTK_MESSAGE_INFO;
        title = "Packages removed!";
        snprintf (subtitle, 1024, "%d packages have been removed (%.2f %s).",
            pkgclip->progress_win->success_packages, success_size, success_unit);
    }
    else
    {
        /* errors */
        double error_size;
        const char *error_unit;
        error_size = humanize_size (pkgclip->progress_win->error_size, '\0', &error_unit);
        
        type = GTK_MESSAGE_WARNING;
        title = "Packages removed! Some errors occurred.";
        snprintf (subtitle, 1024, "%d packages have been removed (%.2f %s); "
            "%d packages could not be removed (%.2f %s).",
            pkgclip->progress_win->success_packages, success_size, success_unit,
            pkgclip->progress_win->error_packages, error_size, error_unit);
    }
    
    /* the window */
    GtkWidget *dialog;
    dialog = gtk_message_dialog_new (
                    GTK_WINDOW (pkgclip->window),
                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                    type,
                    GTK_BUTTONS_CLOSE,
                    title);
    gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
        subtitle);
    gtk_window_set_decorated (GTK_WINDOW (dialog), FALSE);
    
    /* details */
    if (pkgclip->progress_win->error_packages > 0)
    {
        GtkWidget *vbox;
        vbox = gtk_message_dialog_get_message_area (GTK_MESSAGE_DIALOG (dialog));
        
        GtkWidget *expander;
        expander = gtk_expander_new ("Error messages");
        gtk_box_pack_start (GTK_BOX (vbox), expander, TRUE, TRUE, 0);
        gtk_widget_show (expander);
        
        GtkWidget *scrolled;
        scrolled = gtk_scrolled_window_new (NULL, NULL);
        gtk_container_add (GTK_CONTAINER (expander), scrolled);
        gtk_widget_show (scrolled);
        
        GtkWidget *label;
        label = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL (label), pkgclip->progress_win->error_messages);
        gtk_label_set_selectable (GTK_LABEL (label), TRUE);
        gtk_scrolled_window_add_with_viewport (GTK_SCROLLED_WINDOW(scrolled),
            label);
        gtk_widget_show (label);
    }
    
    /* done */
    gtk_dialog_run (GTK_DIALOG (dialog));
    gtk_widget_destroy (dialog);
    
    /* free */
    free (pkgclip->progress_win);
    pkgclip->progress_win = NULL;
}

static void
on_signal (GDBusProxy *proxy _UNUSED_,
           gchar      *sender_name _UNUSED_,
           gchar      *signal_name,
           GVariant   *parameters,
           pkgclip_t  *pkgclip)
{
    const gchar *pkg_name;
    const gchar *error;
    gboolean is_success;
    
    if (g_strcmp0 (signal_name, "RemoveSuccess") == 0)
    {
        is_success = TRUE;
        ++(pkgclip->progress_win->success_packages);
        g_variant_get (parameters, "(s)", &pkg_name);
    }
    else if (g_strcmp0 (signal_name, "RemoveFailure") == 0)
    {
        is_success = FALSE;
        ++(pkgclip->progress_win->error_packages);
        g_variant_get (parameters, "(ss)", &pkg_name, &error);
        
        gchar buf[1024];
        size_t len;
        snprintf (buf, 1024, "%s:\n<span color=\"red\">%s</span>\n\n", pkg_name, error);
        len = strlen (buf);
        
        if (pkgclip->progress_win->em_len + len + 1 >= pkgclip->progress_win->em_alloc)
        {
            pkgclip->progress_win->em_alloc += (guint) len + 1024;
            pkgclip->progress_win->error_messages = (char *) realloc (
                pkgclip->progress_win->error_messages,
                pkgclip->progress_win->em_alloc * sizeof (char *));
            /* if we've just init it, we need to zero it so strcat works fine */
            if (pkgclip->progress_win->em_alloc == len + 1024)
            {
                *(pkgclip->progress_win->error_messages) = '\0';
            }
        }
        strcat (pkgclip->progress_win->error_messages, buf);
        pkgclip->progress_win->em_len += (guint) len;
    }
    else
    {
        return;
    }
    
    gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (pkgclip->progress_win->pbar),
        (double) (pkgclip->progress_win->success_packages
                  + pkgclip->progress_win->error_packages)
                / pkgclip->marked_packages);
    
    GtkTreeModel *model = GTK_TREE_MODEL (pkgclip->store);
    GtkTreeIter iter;
    pc_pkg_t *pc_pkg;
    
    if (!gtk_tree_model_get_iter_first (model, &iter))
    {
        return;
    }
    
    while (1)
    {
        gtk_tree_model_get (model, &iter, COL_PC_PKG, &pc_pkg, -1);
        if (strcmp (pkg_name, pc_pkg->file) == 0)
        {
            if (is_success)
            {
                pkgclip->progress_win->success_size += pc_pkg->filesize;
                /* remove pc_pkg from list of packages */
                alpm_list_t *i;
                for (i = pkgclip->packages; i; i = alpm_list_next (i))
                {
                    if (i->data == pc_pkg)
                    {
                        pkgclip->packages = alpm_list_remove_item (pkgclip->packages, i);
                        break;
                    }
                }
                /* remove from store */
                gtk_list_store_remove (GTK_LIST_STORE (model), &iter);
                /* update counters */
                --(pkgclip->total_packages);
                pkgclip->total_size -= pc_pkg->filesize;
                --(pkgclip->marked_packages);
                pkgclip->marked_size -= pc_pkg->filesize;
                /* free memory */
                free_pc_pkg (pc_pkg);
            }
            else
            {
                pkgclip->progress_win->error_size += pc_pkg->filesize;
            }
            break;
        }
        if (!gtk_tree_model_iter_next (model, &iter))
        {
            break;
        }
    }
}

static void
dbus_method_cb (GObject *source _UNUSED_, GAsyncResult *result, pkgclip_t *pkgclip)
{
    GError *error = NULL;
    GVariant *ret;
    
    gtk_widget_destroy (pkgclip->progress_win->window);
    
    set_locked (FALSE, pkgclip);
    update_label (pkgclip);
    
    ret = g_dbus_proxy_call_finish (pkgclip->proxy, result, &error);
    if (ret == NULL)
    {
        show_error ("Unable to remove packages", error->message, pkgclip);
        g_error_free (error);
        return;
    }
    
    /* to update reasons w/ new list of packages */
    refresh_list (FALSE, pkgclip);
    
    guint processed;
    g_variant_get (ret, "(i)", &processed);
    show_results (processed, pkgclip);
}

static void
select_prev_next_marked (gboolean next, pkgclip_t *pkgclip)
{
    GtkTreeView      *tree = GTK_TREE_VIEW (pkgclip->list);
    GtkTreeSelection *selection = gtk_tree_view_get_selection (tree);
    GtkTreeModel     *model = GTK_TREE_MODEL (pkgclip->store);
    GList            *list;
    GtkTreeIter       iter;
    gboolean          marked;
    gboolean        (*select_fn) (GtkTreeModel *model, GtkTreeIter *iter);
    GtkTreePath      *path;
    
    list = gtk_tree_selection_get_selected_rows (selection, NULL);
    if (list && list->data)
    {
        /* get GtkTreeIter from GtkTreePath */
        if (!gtk_tree_model_get_iter (model, &iter, list->data))
        {
            goto clean;
        }
    }
    else
    {
        /* no selection */
        if (next)
        {
            /* get the first visible item as starting point */
            if (!gtk_tree_view_get_visible_range (tree, &path, NULL))
            {
                goto clean;
            }
        }
        else
        {
            /* get the last visible item as starting point */
            if (!gtk_tree_view_get_visible_range (tree, NULL, &path))
            {
                goto clean;
            }
        }
        gtk_tree_model_get_iter (model, &iter, path);
        gtk_tree_path_free (path);
    }
    select_fn = (next) ? gtk_tree_model_iter_next : gtk_tree_model_iter_previous;
    while (select_fn (model, &iter))
    {
        gtk_tree_model_get (model, &iter, COL_REMOVE, &marked, -1);
        if (marked)
        {
            path = gtk_tree_model_get_path (model, &iter);
            gtk_tree_selection_unselect_all (selection);
            gtk_tree_selection_select_path (selection, path);
            gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW (pkgclip->list),
                                          path, NULL, TRUE, 0.5, 0.0);
            
            
            gtk_tree_path_free (path);
            goto clean;
        }
    }
    gtk_tree_selection_unselect_all (selection);
clean:
    g_list_free_full (list, (GDestroyNotify) gtk_tree_path_free);
}

static void
btn_prev_cb (GtkButton *button _UNUSED_, pkgclip_t *pkgclip)
{
    select_prev_next_marked (FALSE, pkgclip);
}

static void
btn_next_cb (GtkButton *button _UNUSED_, pkgclip_t *pkgclip)
{
    select_prev_next_marked (TRUE, pkgclip);
}

static void
btn_remove_cb (GtkButton *button _UNUSED_, pkgclip_t *pkgclip)
{
    GError *error = NULL;
    alpm_list_t *i;
    char buf[255];
    double size;
    const char *unit;
    
    size = humanize_size (pkgclip->marked_size, '\0', &unit);
    snprintf (buf, 255, "%d packages are marked for removal (%.2f %s)",
        pkgclip->marked_packages, size, unit);
    if (!confirm ("Are you sure you want to remove all marked packages ?",
                  buf,
                  "Remove packages", GTK_STOCK_DELETE,
                  NULL, NULL,
                  pkgclip))
    {
        return;
    }
    
    set_locked (TRUE, pkgclip);
    
    if (pkgclip->proxy == NULL)
    {
        pkgclip->proxy = g_dbus_proxy_new_for_bus_sync (
                            G_BUS_TYPE_SYSTEM,
                            G_DBUS_PROXY_FLAGS_NONE,
                            NULL,
                            "org.jjk.PkgClip",
                            "/org/jjk/PkgClip/Clipper",
                            "org.jjk.PkgClip.ClipperInterface",
                            NULL,
                            &error);
        if (pkgclip->proxy == NULL)
        {
            snprintf (buf, 255, "Error creating proxy: %s\n", error->message);
            show_error ("Cannot remove packages: unable to init DBus", buf, pkgclip);
            g_error_free (error);
            set_locked (FALSE, pkgclip);
            return;
        }
        
        g_signal_connect (pkgclip->proxy,
                          "g-signal",
                          G_CALLBACK (on_signal),
                          (gpointer) pkgclip);
    }
    
    GVariantBuilder *builder;
    
    builder = g_variant_builder_new (G_VARIANT_TYPE ("as"));
    for (i = pkgclip->packages; i; i = alpm_list_next (i))
    {
        pc_pkg_t *pc_pkg = i->data;
        
        if (pc_pkg->remove)
        {
            g_variant_builder_add (builder, "s", pc_pkg->file);
        }
    }
    
    show_progress_window (pkgclip);
    
    g_dbus_proxy_call (pkgclip->proxy,
                       "RemovePackages",
                       g_variant_new ("(as)", builder),
                       G_DBUS_CALL_FLAGS_NONE,
                       -1,
                       NULL,
                       (GAsyncReadyCallback) dbus_method_cb,
                       (gpointer) pkgclip);
    g_variant_builder_unref (builder);
}

static void
menu_reload_cb (GtkMenuItem *menuitem _UNUSED_, pkgclip_t *pkgclip)
{
    reload_list (pkgclip);
}

static void
menu_exit_cb (GtkMenuItem *menuitem _UNUSED_ , pkgclip_t *pkgclip)
{
    quit (pkgclip);
}

static void
change_marked_selection_foreach (GtkTreeModel *model,
                                 GtkTreePath *path _UNUSED_,
                                 GtkTreeIter *iter,
                                 void *ptr[2])
{
    gboolean must_be = ((mark_t) ptr[0] == MARK_SELECTION);
    pkgclip_t *pkgclip = (pkgclip_t *) ptr[1];
    pc_pkg_t *pc_pkg;
    
    gtk_tree_model_get (model, iter, COL_PC_PKG, &pc_pkg, -1);
    if (pc_pkg->remove != must_be)
    {
        pc_pkg->remove = must_be;
        if (pc_pkg->remove)
        {
            ++(pkgclip->marked_packages);
            pkgclip->marked_size += pc_pkg->filesize;
        }
        else
        {
            --(pkgclip->marked_packages);
            pkgclip->marked_size -= pc_pkg->filesize;
        }
        gtk_list_store_set (GTK_LIST_STORE (model), iter, COL_REMOVE, pc_pkg->remove, -1);
    }
}

static void
change_marked (mark_t marked, gint reason, pkgclip_t *pkgclip)
{
    GtkTreeModel *model = GTK_TREE_MODEL (pkgclip->store);
    GtkTreeIter iter;
    pc_pkg_t *pc_pkg;
    gboolean must_be;
    
    if (marked == MARK_SELECTION || marked == UNMARK_SELECTION)
    {
        void *ptr[2] = {(void *) marked, (void *) pkgclip};
        
        gtk_tree_selection_selected_foreach (gtk_tree_view_get_selection (
            GTK_TREE_VIEW (pkgclip->list)),
            (GtkTreeSelectionForeachFunc) change_marked_selection_foreach,
            (gpointer) ptr);
        update_label (pkgclip);
        return;
    }
    else
    {
        if (!gtk_tree_model_get_iter_first (model, &iter))
        {
            return;
        }
        
        while (1)
        {
            gint r;
            gtk_tree_model_get (model, &iter, COL_PC_PKG, &pc_pkg, COL_REASON, &r, -1);
            if (marked == SELECT_RESTORE)
            {
                must_be = (pc_pkg->recomm == RECOMM_REMOVE);
            }
            else /* if (marked == [UN]MARL_REASON) */
            {
                if (reason == r)
                {
                    must_be = (marked == MARK_REASON);
                }
                else
                {
                    /* don't change it */
                    must_be = pc_pkg->remove;
                }
            }
            
            if (pc_pkg->remove != must_be)
            {
                pc_pkg->remove = must_be;
                if (pc_pkg->remove)
                {
                    ++(pkgclip->marked_packages);
                    pkgclip->marked_size += pc_pkg->filesize;
                }
                else
                {
                    --(pkgclip->marked_packages);
                    pkgclip->marked_size -= pc_pkg->filesize;
                }
                gtk_list_store_set (GTK_LIST_STORE (model), &iter, COL_REMOVE, pc_pkg->remove, -1);
            }
            if (!gtk_tree_model_iter_next (model, &iter))
            {
                break;
            }
        }
        update_label (pkgclip);
    }
}

static void
menu_select_all_cb (GtkMenuItem *menuitem _UNUSED_, pkgclip_t *pkgclip)
{
    gtk_tree_selection_select_all (gtk_tree_view_get_selection (
        GTK_TREE_VIEW (pkgclip->list)));
}

static void
menu_unselect_all_cb (GtkMenuItem *menuitem _UNUSED_, pkgclip_t *pkgclip)
{
    gtk_tree_selection_unselect_all (gtk_tree_view_get_selection (
        GTK_TREE_VIEW (pkgclip->list)));
}

static void
menu_select_all_reason_cb (GtkMenuItem *menuitem, pkgclip_t *pkgclip)
{
    gint reason = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (menuitem), "reason"));
    change_marked (MARK_REASON, reason, pkgclip);
}

static void
menu_unselect_all_reason_cb (GtkMenuItem *menuitem, pkgclip_t *pkgclip)
{
    gint reason = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (menuitem), "reason"));
    change_marked (UNMARK_REASON, reason, pkgclip);
}

static void
menu_mark_selection_cb (GtkMenuItem *menuitem _UNUSED_, pkgclip_t *pkgclip)
{
    change_marked (MARK_SELECTION, 0, pkgclip);
}

static void
menu_unmark_selection_cb (GtkMenuItem *menuitem _UNUSED_, pkgclip_t *pkgclip)
{
    change_marked (UNMARK_SELECTION, 0, pkgclip);
}

static void
menu_restore_recomm_cb (GtkMenuItem *menuitem _UNUSED_, pkgclip_t *pkgclip)
{
    change_marked (SELECT_RESTORE, 0, pkgclip);
}

static void
change_as_installed_selection_foreach (GtkTreeModel *model,
                                 GtkTreePath *path _UNUSED_,
                                 GtkTreeIter *iter,
                                 void *ptr[3])
{
    gboolean adding = GPOINTER_TO_INT (ptr[0]);
    gboolean *changed = (gboolean *) ptr[1];
    pkgclip_t *pkgclip = (pkgclip_t *) ptr[2];
    pc_pkg_t *pc_pkg;
    alpm_list_t *i, *item = NULL;
    
    gtk_tree_model_get (model, iter, COL_PC_PKG, &pc_pkg, -1);
    /* search for the item in list. we do this "manually" because we need to
     * have the item, and alpm_find_* functions return the given data... */
    for (i = pkgclip->as_installed; i; i = alpm_list_next (i))
    {
        if (strcmp (i->data, pc_pkg->name) == 0)
        {
            item = i;
            break;
        }
    }
    if (item == NULL)
    {
        if (adding)
        {
            pkgclip->as_installed = alpm_list_add (pkgclip->as_installed,
                strdup (pc_pkg->name));
            *changed = TRUE;
        }
    }
    else if (!adding)
    {
        pkgclip->as_installed = alpm_list_remove_item (pkgclip->as_installed,
            item);
        free (item->data);
        free (item);
        *changed = TRUE;
    }

}

static void
change_as_installed (gboolean adding, pkgclip_t *pkgclip)
{
    gboolean changed = FALSE;
    void *ptr[3] = {GINT_TO_POINTER (adding), (void *) &changed, (void *) pkgclip};
    
    gtk_tree_selection_selected_foreach (gtk_tree_view_get_selection (
        GTK_TREE_VIEW (pkgclip->list)),
        (GtkTreeSelectionForeachFunc) change_as_installed_selection_foreach,
        (gpointer) ptr);
    if (changed)
    {
        refresh_list (FALSE, pkgclip);
        if (save_config (pkgclip))
        {
            gtk_label_set_text (GTK_LABEL (pkgclip->label), "Preferences saved.");
        }
    }
}

static void
menu_add_as_installed_selection_cb (GtkMenuItem *menuitem _UNUSED_, pkgclip_t *pkgclip)
{
    change_as_installed (TRUE, pkgclip);
}

static void
menu_remove_as_installed_selection_cb (GtkMenuItem *menuitem _UNUSED_, pkgclip_t *pkgclip)
{
    change_as_installed (FALSE, pkgclip);
}

static GtkLabel *pkgclip_label;

static void
menu_select_cb (GtkMenuItem *menuitem _UNUSED_, const char *text)
{
    gtk_label_set_text (pkgclip_label, text);
}

static void
menu_deselect_cb (GtkMenuItem *menuitem _UNUSED_, pkgclip_t *pkgclip)
{
    update_label (pkgclip);
}

static gboolean
btn_enter_cb (GtkWidget *widget _UNUSED_, GdkEvent *event _UNUSED_, const char *text)
{
    gtk_label_set_text (pkgclip_label, text);
    return FALSE;
}

static gboolean
btn_leave_cb (GtkWidget *widget _UNUSED_, GdkEvent *event _UNUSED_, pkgclip_t *pkgclip)
{
    update_label (pkgclip);
    return FALSE;
}

static void
prefs_btn_ok_cb (GtkButton *button, pkgclip_t *pkgclip)
{
    gboolean needs_reload  = FALSE;
    gboolean needs_refresh = FALSE;
    gboolean needs_save    = FALSE;
    
    gtk_widget_hide (pkgclip->prefs->window);
    
    char *s;
    int btn_id, i;
    gboolean is_on;
    GtkTreeIter iter;
    
    btn_id = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (button), "btn-id"));
    
    /* OK */
    if (btn_id == 1)
    {
        s = gtk_file_chooser_get_filename (
            GTK_FILE_CHOOSER (pkgclip->prefs->filechooser));
        if (g_strcmp0 (s, pkgclip->pacmanconf) != 0)
        {
            /* make sure we can open the file */
            FILE *fp;
            char file[PATH_MAX];
            snprintf (file, PATH_MAX, "%s/pacman.conf", s);
            fp = fopen (file, "r");
            if (NULL == fp)
            {
                /* we actually allow file to not exists */
                if (errno != ENOENT)
                {
                    show_error ("Unable to open pacman.conf file", file, pkgclip);
                    g_free (s);
                    gtk_widget_show (pkgclip->prefs->window);
                    return;
                }
            }
            else
            {
                fclose (fp);
            }
            /* ok */
            free (pkgclip->pacmanconf);
            pkgclip->pacmanconf = strdup (s);
            g_free (s);
            needs_reload = TRUE;
            needs_save = TRUE;
        }
        
        s = (gchar *) gtk_entry_get_text (GTK_ENTRY (pkgclip->prefs->entry));
        i = atoi (s);
        if (i != pkgclip->nb_old_ver)
        {
            pkgclip->nb_old_ver = i;
            needs_refresh = TRUE;
            needs_save = TRUE;
        }
        
        s = (gchar *) gtk_entry_get_text (GTK_ENTRY (pkgclip->prefs->entry_ai));
        i = atoi (s);
        if (i != pkgclip->nb_old_ver_ai)
        {
            pkgclip->nb_old_ver_ai = i;
            needs_refresh = TRUE;
            needs_save = TRUE;
        }
        
        is_on = gtk_toggle_button_get_active (
            GTK_TOGGLE_BUTTON (pkgclip->prefs->chk_old_pkgrel));
        if (is_on != pkgclip->old_pkgrel)
        {
            pkgclip->old_pkgrel = is_on;
            needs_refresh = TRUE;
            needs_save = TRUE;
        }
        
        /* WARNING: the check is the OPPOSITE of the option... */
        is_on = !gtk_toggle_button_get_active (
            GTK_TOGGLE_BUTTON (pkgclip->prefs->chk_autoload));
        if (is_on != pkgclip->autoload)
        {
            pkgclip->autoload = is_on;
            needs_save = TRUE;
        }
        
        if (pkgclip->prefs->ai_updated)
        {
            FREELIST (pkgclip->as_installed);
            if (gtk_tree_model_get_iter_first (pkgclip->prefs->model_ai, &iter))
            {
                while (1)
                {
                    gtk_tree_model_get (pkgclip->prefs->model_ai, &iter, 0, &s, -1);
                    pkgclip->as_installed = alpm_list_add (pkgclip->as_installed, strdup (s));
                    if (!gtk_tree_model_iter_next (pkgclip->prefs->model_ai, &iter))
                    {
                        break;
                    }
                }
            }
            needs_save = TRUE;
        }
    }
    /* Clear */
    else
    {
        free (pkgclip->pacmanconf);
        pkgclip->pacmanconf = strdup (PACMAN_CONF);
        pkgclip->nb_old_ver = 1;
        pkgclip->old_pkgrel = TRUE;
        pkgclip->autoload = TRUE;
        FREELIST (pkgclip->as_installed);
        pkgclip->nb_old_ver_ai = 0;

        pkgclip->recomm[REASON_NEWER_THAN_INSTALLED]    = RECOMM_KEEP;
        pkgclip->recomm[REASON_INSTALLED]               = RECOMM_KEEP;
        pkgclip->recomm[REASON_OLDER_VERSION]           = RECOMM_KEEP;
        pkgclip->recomm[REASON_ALREADY_OLDER_VERSION]   = RECOMM_REMOVE;
        pkgclip->recomm[REASON_OLDER_PKGREL]            = RECOMM_REMOVE;
        pkgclip->recomm[REASON_PKG_NOT_INSTALLED]       = RECOMM_REMOVE;
        
        pkgclip->recomm[REASON_AS_INSTALLED] = pkgclip->recomm[REASON_INSTALLED];
        
        needs_reload = TRUE;
    }
    
    if (btn_id == 1)
    {
        is_on = gtk_toggle_button_get_active (
            GTK_TOGGLE_BUTTON (pkgclip->prefs->chk_sane_sort_indicator));
    }
    else
    {
        is_on = FALSE;
    }
    if (is_on != pkgclip->sane_sort_indicator)
    {
        pkgclip->sane_sort_indicator = is_on;
        /* so we need to adjust it */
        /* okay, so i don't know how to get the GtkTreeViewColumn currently
         * sorted. we can get the column_id of the sorted column from the
         * model, but no way to link it to a column on the treeview.
         * So, we'll get that column id, and that go through each column on
         * the TV, since we actually added its linked column-id as data */
        GtkTreeViewColumn *column;
        gint c, col;
        GtkSortType order;
        gtk_tree_sortable_get_sort_column_id (GTK_TREE_SORTABLE (pkgclip->store),
            &c, &order);
        for (i = 0; 1; ++i)
        {
            /* get column */
            column = gtk_tree_view_get_column (GTK_TREE_VIEW (pkgclip->list), i);
            /* since we're always sorted, this should never happen */
            if (NULL == column)
            {
                break;
            }
            /* grab associated col-id */
            col = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (column), "col-id"));
            /* is it the one we want? */
            if (col == c)
            {
                /* reverse the indicator */
                gtk_tree_view_column_set_sort_order (column,
                    !gtk_tree_view_column_get_sort_order (column));
                break;
            }
        }
        needs_save = TRUE;
    }
    
    /* reload/refresh */
    if (needs_reload)
    {
        /* well, we need to re-set alpm, so let's clear everything */
        if (pkgclip->handle)
        {
            alpm_release (pkgclip->handle);
        }
        pkgclip->handle = NULL;
        if (NULL != pkgclip->dbpath)
        {
            free (pkgclip->dbpath);
            pkgclip->dbpath = NULL;
        }
        if (NULL != pkgclip->rootpath)
        {
            free (pkgclip->rootpath);
            pkgclip->rootpath = NULL;
        }
        if (NULL != pkgclip->cachedirs)
        {
            FREELIST (pkgclip->cachedirs);
        }
        /* now let's parse the new pacman.conf */
        parse_pacmanconf (pkgclip);
        /* re-init alpm */
        init_alpm (pkgclip);
        /* and then reload */
        reload_list (pkgclip);
    }
    else if (needs_refresh)
    {
        refresh_list (FALSE, pkgclip);
    }
    
    /* save */
    if (btn_id == 1)
    {
        if (needs_save && save_config (pkgclip))
        {
            gtk_label_set_text (GTK_LABEL (pkgclip->label), "Preferences saved.");
        }
    }
    else
    {
        char file[PATH_MAX];
        snprintf (file, PATH_MAX, "%s/.config/pkgclip.conf", g_get_home_dir ());
        if (unlink (file) == 0)
        {
            gtk_label_set_text (GTK_LABEL (pkgclip->label), "Preferences file removed.");
        }
        else
        {
            show_error ("Preferences file could not be removed.", strerror (errno), pkgclip);
        }
    }
    
    gtk_widget_destroy (pkgclip->prefs->window);
}

static void
prefs_destroy_cb (GtkWidget *window _UNUSED_, pkgclip_t *pkgclip)
{
    free (pkgclip->prefs);
    pkgclip->prefs = NULL;
}

static void
prefs_column_clicked_cb (GtkTreeViewColumn *column, GtkToggleButton *button)
{
    if (gtk_toggle_button_get_active (button))
    {
        /* reverse the sort indicator, because when DESCending we should point to
         * the bottom, not the top; and vice versa */
        gtk_tree_view_column_set_sort_order (column,
            !gtk_tree_view_column_get_sort_order (column));
    }
}

static void
prefs_sane_toggled_cb (GtkToggleButton *button _UNUSED_, GtkTreeViewColumn *column)
{
    /* reverse the sort indicator, because when DESCending we should point to
     * the bottom, not the top; and vice versa */
    gtk_tree_view_column_set_sort_order (column,
        !gtk_tree_view_column_get_sort_order (column));
}

static void
prefs_btn_list_add_cb (GtkButton *button _UNUSED_, pkgclip_t *pkgclip)
{
    GtkTreeView *tree = pkgclip->prefs->tree_ai;
    GtkTreeModel *model = gtk_tree_view_get_model (tree);
    GtkTreeIter iter;
    GtkTreePath *path;
    GtkTreeViewColumn *column;
    
    gtk_list_store_append (GTK_LIST_STORE (model), &iter);
    path = gtk_tree_model_get_path (model, &iter);
    column = gtk_tree_view_get_column (tree, 0);
    gtk_tree_view_set_cursor (tree, path, column, TRUE);
    pkgclip->prefs->ai_updated = TRUE;
}

static void
prefs_btn_list_edit_cb (GtkButton *button _UNUSED_, pkgclip_t *pkgclip)
{
    GtkTreeView *tree = pkgclip->prefs->tree_ai;
    GtkTreeSelection *selection;
    GtkTreeModel *model;
    GtkTreeIter iter;
    GtkTreePath *path;
    GtkTreeViewColumn *column;
    
    selection = gtk_tree_view_get_selection (tree);
    if (!gtk_tree_selection_get_selected (selection, &model, &iter))
    {
        return;
    }
    path = gtk_tree_model_get_path (model, &iter);
    column = gtk_tree_view_get_column (tree, 0);
    gtk_tree_view_set_cursor (tree, path, column, TRUE);
    pkgclip->prefs->ai_updated = TRUE;
}

static void
prefs_btn_list_remove_cb (GtkButton *button _UNUSED_, pkgclip_t *pkgclip)
{
    GtkTreeView *tree = pkgclip->prefs->tree_ai;
    GtkTreeSelection *selection;
    GtkTreeModel *model;
    GtkTreeIter iter;
    
    selection = gtk_tree_view_get_selection (tree);
    if (!gtk_tree_selection_get_selected (selection, &model, &iter))
    {
        return;
    }
    gtk_list_store_remove (GTK_LIST_STORE (model), &iter);
    pkgclip->prefs->ai_updated = TRUE;
}

static void
prefs_renderer_edited_cb (GtkCellRendererText *renderer _UNUSED_, gchar *path,
                          gchar *text, pkgclip_t *pkgclip)
{
    GtkTreeModel *model = pkgclip->prefs->model_ai;
    GtkTreeIter iter;
    
    if (gtk_tree_model_get_iter_from_string (model, &iter, path))
    {
        char *s;
        gtk_tree_model_get (model, &iter, 0, &s, -1);
        free (s);
        gtk_list_store_set (GTK_LIST_STORE (model), &iter, 0, text, -1);
        pkgclip->prefs->ai_updated = TRUE;
    }
}

static void
menu_preferences_cb (GtkMenuItem *menuitem _UNUSED_, pkgclip_t *pkgclip)
{
    if (NULL != pkgclip->prefs)
    {
        gtk_window_present (GTK_WINDOW (pkgclip->prefs->window));
        return;
    }
    
    pkgclip->prefs = calloc (1, sizeof (*(pkgclip->prefs)));
    
    /* the window */
    GtkWidget *window;
    window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    pkgclip->prefs->window = window;
    gtk_window_set_title (GTK_WINDOW (window), "Preferences");
    gtk_window_set_transient_for (GTK_WINDOW(window), GTK_WINDOW(pkgclip->window));
    gtk_window_set_destroy_with_parent (GTK_WINDOW(window), TRUE);
    gtk_window_set_skip_taskbar_hint (GTK_WINDOW(window), TRUE);
    gtk_window_set_skip_pager_hint (GTK_WINDOW(window), TRUE);
    gtk_container_set_border_width (GTK_CONTAINER (window), 2);
    gtk_window_set_resizable (GTK_WINDOW (window), FALSE);
    
    /* vbox */
    GtkWidget *vbox;
    vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add (GTK_CONTAINER (window), vbox);
    gtk_widget_show (vbox);
    
    /* ** options ** */
    GtkWidget *label;
    GtkWidget *check;
    
    /* grid */
    GtkWidget *grid;
    int top = 0;
    grid = gtk_grid_new ();
    gtk_box_pack_start (GTK_BOX (vbox), grid, TRUE, TRUE, 0);
    gtk_widget_show (grid);
    
    /* pacman.conf */
    label = gtk_label_new ("Location of pacman.conf:");
    gtk_grid_attach (GTK_GRID (grid), label, 0, top, 1, 1);
    gtk_widget_set_tooltip_text (label, "Define the location of the pacman.conf file");
    gtk_widget_show (label);
    
    GtkWidget *filechooser;
    filechooser = gtk_file_chooser_button_new ("Location of pacman.conf",
                    GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
    pkgclip->prefs->filechooser = filechooser;
    gtk_grid_attach (GTK_GRID (grid), filechooser, 1, top++, 1, 1);
    gtk_widget_set_tooltip_text (filechooser, "Define the location of the pacman.conf file");
    gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (filechooser), pkgclip->pacmanconf);
    gtk_widget_show (filechooser);
    
    /* nb old ver */
    label = gtk_label_new ("Number of old versions to keep:");
    gtk_grid_attach (GTK_GRID (grid), label, 0, top, 1, 1);
    gtk_widget_set_tooltip_text (label, "How many packages of older versions to keep, besides the one installed (or any newer)");
    gtk_widget_show (label);
    
    GtkWidget *combo;
    combo = gtk_combo_box_text_new_with_entry ();
    GtkWidget *entry;
    entry = gtk_bin_get_child (GTK_BIN (combo));
    pkgclip->prefs->entry = entry;
    int i;
    char buf[3];
    for (i = 0; i <= 10; ++i)
    {
        snprintf (buf, 3, "%d", i);
        gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), buf);
    }
    if (pkgclip->nb_old_ver <= 10)
    {
        gtk_combo_box_set_active (GTK_COMBO_BOX (combo), pkgclip->nb_old_ver);
    }
    else
    {
        snprintf (buf, 3, "%d", pkgclip->nb_old_ver);
        gtk_entry_set_text (GTK_ENTRY (entry), buf);
    }
    gtk_grid_attach (GTK_GRID (grid), combo, 1, top++, 1, 1);
    gtk_widget_set_tooltip_text (combo, "How many packages of older versions to keep, besides the one installed (or any newer)");
    gtk_widget_show (combo);
    
    /* old pkgrel */
    check = gtk_check_button_new_with_label ("Remove older package releases");
    pkgclip->prefs->chk_old_pkgrel = check;
    gtk_grid_attach (GTK_GRID (grid), check, 0, top++, 2, 1);
    gtk_widget_set_margin_left (check, 23);
    gtk_widget_set_tooltip_markup (check, "Remove packages for the same version but an older package release <small>(MAJOR.MINOR.REVISION-<b>PKGREL</b>)</small>.");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check), pkgclip->old_pkgrel);
    gtk_widget_show (check);
    
    /* sane sort indicator */
    check = gtk_check_button_new_with_label ("Use sane sort indicator");
    pkgclip->prefs->chk_sane_sort_indicator = check;
    gtk_grid_attach (GTK_GRID (grid), check, 0, top++, 2, 1);
    gtk_widget_set_margin_left (check, 23);
    gtk_widget_set_tooltip_text (check, "So when sorted descendingly, the arrow points down...");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check), pkgclip->sane_sort_indicator);
    gtk_widget_show (check);

    /* autoload -- WARNING: the check is the OPPOSITE of the option... */
    check = gtk_check_button_new_with_label ("Do not load packages on start");
    pkgclip->prefs->chk_autoload = check;
    gtk_grid_attach (GTK_GRID (grid), check, 0, top++, 2, 1);
    gtk_widget_set_margin_left (check, 23);
    gtk_widget_set_tooltip_text (check, "You will have to manually load packages (using menu \"Reload packages\")");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check), !pkgclip->autoload);
    gtk_widget_show (check);
    
    /* ** As Installed ** */
    GtkWidget *expander;
    expander = gtk_expander_new ("Packages to treat as if they were installed");
    gtk_box_pack_start (GTK_BOX (vbox), expander, FALSE, FALSE, 15);
    gtk_widget_show (expander);
    
    /* vbox */
    GtkWidget *vbox_exp;
    vbox_exp = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add (GTK_CONTAINER (expander), vbox_exp);
    gtk_widget_show (vbox_exp);
    
    /* hbox */
    GtkWidget *hbox;
    hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start (GTK_BOX (vbox_exp), hbox, FALSE, FALSE, 15);
    gtk_widget_show (hbox);
    
    /* liststore for as_installed list */
    GtkListStore *store;
    store = gtk_list_store_new (1, G_TYPE_STRING);
    pkgclip->prefs->model_ai = GTK_TREE_MODEL (store);
    
    /* said list */
    GtkWidget *list;
    list = gtk_tree_view_new_with_model (GTK_TREE_MODEL (store));
    pkgclip->prefs->tree_ai = GTK_TREE_VIEW (list);
    g_object_unref (store);
    
    /* vbox - sort of a toolbar but vertical */
    GtkWidget *vbox_tb;
    GtkWidget *button;
    GtkWidget *image;
    vbox_tb = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start (GTK_BOX (hbox), vbox_tb, FALSE, FALSE, 0);
    gtk_widget_show (vbox_tb);
    
    /* button Add */
    image = gtk_image_new_from_stock (GTK_STOCK_ADD, GTK_ICON_SIZE_MENU);
    button = gtk_button_new ();
    gtk_button_set_image (GTK_BUTTON (button), image);
    gtk_widget_set_tooltip_text (button, "Add a new package to the list");
    gtk_box_pack_start (GTK_BOX (vbox_tb), button, FALSE, FALSE, 0);
    g_signal_connect (G_OBJECT (button), "clicked",
                      G_CALLBACK (prefs_btn_list_add_cb), (gpointer) pkgclip);
    gtk_widget_show (button);
    /* button Edit */
    image = gtk_image_new_from_stock (GTK_STOCK_EDIT, GTK_ICON_SIZE_MENU);
    button = gtk_button_new ();
    gtk_button_set_image (GTK_BUTTON (button), image);
    gtk_widget_set_tooltip_text (button, "Edit selected package");
    gtk_box_pack_start (GTK_BOX (vbox_tb), button, FALSE, FALSE, 0);
    g_signal_connect (G_OBJECT (button), "clicked",
                      G_CALLBACK (prefs_btn_list_edit_cb), (gpointer) pkgclip);
    gtk_widget_show (button);
    /* button Remove */
    image = gtk_image_new_from_stock (GTK_STOCK_REMOVE, GTK_ICON_SIZE_MENU);
    button = gtk_button_new ();
    gtk_button_set_image (GTK_BUTTON (button), image);
    gtk_widget_set_tooltip_text (button, "Remove selected package from the list");
    gtk_box_pack_start (GTK_BOX (vbox_tb), button, FALSE, FALSE, 0);
    g_signal_connect (G_OBJECT (button), "clicked",
                      G_CALLBACK (prefs_btn_list_remove_cb), (gpointer) pkgclip);
    gtk_widget_show (button);
    
    /* a scrolledwindow for the list */
    GtkWidget *scrolled;
    scrolled = gtk_scrolled_window_new (
        gtk_scrollable_get_hadjustment (GTK_SCROLLABLE (list)),
        gtk_scrollable_get_vadjustment (GTK_SCROLLABLE (list)));
    gtk_box_pack_start (GTK_BOX (hbox), scrolled, TRUE, TRUE, 0);
    gtk_widget_show (scrolled);
    
    /* cell renderer & column(s) */
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;
    /* column: Package */
    renderer = gtk_cell_renderer_text_new ();
    g_object_set (G_OBJECT (renderer), "editable", TRUE, NULL);
    g_signal_connect (G_OBJECT (renderer), "edited",
                      G_CALLBACK (prefs_renderer_edited_cb), (gpointer) pkgclip);
    column = gtk_tree_view_column_new_with_attributes ("Packages Name",
                                                       renderer,
                                                       "text", 0,
                                                       NULL);
    gtk_tree_view_column_set_sort_column_id (column, 0);
    g_signal_connect (G_OBJECT (column), "clicked",
                      G_CALLBACK (prefs_column_clicked_cb),
                        (gpointer) pkgclip->prefs->chk_sane_sort_indicator);
    gtk_tree_view_append_column (GTK_TREE_VIEW (list), column);
    
    /* eo.columns  */
    
    /* attach a callback for the sane sort order toggle button now, so we can
     * have a pointer to column as user data */
    g_signal_connect (G_OBJECT (pkgclip->prefs->chk_sane_sort_indicator), "toggled",
                      G_CALLBACK (prefs_sane_toggled_cb), (gpointer) column);
    
    /* fill data */
    GtkTreeIter iter;
    alpm_list_t *j;
    for (j = pkgclip->as_installed; j; j = alpm_list_next (j))
    {
        gtk_list_store_append (store, &iter);
        gtk_list_store_set (store, &iter,
            0,  j->data,
            -1);
    }
    
    gtk_container_add (GTK_CONTAINER (scrolled), list);
    gtk_widget_show (list);
    
    /* nb old ver ai */
    hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start (GTK_BOX (vbox_exp), hbox, FALSE, FALSE, 0);
    gtk_widget_show (hbox);
    
    label = gtk_label_new ("Number of old versions to keep:");
    gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);
    gtk_widget_set_tooltip_text (label, "How many packages of older versions to keep for packages treated As Installed, besides the one installed (or any newer)");
    gtk_widget_show (label);
    
    combo = gtk_combo_box_text_new_with_entry ();
    entry = gtk_bin_get_child (GTK_BIN (combo));
    pkgclip->prefs->entry_ai = entry;
    for (i = 0; i <= 10; ++i)
    {
        snprintf (buf, 3, "%d", i);
        gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), buf);
    }
    if (pkgclip->nb_old_ver <= 10)
    {
        gtk_combo_box_set_active (GTK_COMBO_BOX (combo), pkgclip->nb_old_ver_ai);
    }
    else
    {
        snprintf (buf, 3, "%d", pkgclip->nb_old_ver_ai);
        gtk_entry_set_text (GTK_ENTRY (entry), buf);
    }
    gtk_box_pack_start (GTK_BOX (hbox), combo, TRUE, FALSE, 0);
    gtk_widget_set_tooltip_text (combo, "How many packages of older versions to keep for packages treated As Installed, besides the one installed (or any newer)");
    gtk_widget_show (combo);
    
    /* ** buttons ** */
    
    /* hbox */
    hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);
    gtk_widget_show (hbox);
    
    /* button Clean */
    image = gtk_image_new_from_stock (GTK_STOCK_CLEAR, GTK_ICON_SIZE_MENU);
    button = gtk_button_new_with_label ("Clear Saved Preferences");
    gtk_button_set_image (GTK_BUTTON (button), image);
    gtk_widget_set_tooltip_text (button, "Remove PkgClip's configuration file");
    g_object_set_data (G_OBJECT (button), "btn-id", (gpointer) 0);
    g_signal_connect (G_OBJECT (button), "clicked",
                      G_CALLBACK (prefs_btn_ok_cb), (gpointer) pkgclip);
    gtk_box_pack_start (GTK_BOX (hbox), button, FALSE, FALSE, 4);
    gtk_widget_show (button);
    
    /* button OK */
    button = gtk_button_new_from_stock (GTK_STOCK_OK);
    g_object_set_data (G_OBJECT (button), "btn-id", (gpointer) 1);
    g_signal_connect (G_OBJECT (button), "clicked",
                      G_CALLBACK (prefs_btn_ok_cb), (gpointer) pkgclip);
    gtk_box_pack_end (GTK_BOX (hbox), button, FALSE, FALSE, 4);
    gtk_widget_show (button);
    
    /* button Cancel */
    button = gtk_button_new_from_stock (GTK_STOCK_CANCEL);
    g_signal_connect_swapped (G_OBJECT (button), "clicked",
                     G_CALLBACK (gtk_widget_destroy), (gpointer) window);
    gtk_box_pack_end (GTK_BOX (hbox), button, FALSE, FALSE, 2);
    gtk_widget_show (button);
    
    /* signals */
    g_signal_connect (G_OBJECT (window), "destroy",
                     G_CALLBACK (prefs_destroy_cb), (gpointer) pkgclip);
    
    /* show */
    gtk_widget_show (window);
}

static void
menu_help_cb (GtkMenuItem *menuitem _UNUSED_, pkgclip_t *pkgclip)
{
    GError *error = NULL;
    if (!gtk_show_uri (NULL, "file:///usr/share/doc/pkgclip/html/index.html",
        GDK_CURRENT_TIME, &error))
    {
        show_error ("Unable to open help page", error->message, pkgclip);
        g_error_free (error);
    }
}

static void
menu_about_cb (GtkMenuItem *menuitem _UNUSED_, pkgclip_t *pkgclip)
{
    GtkAboutDialog *about;
    const char *authors[] = {"Olivier Brunel", "Pacman Development Team", NULL};
    const char *artists[] = {"Hylke Bons", NULL};
    
    about = GTK_ABOUT_DIALOG (gtk_about_dialog_new ());
    gtk_window_set_transient_for (GTK_WINDOW (about), GTK_WINDOW(pkgclip->window));
    gtk_window_set_modal (GTK_WINDOW (about), TRUE);
    gtk_window_set_skip_taskbar_hint (GTK_WINDOW(about), TRUE);
    gtk_window_set_skip_pager_hint (GTK_WINDOW(about), TRUE);
    gtk_about_dialog_set_program_name (about, "PkgClip");
    gtk_about_dialog_set_version (about, PKGCLIP_VERSION);
    gtk_about_dialog_set_comments (about, PKGCLIP_TAGLINE);
    gtk_about_dialog_set_website (about, "https://bitbucket.org/jjacky/pkgclip");
    gtk_about_dialog_set_website_label (about, "https://bitbucket.org/jjacky/pkgclip");
    gtk_about_dialog_set_copyright (about, "Copyright (C) 2012 Olivier Brunel");
    gtk_about_dialog_set_license_type (about, GTK_LICENSE_GPL_3_0);
    gtk_about_dialog_set_authors (about, authors);
    gtk_about_dialog_set_artists (about, artists);
    
    GdkPixbuf *pixbuf;
    pixbuf = gdk_pixbuf_new_from_xpm_data (pkgclip_xpm);
    gtk_about_dialog_set_logo (about, pixbuf);
    g_object_unref (G_OBJECT (pixbuf));
    
    gtk_dialog_run (GTK_DIALOG (about));
    gtk_widget_destroy (GTK_WIDGET (about));
}

static gboolean
list_button_press_cb (GtkWidget *widget _UNUSED_, GdkEventButton *event, pkgclip_t *pkgclip)
{
    /* right button */
    if (event->button == 3)
    {
        gtk_menu_popup (GTK_MENU (pkgclip->mnu_edit), NULL, NULL, NULL, NULL,
            event->button, event->time);
        /* stop processing event (else the selection would get lost) */
        return TRUE;
    }
    /* keep processing event */
    return FALSE;
}

int
main (int argc, char *argv[])
{
    pkgclip_t *pkgclip;
    
    gtk_init (&argc, &argv);
    pkgclip = new_pkgclip ();
    init_alpm (pkgclip);
    
    /* use to set images on menus/buttons */
    GtkWidget *image;
    
    /* the window */
    GtkWidget *window;
    window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    pkgclip->window = window;
    gtk_window_set_title (GTK_WINDOW (window), "PkgClip v" PKGCLIP_VERSION);
    gtk_container_set_border_width (GTK_CONTAINER (window), 0);
    gtk_window_set_has_resize_grip (GTK_WINDOW (window), FALSE);
    gtk_window_set_default_size (GTK_WINDOW (window), 800, 600);
    /* icon */
    GdkPixbuf *pixbuf;
    pixbuf = gdk_pixbuf_new_from_xpm_data (pkgclip_xpm);
    gtk_window_set_icon (GTK_WINDOW (window), pixbuf);
    g_object_unref (G_OBJECT (pixbuf));
    
    /* everything in a vbox */
    GtkWidget *vbox;
    vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add (GTK_CONTAINER (window), vbox);
    gtk_widget_show (vbox);
    
    /* menubar */
    GtkWidget *menubar;
    menubar = gtk_menu_bar_new ();
    gtk_box_pack_start (GTK_BOX (vbox), menubar, FALSE, FALSE, 0);
    gtk_widget_show (menubar);
    
    GtkWidget *menu;
    GtkWidget *menuitem;
    GtkWidget *submenu;
    GtkWidget *submenuitem;
    int i;
    char buf[100], buf2[100];
    char *mnu_reasons_desc_select[NB_REASONS];
    char *mnu_reasons_desc_unselect[NB_REASONS];
    
    /* menu "PkgClip" */
    menu = gtk_menu_new ();
    /* reload pkgs */
    menuitem = gtk_image_menu_item_new_with_label ("Reload packages from cache");
    pkgclip->mnu_reload = menuitem;
    image = gtk_image_new_from_stock (GTK_STOCK_REFRESH, GTK_ICON_SIZE_MENU);
    gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM(menuitem), image);
    g_signal_connect (G_OBJECT (menuitem), "activate",
        G_CALLBACK (menu_reload_cb), (gpointer) pkgclip);
    g_signal_connect (G_OBJECT (menuitem), "select",
        G_CALLBACK (menu_select_cb), (gpointer) "Reload list of packages from cache folder");
    g_signal_connect (G_OBJECT (menuitem), "deselect",
        G_CALLBACK (menu_deselect_cb), (gpointer) pkgclip);
    gtk_container_add (GTK_CONTAINER (menu), menuitem);
    gtk_widget_show (menuitem);
    /* --- */
    menuitem = gtk_separator_menu_item_new ();
    gtk_container_add (GTK_CONTAINER (menu), menuitem);
    gtk_widget_show (menuitem);
    /* quit */
    menuitem = gtk_image_menu_item_new_from_stock (GTK_STOCK_QUIT, NULL);
    g_signal_connect (G_OBJECT (menuitem), "activate",
        G_CALLBACK (menu_exit_cb), (gpointer) pkgclip);
    g_signal_connect (G_OBJECT (menuitem), "select",
        G_CALLBACK (menu_select_cb), (gpointer) "Exit PkgClip");
    g_signal_connect (G_OBJECT (menuitem), "deselect",
        G_CALLBACK (menu_deselect_cb), (gpointer) pkgclip);
    gtk_container_add (GTK_CONTAINER (menu), menuitem);
    gtk_widget_show (menuitem);
    /* add in menubar */
    menuitem = gtk_menu_item_new_with_label ("PkgClip");
    gtk_menu_item_set_submenu (GTK_MENU_ITEM (menuitem), menu);
    gtk_container_add (GTK_CONTAINER (menubar), menuitem);
    gtk_widget_show (menuitem);
    
    /* menu "Edit" */
    menu = gtk_menu_new ();
    pkgclip->mnu_edit = menu;
    /* select all */
    menuitem = gtk_image_menu_item_new_from_stock (GTK_STOCK_SELECT_ALL, NULL);
    g_signal_connect (G_OBJECT (menuitem), "activate",
        G_CALLBACK (menu_select_all_cb), (gpointer) pkgclip);
    g_signal_connect (G_OBJECT (menuitem), "select",
        G_CALLBACK (menu_select_cb), (gpointer) "Select all packages");
    g_signal_connect (G_OBJECT (menuitem), "deselect",
        G_CALLBACK (menu_deselect_cb), (gpointer) pkgclip);
    gtk_container_add (GTK_CONTAINER (menu), menuitem);
    gtk_widget_show (menuitem);
    /* unselect all */
    menuitem = gtk_image_menu_item_new_with_label ("Unselect all");
    g_signal_connect (G_OBJECT (menuitem), "activate",
        G_CALLBACK (menu_unselect_all_cb), (gpointer) pkgclip);
    g_signal_connect (G_OBJECT (menuitem), "select",
        G_CALLBACK (menu_select_cb), (gpointer) "Unselect all packages");
    g_signal_connect (G_OBJECT (menuitem), "deselect",
        G_CALLBACK (menu_deselect_cb), (gpointer) pkgclip);
    gtk_container_add (GTK_CONTAINER (menu), menuitem);
    gtk_widget_show (menuitem);
    /* --- */
    menuitem = gtk_separator_menu_item_new ();
    gtk_container_add (GTK_CONTAINER (menu), menuitem);
    gtk_widget_show (menuitem);
    /* select all... */
    menuitem = gtk_image_menu_item_new_with_label ("Mark all...");
    g_signal_connect (G_OBJECT (menuitem), "select",
        G_CALLBACK (menu_select_cb), (gpointer) "Mark all packages with a given reason");
    g_signal_connect (G_OBJECT (menuitem), "deselect",
        G_CALLBACK (menu_deselect_cb), (gpointer) pkgclip);
    gtk_container_add (GTK_CONTAINER (menu), menuitem);
    gtk_widget_show (menuitem);
    /* REASONS */
    submenu = gtk_menu_new ();
    for (i = 0; i < NB_REASONS; ++i)
    {
        /* remove parenthesis when there's %d (nb old ver, etc) */
        snprintf (buf, 100, "%s", reason_label[i]);
        char *s = strchr (buf, '%');
        if (s != NULL)
        {
            s = strchr (buf, '(');
            if (s != NULL)
            {
                *--s = '\0';
            }
        }
        /* the desc */
        snprintf (buf2, 100, "Mark all packages \"%s\"", buf);
        mnu_reasons_desc_select[i] = strdup (buf2);
        /* menuitem */
        submenuitem = gtk_image_menu_item_new_with_label (buf);
        g_object_set_data (G_OBJECT (submenuitem), "reason", GINT_TO_POINTER (i));
        g_signal_connect (G_OBJECT (submenuitem), "activate",
            G_CALLBACK (menu_select_all_reason_cb), (gpointer) pkgclip);
        g_signal_connect (G_OBJECT (submenuitem), "select",
            G_CALLBACK (menu_select_cb), (gpointer) mnu_reasons_desc_select[i]);
        g_signal_connect (G_OBJECT (submenuitem), "deselect",
            G_CALLBACK (menu_deselect_cb), (gpointer) pkgclip);
        gtk_container_add (GTK_CONTAINER (submenu), submenuitem);
        gtk_widget_show (submenuitem);
    }
    gtk_menu_item_set_submenu (GTK_MENU_ITEM (menuitem), submenu);
    gtk_widget_show (submenu);
    /* unselect all... */
    menuitem = gtk_image_menu_item_new_with_label ("Unmark all...");
    g_signal_connect (G_OBJECT (menuitem), "select",
        G_CALLBACK (menu_select_cb), (gpointer) "Unmark all packages with a given reason");
    g_signal_connect (G_OBJECT (menuitem), "deselect",
        G_CALLBACK (menu_deselect_cb), (gpointer) pkgclip);
    gtk_container_add (GTK_CONTAINER (menu), menuitem);
    gtk_widget_show (menuitem);
    /* REASONS */
    submenu = gtk_menu_new ();
    for (i = 0; i < NB_REASONS; ++i)
    {
        /* remove parenthesis when there's %d (nb old ver, etc) */
        snprintf (buf, 100, "%s", reason_label[i]);
        char *s = strchr (buf, '%');
        if (s != NULL)
        {
            s = strchr (buf, '(');
            if (s != NULL)
            {
                *--s = '\0';
            }
        }
        /* the desc */
        snprintf (buf2, 100, "Unmark all packages \"%s\"", buf);
        mnu_reasons_desc_unselect[i] = strdup (buf2);
        /* menuitem */
        submenuitem = gtk_image_menu_item_new_with_label (buf);
        g_object_set_data (G_OBJECT (submenuitem), "reason", GINT_TO_POINTER (i));
        g_signal_connect (G_OBJECT (submenuitem), "activate",
            G_CALLBACK (menu_unselect_all_reason_cb), (gpointer) pkgclip);
        g_signal_connect (G_OBJECT (submenuitem), "select",
            G_CALLBACK (menu_select_cb), (gpointer) mnu_reasons_desc_unselect[i]);
        g_signal_connect (G_OBJECT (submenuitem), "deselect",
            G_CALLBACK (menu_deselect_cb), (gpointer) pkgclip);
        gtk_container_add (GTK_CONTAINER (submenu), submenuitem);
        gtk_widget_show (submenuitem);
    }
    gtk_menu_item_set_submenu (GTK_MENU_ITEM (menuitem), submenu);
    gtk_widget_show (submenu);
    /* --- */
    menuitem = gtk_separator_menu_item_new ();
    gtk_container_add (GTK_CONTAINER (menu), menuitem);
    gtk_widget_show (menuitem);
    /* mark selection */
    menuitem = gtk_image_menu_item_new_with_label ("Mark Selection");
    g_signal_connect (G_OBJECT (menuitem), "activate",
        G_CALLBACK (menu_mark_selection_cb), (gpointer) pkgclip);
    g_signal_connect (G_OBJECT (menuitem), "select",
        G_CALLBACK (menu_select_cb), (gpointer) "Mark all selected packages to be removed");
    g_signal_connect (G_OBJECT (menuitem), "deselect",
        G_CALLBACK (menu_deselect_cb), (gpointer) pkgclip);
    gtk_container_add (GTK_CONTAINER (menu), menuitem);
    gtk_widget_show (menuitem);
    /* unmark selection */
    menuitem = gtk_image_menu_item_new_with_label ("Unmark Selection");
    g_signal_connect (G_OBJECT (menuitem), "activate",
        G_CALLBACK (menu_unmark_selection_cb), (gpointer) pkgclip);
    g_signal_connect (G_OBJECT (menuitem), "select",
        G_CALLBACK (menu_select_cb), (gpointer) "Unmark all selected packages");
    g_signal_connect (G_OBJECT (menuitem), "deselect",
        G_CALLBACK (menu_deselect_cb), (gpointer) pkgclip);
    gtk_container_add (GTK_CONTAINER (menu), menuitem);
    gtk_widget_show (menuitem);
    /* --- */
    menuitem = gtk_separator_menu_item_new ();
    gtk_container_add (GTK_CONTAINER (menu), menuitem);
    gtk_widget_show (menuitem);
    /* add selection to list */
    menuitem = gtk_image_menu_item_new_with_label ("Add Selection to As Installed list");
    g_signal_connect (G_OBJECT (menuitem), "activate",
        G_CALLBACK (menu_add_as_installed_selection_cb), (gpointer) pkgclip);
    g_signal_connect (G_OBJECT (menuitem), "select",
        G_CALLBACK (menu_select_cb), (gpointer) "Adds all selected packages to list of packages to treat as if they were installed");
    g_signal_connect (G_OBJECT (menuitem), "deselect",
        G_CALLBACK (menu_deselect_cb), (gpointer) pkgclip);
    gtk_container_add (GTK_CONTAINER (menu), menuitem);
    gtk_widget_show (menuitem);
    /* remove selection from list */
    menuitem = gtk_image_menu_item_new_with_label ("Remove Selection from As Installed list");
    g_signal_connect (G_OBJECT (menuitem), "activate",
        G_CALLBACK (menu_remove_as_installed_selection_cb), (gpointer) pkgclip);
    g_signal_connect (G_OBJECT (menuitem), "select",
        G_CALLBACK (menu_select_cb), (gpointer) "Removes all selected packages from list of packages to treat as if they were installed");
    g_signal_connect (G_OBJECT (menuitem), "deselect",
        G_CALLBACK (menu_deselect_cb), (gpointer) pkgclip);
    gtk_container_add (GTK_CONTAINER (menu), menuitem);
    gtk_widget_show (menuitem);
    /* --- */
    menuitem = gtk_separator_menu_item_new ();
    gtk_container_add (GTK_CONTAINER (menu), menuitem);
    gtk_widget_show (menuitem);
    /* restore recommendations */
    menuitem = gtk_image_menu_item_new_with_label ("Restore recommendations");
    g_signal_connect (G_OBJECT (menuitem), "activate",
        G_CALLBACK (menu_restore_recomm_cb), (gpointer) pkgclip);
    g_signal_connect (G_OBJECT (menuitem), "select",
        G_CALLBACK (menu_select_cb), (gpointer) "Mark only & all recommended packages");
    g_signal_connect (G_OBJECT (menuitem), "deselect",
        G_CALLBACK (menu_deselect_cb), (gpointer) pkgclip);
    gtk_container_add (GTK_CONTAINER (menu), menuitem);
    gtk_widget_show (menuitem);
    /* --- */
    menuitem = gtk_separator_menu_item_new ();
    gtk_container_add (GTK_CONTAINER (menu), menuitem);
    gtk_widget_show (menuitem);
    /* preferences */
    menuitem = gtk_image_menu_item_new_from_stock (GTK_STOCK_PREFERENCES, NULL);
    g_signal_connect (G_OBJECT (menuitem), "activate",
        G_CALLBACK (menu_preferences_cb), (gpointer) pkgclip);
    g_signal_connect (G_OBJECT (menuitem), "select",
        G_CALLBACK (menu_select_cb), (gpointer) "Edit PkgClip preferences");
    g_signal_connect (G_OBJECT (menuitem), "deselect",
        G_CALLBACK (menu_deselect_cb), (gpointer) pkgclip);
    gtk_container_add (GTK_CONTAINER (menu), menuitem);
    gtk_widget_show (menuitem);
    /* add in menubar */
    menuitem = gtk_menu_item_new_with_label ("Edit");
    gtk_menu_item_set_submenu (GTK_MENU_ITEM (menuitem), menu);
    gtk_container_add (GTK_CONTAINER (menubar), menuitem);
    gtk_widget_show (menuitem);
    
    /* menu "Help" */
    menu = gtk_menu_new ();
    /* help */
    menuitem = gtk_image_menu_item_new_from_stock (GTK_STOCK_HELP, NULL);
    g_signal_connect (G_OBJECT (menuitem), "activate",
        G_CALLBACK (menu_help_cb), (gpointer) pkgclip);
    g_signal_connect (G_OBJECT (menuitem), "select",
        G_CALLBACK (menu_select_cb), (gpointer) "Open the help page for PkgClip in the default browser");
    g_signal_connect (G_OBJECT (menuitem), "deselect",
        G_CALLBACK (menu_deselect_cb), (gpointer) pkgclip);
    gtk_container_add (GTK_CONTAINER (menu), menuitem);
    gtk_widget_show (menuitem);
    /* --- */
    menuitem = gtk_separator_menu_item_new ();
    gtk_container_add (GTK_CONTAINER (menu), menuitem);
    gtk_widget_show (menuitem);
    menuitem = gtk_image_menu_item_new_from_stock (GTK_STOCK_ABOUT, NULL);
    g_signal_connect (G_OBJECT (menuitem), "activate",
        G_CALLBACK (menu_about_cb), (gpointer) pkgclip);
    g_signal_connect (G_OBJECT (menuitem), "select",
        G_CALLBACK (menu_select_cb), (gpointer) "Show copyright & version information");
    g_signal_connect (G_OBJECT (menuitem), "deselect",
        G_CALLBACK (menu_deselect_cb), (gpointer) pkgclip);
    gtk_container_add (GTK_CONTAINER (menu), menuitem);
    gtk_widget_show (menuitem);
    /* add in menubar */
    menuitem = gtk_menu_item_new_with_label ("Help");
    gtk_menu_item_set_submenu (GTK_MENU_ITEM (menuitem), menu);
    gtk_container_add (GTK_CONTAINER (menubar), menuitem);
    gtk_widget_show (menuitem);
    
    /* store for the list */
    GtkListStore *store;
    store = gtk_list_store_new (COL_NB,
        G_TYPE_POINTER, /* pc_pkg */
        G_TYPE_STRING,  /* package */
        G_TYPE_STRING,  /* version */
        G_TYPE_INT,     /* size */
        G_TYPE_BOOLEAN, /* remove */
        G_TYPE_INT,     /* recomm */
        G_TYPE_INT,     /* reason */
        G_TYPE_INT,     /* nb old ver */
        G_TYPE_INT      /* nb old ver total */
        );
    pkgclip->store = store;
    /* set our custom sort function for COL_PACKAGE */
    gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (store), COL_PACKAGE,
        (GtkTreeIterCompareFunc) list_sort_package, NULL, NULL);
    
    /* said list */
    GtkWidget *list;
    list = gtk_tree_view_new_with_model (GTK_TREE_MODEL (store));
    pkgclip->list = list;
    g_object_unref (store);
    gtk_tree_view_set_headers_clickable (GTK_TREE_VIEW (list), TRUE);
    /* multiple selection */
    GtkTreeSelection *selection;
    selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (list));
    gtk_tree_selection_set_mode (selection, GTK_SELECTION_MULTIPLE);
    /* hint for alternate row colors */
    gtk_tree_view_set_rules_hint (GTK_TREE_VIEW (list), TRUE);
    /* for right-click */
    gtk_widget_add_events (list, GDK_BUTTON_RELEASE_MASK);
    g_signal_connect (G_OBJECT (list), "button-press-event",
                     G_CALLBACK (list_button_press_cb), (gpointer) pkgclip);
    /* tooltip */
    gtk_widget_set_has_tooltip (list, TRUE);
    g_signal_connect (G_OBJECT (list), "query-tooltip",
                     G_CALLBACK (list_query_tooltip_cb), (gpointer) pkgclip);
    
    /* a scrolledwindow for the list */
    GtkWidget *scrolled;
    scrolled = gtk_scrolled_window_new (
        gtk_scrollable_get_hadjustment (GTK_SCROLLABLE (list)),
        gtk_scrollable_get_vadjustment (GTK_SCROLLABLE (list)));
    gtk_box_pack_start (GTK_BOX (vbox), scrolled, TRUE, TRUE, 0);
    gtk_widget_show (scrolled);
    
    /* cell renderer & column(s) */
    GtkCellRenderer *renderer, *tgl_renderer;
    GtkTreeViewColumn *column;
    /* column: Package */
    renderer = gtk_cell_renderer_text_new ();
    column = gtk_tree_view_column_new_with_attributes ("Package",
                                                       renderer,
                                                       "text", COL_PACKAGE,
                                                       NULL);
    g_object_set_data (G_OBJECT (column), "col-id", (gpointer) COL_PACKAGE);
    gtk_tree_view_column_set_sort_column_id (column, COL_PACKAGE);
    g_signal_connect (G_OBJECT (column), "clicked",
                     G_CALLBACK (column_clicked_cb), (gpointer) pkgclip);
    gtk_tree_view_column_set_resizable (column, TRUE);
    gtk_tree_view_append_column (GTK_TREE_VIEW (list), column);
    /* since we add items sorted, let's show it */
    /* FIXME: this actually causes to sort "again" */
    gtk_tree_view_column_clicked (column);
    /* column: Version */
    column = gtk_tree_view_column_new_with_attributes ("Version",
                                                       renderer,
                                                       "text", COL_VERSION,
                                                       NULL);
    g_object_set_data (G_OBJECT (column), "col-id", (gpointer) COL_VERSION);
    gtk_tree_view_column_set_sort_column_id (column, COL_VERSION);
    g_signal_connect (G_OBJECT (column), "clicked",
                     G_CALLBACK (column_clicked_cb), (gpointer) pkgclip);
    gtk_tree_view_column_set_resizable (column, TRUE);
    gtk_tree_view_append_column (GTK_TREE_VIEW (list), column);
    /* column: Remove */
    tgl_renderer = gtk_cell_renderer_toggle_new ();
    column = gtk_tree_view_column_new_with_attributes ("Remove",
                                                       tgl_renderer,
                                                       "active", COL_REMOVE,
                                                       NULL);
    g_object_set_data (G_OBJECT (column), "col-id", (gpointer) COL_REMOVE);
    gtk_tree_view_column_set_sort_column_id (column, COL_REMOVE);
    g_signal_connect (G_OBJECT (column), "clicked",
                     G_CALLBACK (column_clicked_cb), (gpointer) pkgclip);
    g_object_set (tgl_renderer, "activatable", TRUE, "active", FALSE, NULL);
    g_signal_connect (G_OBJECT (tgl_renderer), "toggled",
                     G_CALLBACK (renderer_toggle_cb), (gpointer) pkgclip);
    gtk_tree_view_append_column (GTK_TREE_VIEW (list), column);
    /* column: File size */
    column = gtk_tree_view_column_new_with_attributes ("File size",
                                                       renderer,
                                                       "text", COL_SIZE,
                                                       NULL);
    g_object_set_data (G_OBJECT (column), "col-id", (gpointer) COL_SIZE);
    gtk_tree_view_column_set_sort_column_id (column, COL_SIZE);
    g_signal_connect (G_OBJECT (column), "clicked",
                     G_CALLBACK (column_clicked_cb), (gpointer) pkgclip);
    gtk_tree_view_column_set_cell_data_func (column, renderer,
        (GtkTreeCellDataFunc) rend_size, (gpointer) COL_SIZE, NULL);
    gtk_tree_view_column_set_resizable (column, TRUE);
    gtk_tree_view_append_column (GTK_TREE_VIEW (list), column);
    /* column: Recomm */
    column = gtk_tree_view_column_new_with_attributes ("Suggest",
                                                       renderer,
                                                       "text", COL_RECOMM,
                                                       NULL);
    g_object_set_data (G_OBJECT (column), "col-id", (gpointer) COL_RECOMM);
    gtk_tree_view_column_set_sort_column_id (column, COL_RECOMM);
    g_signal_connect (G_OBJECT (column), "clicked",
                     G_CALLBACK (column_clicked_cb), (gpointer) pkgclip);
    gtk_tree_view_column_set_cell_data_func (column, renderer,
        (GtkTreeCellDataFunc) rend_recomm, NULL, NULL);
    gtk_tree_view_column_set_resizable (column, TRUE);
    gtk_tree_view_append_column (GTK_TREE_VIEW (list), column);
    /* column: Reason */
    column = gtk_tree_view_column_new_with_attributes ("Reason",
                                                       renderer,
                                                       "text", COL_REASON,
                                                       NULL);
    g_object_set_data (G_OBJECT (column), "col-id", (gpointer) COL_REASON);
    gtk_tree_view_column_set_sort_column_id (column, COL_REASON);
    g_signal_connect (G_OBJECT (column), "clicked",
                     G_CALLBACK (column_clicked_cb), (gpointer) pkgclip);
    gtk_tree_view_column_set_cell_data_func (column, renderer,
        (GtkTreeCellDataFunc) rend_reason, NULL, NULL);
    gtk_tree_view_column_set_resizable (column, TRUE);
    gtk_tree_view_append_column (GTK_TREE_VIEW (list), column);
    
    /* eo.columns  */
    
    gtk_container_add (GTK_CONTAINER (scrolled), list);
    gtk_widget_show (list);
    
    /* hbox for status/info on left, buttons on right */
    GtkWidget *hbox;
    hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);
    gtk_widget_show (hbox);
    
    /* label */
    GtkWidget *label;
    label = gtk_label_new ("Welcome.");
    pkgclip->label = label;
    pkgclip_label = GTK_LABEL (label);
    gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, TRUE, 0);
    gtk_widget_show (label);
    
    GtkWidget *button;
    
    /* button: Remove marked packages */
    image = gtk_image_new_from_stock (GTK_STOCK_DELETE, GTK_ICON_SIZE_MENU);
    button = gtk_button_new_with_label ("Remove marked packages...");
    pkgclip->button = button;
    gtk_button_set_image (GTK_BUTTON (button), image);
    gtk_widget_set_sensitive (button, FALSE);
    g_signal_connect (G_OBJECT (button), "clicked",
                     G_CALLBACK (btn_remove_cb), (gpointer) pkgclip);
    //gtk_widget_add_events (list, GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);
    g_signal_connect (G_OBJECT (button), "enter-notify-event",
        G_CALLBACK (btn_enter_cb), (gpointer) "Remove all packages marked/checked (confirmation required)");
    g_signal_connect (G_OBJECT (button), "leave-notify-event",
        G_CALLBACK (btn_leave_cb), (gpointer) pkgclip);
    gtk_widget_set_tooltip_text (button, "Will remove, after a confirmation and PolicyKit authentification, all marked packages");
    gtk_box_pack_end (GTK_BOX (hbox), button, FALSE, FALSE, 10);
    gtk_widget_show (button);
    
    /* button: Select next marked package */
    image = gtk_image_new_from_stock (GTK_STOCK_MEDIA_NEXT, GTK_ICON_SIZE_MENU);
    button = gtk_button_new ();
    gtk_button_set_image (GTK_BUTTON (button), image);
    g_signal_connect (G_OBJECT (button), "clicked",
                     G_CALLBACK (btn_next_cb), (gpointer) pkgclip);
    g_signal_connect (G_OBJECT (button), "enter-notify-event",
        G_CALLBACK (btn_enter_cb), (gpointer) "Select next marked package");
    g_signal_connect (G_OBJECT (button), "leave-notify-event",
        G_CALLBACK (btn_leave_cb), (gpointer) pkgclip);
    gtk_widget_set_tooltip_text (button, "Select next marked package");
    gtk_box_pack_end (GTK_BOX (hbox), button, FALSE, FALSE, 0);
    gtk_widget_show (button);
    
    /* button: Select previous marked package */
    image = gtk_image_new_from_stock (GTK_STOCK_MEDIA_PREVIOUS, GTK_ICON_SIZE_MENU);
    button = gtk_button_new ();
    gtk_button_set_image (GTK_BUTTON (button), image);
    g_signal_connect (G_OBJECT (button), "clicked",
                     G_CALLBACK (btn_prev_cb), (gpointer) pkgclip);
    g_signal_connect (G_OBJECT (button), "enter-notify-event",
        G_CALLBACK (btn_enter_cb), (gpointer) "Select previous marked package");
    g_signal_connect (G_OBJECT (button), "leave-notify-event",
        G_CALLBACK (btn_leave_cb), (gpointer) pkgclip);
    gtk_widget_set_tooltip_text (button, "Select previous marked package");
    gtk_box_pack_end (GTK_BOX (hbox), button, FALSE, FALSE, 0);
    gtk_widget_show (button);
    
    /* signals */
    g_signal_connect (G_OBJECT (window), "destroy",
                     G_CALLBACK (window_destroy_cb), (gpointer) pkgclip);

    
    /* show */
    gtk_widget_show_now (window);
    gtk_main_iteration ();
    
    /* load list */
    if (pkgclip->autoload)
    {
        reload_list (pkgclip);
    }
    
    if (!pkgclip->abort)
    {
        pkgclip->in_gtk_main = TRUE;
        gtk_main ();
    }
    
    /* free "[un]mark REASON" descriptions */
    for (i = 0; i < NB_REASONS; ++i)
    {
        free (mnu_reasons_desc_select[i]);
        free (mnu_reasons_desc_unselect[i]);
    }
    
    /* free alpm */
    if (pkgclip->handle && alpm_release (pkgclip->handle) == -1)
    {
		show_error ("Error releasing alpm library", NULL, pkgclip);
	}
    
    free_pkgclip (pkgclip);
    return 0;
}

