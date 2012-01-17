/**
 * PkgClip - Copyright (C) 2012 Olivier Brunel
 *
 * pkgclip-dbus.c
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

/* C */
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

/* PolicyKit */
#include <polkit/polkit.h>

/* gio - for dbus */
#include <gio/gio.h>

#define _UNUSED_                __attribute__ ((unused)) 

static GDBusNodeInfo *introspection_data = NULL;

/* Introspection data for the service we are exporting */
static const gchar introspection_xml[] =
  "<node>"
  "  <interface name='org.jjk.PkgClip.ClipperInterface'>"
  "    <method name='RemovePackages'>"
  "      <arg type='as' name='packages'   direction='in'/>"
  "      <arg type='i'  name='processed'  direction='out'/>"
  "    </method>"
  "    <signal name='RemoveSuccess'>"
  "      <arg type='s' name='package' />"
  "    </signal>"
  "    <signal name='RemoveFailure'>"
  "      <arg type='s' name='package' />"
  "      <arg type='s' name='error' />"
  "    </signal>"
  "  </interface>"
  "</node>";

static GMainLoop *loop;

static void
handle_method_call (GDBusConnection       *connection,
                    const gchar           *sender,
                    const gchar           *object_path,
                    const gchar           *interface_name,
                    const gchar           *method_name,
                    GVariant              *parameters,
                    GDBusMethodInvocation *invocation,
                    gpointer               data _UNUSED_)
{
    /* only one method we support */
    if (g_strcmp0 (method_name, "RemovePackages") != 0)
    {
        return;
    }
    
    /* first off, check auth */
    GError *error = NULL;
    PolkitAuthority *authority;
    PolkitSubject *subject;
    PolkitAuthorizationResult *result;

    authority = polkit_authority_get_sync (NULL, NULL);
    subject = polkit_system_bus_name_new (sender);
    result = polkit_authority_check_authorization_sync (
                authority,
                subject, 
                "org.jjk.pkgclip.removepkgs",
                NULL,
                POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION,
                NULL,
                &error);
    if (result == NULL)
    {
        g_dbus_method_invocation_return_gerror (invocation, error);
        return;
    }
    if (!polkit_authorization_result_get_is_authorized (result))
    {
        g_object_unref (result);
        g_dbus_method_invocation_return_dbus_error (
            invocation,
            "org.jjk.PkgClip.AuthError",
            "Authorization from PolicyKit failed");
        return;
    }
    g_object_unref (result);
    
    /* ok, now do the work */
    GVariantIter *iter;
    const gchar *pkg;
    guint processed = 0;
    
    g_variant_get (parameters, "(as)", &iter);
    while (g_variant_iter_loop (iter, "s", &pkg))
    {
        ++processed;
        if (unlink (pkg) == 0)
        {
            g_dbus_connection_emit_signal (connection,
                                           sender,
                                           object_path,
                                           interface_name,
                                           "RemoveSuccess",
                                           g_variant_new ("(s)", pkg),
                                           &error);
            g_assert_no_error (error);
        }
        else
        {
            g_dbus_connection_emit_signal (connection,
                                           sender,
                                           object_path,
                                           interface_name,
                                           "RemoveFailure",
                                           g_variant_new ("(ss)", pkg, strerror (errno)),
                                           &error);
            g_assert_no_error (error);
        }
    }
    g_variant_iter_free (iter);
    
    g_dbus_method_invocation_return_value (invocation,
                                           g_variant_new ("(i)", processed));
    /* we have no reason to keep running at this point */
    g_main_loop_quit (loop);
}

static void
on_bus_acquired (GDBusConnection *connection,
                 const gchar     *name _UNUSED_,
                 gpointer         user_data _UNUSED_)
{
    guint registration_id;
    GDBusInterfaceVTable interface_vtable;
    memset (&interface_vtable, 0, sizeof (GDBusInterfaceVTable));
    interface_vtable.method_call  = handle_method_call;

    registration_id = g_dbus_connection_register_object (
                        connection,
                        "/org/jjk/PkgClip/Clipper",
                        introspection_data->interfaces[0],
                        &interface_vtable,
                        NULL,
                        NULL,
                        NULL);
    g_assert (registration_id > 0);
}

static void
on_name_acquired (GDBusConnection *connection _UNUSED_,
                  const gchar     *name _UNUSED_,
                  gpointer         user_data _UNUSED_)
{
    /* void */
}

static void
on_name_lost (GDBusConnection *connection _UNUSED_,
              const gchar     *name _UNUSED_,
              gpointer         user_data _UNUSED_)
{
  g_main_loop_quit (loop);
}


int
main (int argc _UNUSED_, char *argv[] _UNUSED_)
{
  guint owner_id;

  g_type_init ();

  /* We are lazy here - we don't want to manually provide
   * the introspection data structures - so we just build
   * them from XML.
   */
  introspection_data = g_dbus_node_info_new_for_xml (introspection_xml, NULL);
  g_assert (introspection_data != NULL);

  owner_id = g_bus_own_name (G_BUS_TYPE_SYSTEM,
                             "org.jjk.PkgClip",
                             G_BUS_NAME_OWNER_FLAGS_NONE,
                             on_bus_acquired,
                             on_name_acquired,
                             on_name_lost,
                             NULL,
                             NULL);

  loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (loop);

  g_bus_unown_name (owner_id);
  g_dbus_node_info_unref (introspection_data);
  return 0;
}

