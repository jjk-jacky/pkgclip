/**
 * PkgClip - Copyright (C) 2012-2013 Olivier Brunel
 *
 * util.c
 * Copyright (C) 2012 Olivier Brunel <i.am.jack.mail@gmail.com>
 * Copyright (c) 2006-2011 Pacman Development Team <pacman-dev@archlinux.org>
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

#include "config.h"

/* C */
#include <ctype.h>
#include <string.h>
#include <dirent.h> /* PATH_MAX */
#include <errno.h>
#include <stdio.h>
#include <glob.h>

/* pkgclip */
#include "pkgclip.h"
#include "util.h"


static void setrepeatingoption (char *ptr, alpm_list_t **list);
static void parse_config_file (const char *file, gboolean is_pacman, int depth,
    pkgclip_t *pkgclip);
static void setstringoption (char *value, char **cfg);
static void setrecommoption (char *value, recomm_t *cfg);

/*******************************************************************************
 * The following functions come from pacman's source code. (They might have
 * been (slightly) modified.)
 *
 * Copyright (c) 2006-2011 Pacman Development Team <pacman-dev@archlinux.org>
 * Copyright (c) 2002-2006 by Judd Vinet <jvinet@zeroflux.org>
 * http://projects.archlinux.org/pacman.git
 *
 ******************************************************************************/

/** Converts sizes in bytes into human readable units.
 *
 * @param bytes the size in bytes
 * @param target_unit '\0' or a short label. If equal to one of the short unit
 * labels ('B', 'K', ...) bytes is converted to target_unit; if '\0', the first
 * unit which will bring the value to below a threshold of 2048 will be chosen.
 * @param long_labels whether to use short ("K") or long ("KiB") unit labels
 * @param label will be set to the appropriate unit label
 *
 * @return the size in the appropriate unit
 */
double
humanize_size (off_t bytes, const char target_unit, const char **label)
{
    static const char *labels[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB",
        "EiB", "ZiB", "YiB"};
    static const int unitcount = sizeof (labels) / sizeof (labels[0]);

    double val = (double) bytes;
    int i;

    for (i = 0; i < unitcount - 1; ++i)
    {
        if (target_unit != '\0' && labels[i][0] == target_unit)
            break;
        else if (target_unit == '\0' && val <= 2048.0 && val >= -2048.0)
            break;
        val /= 1024.0;
    }

    if (label)
        *label = labels[i];

    return val;
}

/**
 * Trim whitespace and newlines from a string
 */
char *
strtrim (char *str)
{
    char *pch = str;

    if (str == NULL || *str == '\0')
        /* string is empty, so we're done. */
        return str;

    while (isspace ((unsigned char) *pch))
        ++pch;
    if (pch != str)
    {
        size_t len = strlen (pch);
        if (len)
            memmove (str, pch, len + 1);
        else
            *str = '\0';
    }

    /* check if there wasn't anything but whitespace in the string. */
    if (*str == '\0')
        return str;

    pch = (str + (strlen (str) - 1));
    while (isspace ((unsigned char) *pch))
        --pch;
    *++pch = '\0';

    return str;
}

/** Add repeating options such as NoExtract, NoUpgrade, etc to libalpm
 * settings. Refactored out of the parseconfig code since all of them did
 * the exact same thing and duplicated code.
 * @param ptr a pointer to the start of the multiple options
 * @param option the string (friendly) name of the option, used for messages
 * @param list the list to add the option to
 */
static void
setrepeatingoption (char *ptr, alpm_list_t **list)
{
    char *q;

    while ((q = strchr(ptr, ' ')))
    {
        *q = '\0';
        *list = alpm_list_add (*list, strdup (ptr));
        ptr = q + 1;
    }
    *list = alpm_list_add (*list, strdup (ptr));
}

