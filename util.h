/**
 * PkgClip - Copyright (C) 2012-2016 Olivier Brunel
 *
 * util.h
 * Copyright (C) 2012-2016 Olivier Brunel <jjk@jjacky.com>
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

#ifndef _PKGCLIP_UTIL_H
#define _PKGCLIP_UTIL_H

#define PKG_INFO_TPL    "<b>$NAME</b> $VERSION\\t<i>$FILE\\t($SIZE)</i>\\n$DESC\\n$REASON [$RECOMM]"

char * strtrim (char *str);
double humanize_size (off_t bytes, const char target_unit, const char **label);
void show_error (const gchar *message, const gchar *submessage, pkgclip_t *pkgclip);
gboolean confirm (const gchar *message, const gchar *submessage,
                  const gchar *btn_yes_label, const gchar *btn_yes_image,
                  const gchar *btn_no_label, const gchar *btn_no_image,
                  pkgclip_t *pkgclip);
void parse_pacmanconf (pkgclip_t *pkgclip);
char * get_tpl_pkg_info (pkgclip_t *pkgclip);
void load_pkg_info (pkgclip_t *pkgclip);
pkgclip_t * new_pkgclip (void);
gboolean save_config (pkgclip_t *pkgclip);
void free_pkgclip (pkgclip_t *pkgclip);

#endif /* _PKGCLIP_UTIL_H */