/** inspired from pacman's function */
static void
parse_config_file (const char *file, gboolean is_pacman, int depth, pkgclip_t *pkgclip)
{
    FILE       *fp              = NULL;
    char        line[PATH_MAX];
    int         linenum         = 0;
    gboolean    ignore_section  = FALSE;
    const int   max_depth       = 10;

    fp = fopen (file, "r");
    if (fp == NULL)
    {
        if (is_pacman)
        {
            snprintf (line, PATH_MAX, "Config file %s could not be read", file);
            show_error (line, NULL, pkgclip);
        }
        return;
    }

    while (fgets (line, PATH_MAX, fp))
    {
        char *key, *value, *ptr;
        size_t line_len;

        ++linenum;
        strtrim (line);
        line_len = strlen(line);

        /* ignore whole line and end of line comments */
        if (line_len == 0 || line[0] == '#')
            continue;
        if (NULL != (ptr = strchr(line, '#')))
            *ptr = '\0';

        /* section -- because we might parse pacman.conf */
        if (line[0] == '[' && line[line_len - 1] == ']')
        {
            if (!is_pacman)
                continue;

            /* only possibility here is a line == '[]' */
            if (line_len <= 2)
            {
                ignore_section = TRUE;
                continue;
            }

            char *name;
            name = line;
            name[line_len - 1] = '\0';
            ++name;
            /* we only allow "options" as section name, ignore everything else */
            ignore_section = (strcmp (name, "options") != 0);
            continue;
        }

        /* directive */
        /* strsep modifies the 'line' string: 'key \0 value' */
        key = line;
        value = line;
        strsep (&value, "=");
        strtrim (key);
        strtrim (value);

        if (key == NULL)
            continue;

        if (is_pacman)
        {
            if (strcmp (key, "Include") == 0)
            {
                if (depth + 1 >= max_depth)
                    continue;

                glob_t globbuf;
                int globret;
                size_t gindex;

                if (value == NULL)
                    continue;

                /* Ignore include failures... assume non-critical */
                globret = glob (value, GLOB_NOCHECK, NULL, &globbuf);
                switch (globret)
                {
                    case GLOB_NOSPACE:
                    case GLOB_ABORTED:
                    case GLOB_NOMATCH:
                        break;
                    default:
                        for (gindex = 0; gindex < globbuf.gl_pathc; gindex++)
                            parse_config_file (globbuf.gl_pathv[gindex],
                                    is_pacman, depth + 1, pkgclip);
                        break;
                }
                globfree (&globbuf);
                continue;
            }
            else if (ignore_section)
                continue;
            else if (strcmp (key, "DBPath") == 0)
                setstringoption (value, &(pkgclip->dbpath));
            else if (strcmp (key, "RootDir") == 0)
                setstringoption (value, &(pkgclip->rootpath));
            else if (strcmp (key, "CacheDir") == 0)
                setrepeatingoption (value, &(pkgclip->cachedirs));
        }
        else
        {
            if (strcmp (key, "PacmanConf") == 0)
                setstringoption (value, &(pkgclip->pacmanconf));
            else if (strcmp (key, "NoAutoload") == 0)
                pkgclip->autoload = FALSE;
            else if (strcmp (key, "PkgrelNoSpecial") == 0)
                pkgclip->old_pkgrel = FALSE;
            else if (strcmp (key, "NbOldVersion") == 0)
            {
                char *s;
                setstringoption (value, &s);
                if (NULL != s)
                {
                    pkgclip->nb_old_ver = atoi (s);
                    free (s);
                }
            }
            else if (strcmp (key, "NbOldVersionAsInstalled") == 0)
            {
                char *s;
                setstringoption (value, &s);
                if (NULL != s)
                {
                    pkgclip->nb_old_ver_ai = atoi (s);
                    free (s);
                }
            }
            else if (strcmp (key, "AsInstalled") == 0)
                setrepeatingoption (value, &(pkgclip->as_installed));
            else if (strncmp (key, "RecommFor", 9) == 0) /* 9 == strlen("RecommFor") */
            {
                char *s;
                s = key + 9;

                if (strcmp (s, "NewerThanInstalled") == 0)
                    setrecommoption (value, &(pkgclip->recomm[REASON_NEWER_THAN_INSTALLED]));
                else if (strcmp (s, "Installed") == 0)
                    setrecommoption (value, &(pkgclip->recomm[REASON_INSTALLED]));
                else if (strcmp (s, "OlderVersion") == 0)
                    setrecommoption (value, &(pkgclip->recomm[REASON_OLDER_VERSION]));
                else if (strcmp (s, "AlreadyOlderVersion") == 0)
                    setrecommoption (value, &(pkgclip->recomm[REASON_ALREADY_OLDER_VERSION]));
                else if (strcmp (s, "OlderPkgrel") == 0)
                    setrecommoption (value, &(pkgclip->recomm[REASON_OLDER_PKGREL]));
                else if (strcmp (s, "PkgNotInstalled") == 0)
                    setrecommoption (value, &(pkgclip->recomm[REASON_PKG_NOT_INSTALLED]));
            }
            else if (strcmp (key, "HidePkgInfo") == 0)
                pkgclip->show_pkg_info = FALSE;
            else if (strcmp (key, "PkgInfo") == 0)
                setstringoption (value, &(pkgclip->pkg_info));
        }
    }

    fclose (fp);
}

/******************************************************************************/

gboolean
confirm (const gchar *message,
         const gchar *submessage,
         const gchar *btn_yes_label,
         const gchar *btn_yes_image,
         const gchar *btn_no_label,
         const gchar *btn_no_image,
         pkgclip_t *pkgclip
         )
{
    GtkWidget *dialog;
    GtkWidget *button;
    GtkWidget *image;
    gint       rc;

    if (NULL == submessage)
    {
        dialog = gtk_message_dialog_new_with_markup (
                GTK_WINDOW(pkgclip->window),
                GTK_DIALOG_DESTROY_WITH_PARENT,
                GTK_MESSAGE_QUESTION,
                GTK_BUTTONS_NONE,
                NULL);
        gtk_message_dialog_set_markup (GTK_MESSAGE_DIALOG(dialog), message);
    }
    else
    {
        dialog = gtk_message_dialog_new (
                GTK_WINDOW(pkgclip->window),
                GTK_DIALOG_DESTROY_WITH_PARENT,
                GTK_MESSAGE_QUESTION,
                GTK_BUTTONS_NONE,
                "%s",
                message);
        gtk_message_dialog_format_secondary_markup (
                GTK_MESSAGE_DIALOG(dialog),
                "%s",
                submessage);
    }

    gtk_window_set_decorated (GTK_WINDOW(dialog), FALSE);
    gtk_window_set_skip_taskbar_hint (GTK_WINDOW(dialog), TRUE);
    gtk_window_set_skip_pager_hint (GTK_WINDOW(dialog), TRUE);

    button = gtk_dialog_add_button(
            GTK_DIALOG(dialog),
            (NULL == btn_no_label) ? GTK_STOCK_NO : btn_no_label,
            GTK_RESPONSE_NO);
    image = gtk_image_new_from_stock (
            (NULL == btn_no_image) ? GTK_STOCK_NO : btn_no_image,
            GTK_ICON_SIZE_MENU);
    gtk_button_set_image( GTK_BUTTON(button), image);

    button = gtk_dialog_add_button(
            GTK_DIALOG(dialog),
            (NULL == btn_yes_label) ? GTK_STOCK_YES : btn_yes_label,
            GTK_RESPONSE_YES);
    image = gtk_image_new_from_stock (
            (NULL == btn_yes_image) ? GTK_STOCK_YES : btn_yes_image,
            GTK_ICON_SIZE_MENU);
    gtk_button_set_image( GTK_BUTTON(button), image);

    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_NO);
    rc = gtk_dialog_run (GTK_DIALOG(dialog));
    gtk_widget_destroy (dialog);
    return rc == GTK_RESPONSE_YES;
}

void
show_error (const gchar *message, const gchar *submessage, pkgclip_t *pkgclip)
{
    GtkWidget *dialog;

    if (NULL == submessage)
    {
        dialog = gtk_message_dialog_new_with_markup (
                GTK_WINDOW(pkgclip->window),
                GTK_DIALOG_DESTROY_WITH_PARENT,
                GTK_MESSAGE_ERROR,
                GTK_BUTTONS_OK,
                NULL);
        gtk_message_dialog_set_markup (GTK_MESSAGE_DIALOG(dialog), message);
    }
    else
    {
        dialog = gtk_message_dialog_new (
                GTK_WINDOW(pkgclip->window),
                GTK_DIALOG_DESTROY_WITH_PARENT,
                GTK_MESSAGE_ERROR,
                GTK_BUTTONS_OK,
                "%s",
                message);
        gtk_message_dialog_format_secondary_markup (
                GTK_MESSAGE_DIALOG(dialog),
                "%s",
                submessage);
    }

    gtk_window_set_decorated (GTK_WINDOW(dialog), FALSE);
    gtk_window_set_skip_taskbar_hint (GTK_WINDOW(dialog), TRUE);
    gtk_window_set_skip_pager_hint (GTK_WINDOW(dialog), TRUE);

    g_signal_connect_swapped (dialog, "response",
            G_CALLBACK (gtk_widget_destroy), dialog);
    gtk_widget_show_all (dialog);
}

static void
setstringoption (char *value, char **cfg)
{
    size_t len;

    if (NULL == value)
        return;

    if (NULL != *cfg)
        free (*cfg);

    if (value[0] == '"')
    {
        len = strlen (value) - 1;
        if (value[len] == '"')
        {
            value[len] = '\0';
            ++value;
        }
    }

    *cfg = strdup (value);
}

static void
setrecommoption (char *value, recomm_t *cfg)
{
    if (strcmp (value, "Keep") == 0)
        *cfg = RECOMM_KEEP;
    else if (strcmp (value, "Remove") == 0)
        *cfg = RECOMM_REMOVE;
}

void
parse_pacmanconf (pkgclip_t *pkgclip)
{
    char file[PATH_MAX];

    if (!pkgclip->pacmanconf)
        pkgclip->pacmanconf = strdup (PACMAN_CONF);
    snprintf (file, PATH_MAX, "%s/pacman.conf", pkgclip->pacmanconf);
    parse_config_file (file, TRUE, 1, pkgclip);

    /* set defaults for what's not set */
    if (!pkgclip->dbpath)
        pkgclip->dbpath = strdup (DB_PATH);
    if (!pkgclip->rootpath)
        pkgclip->rootpath = strdup (ROOT_PATH);
    if (!pkgclip->cachedirs)
        pkgclip->cachedirs = alpm_list_add (NULL, strdup (CACHE_PATH));
}

/* returns the template string (to be shown/saved) with '\t' and '\n' "escaped"
 * in a new string, to be free()-d */
char *
get_tpl_pkg_info (pkgclip_t *pkgclip)
{
    char *tpl, *s, *d;
    size_t len, alloc;

    alloc = strlen (pkgclip->pkg_info) + 23;
    tpl = malloc (sizeof (*tpl) * (alloc + 1));
    s = pkgclip->pkg_info;
    d = tpl;
    len = 0;
    for ( ; ; ++s, ++d)
    {
        if (++len >= alloc)
        {
            alloc += 23;
            tpl = realloc (tpl, sizeof (*tpl) * (alloc + 1));
        }

        if (*s == '\t')
        {
            *d++ = '\\';
            *d = 't';
        }
        else if (*s == '\n')
        {
            *d++ = '\\';
            *d = 'n';
        }
        else
            *d = *s;
        if (*s == '\0')
            break;
    }

    return tpl;
}

/* takes pkgclip->pkg_info, replaces "\t" & "\n" by their corresponding characters,
 * and sets up pkgclip->pkg_info_extras (free-ing what's in it first if needed) */
void
load_pkg_info (pkgclip_t *pkgclip)
{
    char *s, *last;
    size_t l;
    const char *vars[] = { "NAME",      (const char *) VAR_NAME,
                           "DESC",      (const char *) VAR_DESC,
                           "FILE",      (const char *) VAR_FILE,
                           "VERSION",   (const char *) VAR_VERSION,
                           "SIZE",      (const char *) VAR_SIZE,
                           "RECOMM",    (const char *) VAR_RECOMM,
                           "REASON",    (const char *) VAR_REASON,
                           NULL };

    s = pkgclip->pkg_info;
    l = strlen (s);
    while ((s = strstr (s, "\\t")))
    {
        *s = '\t';
        memmove (s + 1, s + 2, l - (size_t) (s - pkgclip->pkg_info) - 1);
        --l;
    }
    s = pkgclip->pkg_info;
    while ((s = strstr (s, "\\n")))
    {
        *s = '\n';
        memmove (s + 1, s + 2, l - (size_t) (s - pkgclip->pkg_info) - 1);
        --l;
    }

    /* free things if needed */
    alpm_list_free (pkgclip->pkg_info_extras);
    pkgclip->pkg_info_extras = NULL;

    s = pkgclip->pkg_info;
    last = s;
    while ((s = strchr (s, '$')))
    {
        const char **v = vars;

        while (*v)
        {
            l = strlen (*v);
            if (strncmp (s + 1, *v, l) == 0)
            {
                pkgclip->pkg_info_extras = alpm_list_add (pkgclip->pkg_info_extras,
                        (void *) (s - last));
                pkgclip->pkg_info_extras = alpm_list_add (pkgclip->pkg_info_extras,
                        (void *) *(v + 1));
                s += l + 1;
                last = s;
                v = NULL;
                break;
            }
            v += 2;
        }
        if (v && !*v)
        {
            /* did not find a match, moving forward leaving this untouched */
            ++s;
            continue;
        }

        if (*s)
            pkgclip->pkg_info_extras = alpm_list_add (pkgclip->pkg_info_extras,
                    s);
    }
}

pkgclip_t *
new_pkgclip (void)
{
    pkgclip_t *pkgclip = calloc (1, sizeof (*pkgclip));

    /* set some defaults */
    pkgclip->autoload = TRUE;
    pkgclip->old_pkgrel = TRUE;
    pkgclip->recomm[REASON_NEWER_THAN_INSTALLED]    = RECOMM_KEEP;
    pkgclip->recomm[REASON_INSTALLED]               = RECOMM_KEEP;
    pkgclip->recomm[REASON_OLDER_VERSION]           = RECOMM_KEEP;
    pkgclip->recomm[REASON_ALREADY_OLDER_VERSION]   = RECOMM_REMOVE;
    pkgclip->recomm[REASON_OLDER_PKGREL]            = RECOMM_REMOVE;
    pkgclip->recomm[REASON_PKG_NOT_INSTALLED]       = RECOMM_REMOVE;
    pkgclip->recomm[REASON_AS_INSTALLED] = pkgclip->recomm[REASON_INSTALLED];
    pkgclip->nb_old_ver = 1;
    pkgclip->nb_old_ver_ai = 0;
    pkgclip->show_pkg_info = TRUE;
    pkgclip->pkg_info = strdup (PKG_INFO_TPL);

    /* parse config file, if any */
    char file[PATH_MAX];
    snprintf (file, PATH_MAX, "%s/.config/pkgclip.conf", g_get_home_dir ());
    parse_config_file (file, FALSE, 1, pkgclip);

    /* parse pacman.conf */
    parse_pacmanconf (pkgclip);

    /* prepare package info */
    load_pkg_info (pkgclip);

    return pkgclip;
}

gboolean
save_config (pkgclip_t *pkgclip)
{
    FILE *fp;
    char buf[1024], *s;
    int len = 1024, nb;
    char file[PATH_MAX];
    alpm_list_t *i;

    snprintf (file, PATH_MAX, "%s/.config/pkgclip.conf", g_get_home_dir ());
    fp = fopen (file, "w");
    if (NULL == fp)
    {
        snprintf (buf, 1024, "%s: %s", file, strerror (errno));
        show_error ("Unable to write configuration file", buf, pkgclip);
        return FALSE;
    }

    if (strcmp (pkgclip->pacmanconf, PACMAN_CONF) != 0)
    {
        snprintf (buf, 1024, "PacmanConf = %s\n", pkgclip->pacmanconf);
        if (EOF == fputs (buf, fp))
            goto err_save;
    }

    if (!pkgclip->autoload)
        if (EOF == fputs ("NoAutoload\n", fp))
            goto err_save;

    if (!pkgclip->old_pkgrel)
        if (EOF == fputs ("PkgrelNoSpecial\n", fp))
            goto err_save;

    if (pkgclip->nb_old_ver != 1)
    {
        snprintf (buf, 1024, "NbOldVersion = %d\n", pkgclip->nb_old_ver);
        if (EOF == fputs (buf, fp))
            goto err_save;
    }

    if (pkgclip->nb_old_ver_ai != 0)
    {
        snprintf (buf, 1024, "NbOldVersionAsInstalled = %d\n", pkgclip->nb_old_ver_ai);
        if (EOF == fputs (buf, fp))
            goto err_save;
    }

    if (!pkgclip->show_pkg_info)
        if (EOF == fputs ("HidePkgInfo\n", fp))
            goto err_save;

    s = get_tpl_pkg_info (pkgclip);
    if (strcmp (s, PKG_INFO_TPL) != 0)
    {
        if (EOF == fputs ("PkgInfo = \"", fp))
            goto err_save;
        if (EOF == fputs (s, fp))
            goto err_save;
        if (EOF == fputs ("\"\n", fp))
            goto err_save;
    }
    free (s);

    buf[0] = '\0';
    s = buf;
    for (i = pkgclip->as_installed; i; i = alpm_list_next (i))
    {
        nb = snprintf (s, (size_t) len, " %s", (char *) i->data);
        len -= nb;
        s += nb;
    }
    if (len < 0)
        goto err_save;
    if (len < 1024)
    {
        if (EOF == fputs ("AsInstalled =", fp))
            goto err_save;
        if (EOF == fputs (buf, fp))
            goto err_save;
        if (EOF == fputs ("\n", fp))
            goto err_save;
    }

    if (pkgclip->recomm[REASON_NEWER_THAN_INSTALLED] != RECOMM_KEEP)
        if (EOF == fputs ("RecommForNewerThanInstalled = Remove\n", fp))
            goto err_save;
    if (pkgclip->recomm[REASON_INSTALLED] != RECOMM_KEEP)
        if (EOF == fputs ("RecommForInstalled = Remove\n", fp))
            goto err_save;
    if (pkgclip->recomm[REASON_OLDER_VERSION] != RECOMM_KEEP)
        if (EOF == fputs ("RecommForOlderVersion = Remove\n", fp))
            goto err_save;
    if (pkgclip->recomm[REASON_ALREADY_OLDER_VERSION] != RECOMM_REMOVE)
        if (EOF == fputs ("RecommForAlreadyOlderVersion = Keep\n", fp))
            goto err_save;
    if (pkgclip->recomm[REASON_OLDER_PKGREL] != RECOMM_REMOVE)
        if (EOF == fputs ("RecommForOlderPkgrel = Keep\n", fp))
            goto err_save;
    if (pkgclip->recomm[REASON_PKG_NOT_INSTALLED] != RECOMM_REMOVE)
        if (EOF == fputs ("RecommForPkgNotInstalled = Keep\n", fp))
            goto err_save;

    fclose (fp);
    return TRUE;

err_save:
    snprintf (buf, 1024, "%s: %s", file, strerror (errno));
    show_error ("Unable to write configuration file", buf, pkgclip);
    fclose (fp);
    unlink (file); /* try to avoid leaving half-saved file */
    return FALSE;
}

void
free_pkgclip (pkgclip_t *pkgclip)
{
    if (NULL == pkgclip->pacmanconf)
    {
        free (pkgclip->pacmanconf);
        pkgclip->pacmanconf = NULL;
    }
    if (NULL == pkgclip->dbpath)
    {
        free (pkgclip->dbpath);
        pkgclip->dbpath = NULL;
    }
    if (NULL == pkgclip->rootpath)
    {
        free (pkgclip->rootpath);
        pkgclip->rootpath = NULL;
    }
    if (NULL == pkgclip->cachedirs)
    {
        FREELIST (pkgclip->cachedirs);
    }
    free (pkgclip->pkg_info);
    alpm_list_free (pkgclip->pkg_info_extras);
    if (pkgclip->str_info)
        g_string_free (pkgclip->str_info, TRUE);
    free (pkgclip);
}
