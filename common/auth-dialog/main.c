/* SPDX-License-Identifier: MIT */
/* The VPN auth dialog in external UI mode only: tells nmcli (and other agents that support the mode) which
 * secrets to ask for, so that `nmcli --ask` and `passwd-file` work. plasma-nm asks through the editor plugin. */
#include <stdio.h>
#include <unistd.h>

#include "pnv-daemon.h"

int main(int argc, char **argv)
{
    g_autofree char *uuid = NULL, *name = NULL, *service = NULL;
    gboolean external = FALSE, interactive = FALSE, renew = FALSE;
    g_auto(GStrv) hints = NULL;
    GOptionEntry options[] = {
        {"uuid", 'u', 0, G_OPTION_ARG_STRING, &uuid, "Connection UUID", "UUID"},
        {"name", 'n', 0, G_OPTION_ARG_STRING, &name, "Connection name", "NAME"},
        {"service", 's', 0, G_OPTION_ARG_STRING, &service, "VPN service type", "SERVICE"},
        {"external-ui-mode", 0, 0, G_OPTION_ARG_NONE, &external, "Describe the secrets, do not ask", NULL},
        {"allow-interaction", 'i', 0, G_OPTION_ARG_NONE, &interactive, "Allow user interaction", NULL},
        {"reprompt", 'r', 0, G_OPTION_ARG_NONE, &renew, "Ask for new secrets", NULL},
        {"hint", 't', 0, G_OPTION_ARG_STRING_ARRAY, &hints, "Secret hints (ignored)", "HINT"},
        {NULL},
    };
    g_autoptr(GOptionContext) context = g_option_context_new(NULL);
    g_autoptr(GError) error = NULL;
    g_autoptr(GHashTable) data = NULL, secrets = NULL;
    g_autoptr(NMSettingVpn) s_vpn = NULL;
    g_autoptr(GKeyFile) out = g_key_file_new();
    g_autofree char *text = NULL, *description = NULL;
    const PnvSecret *known;

    g_option_context_add_main_entries(context, options, NULL);
    if (!g_option_context_parse(context, &argc, &argv, &error)) {
        g_printerr("%s\n", error->message);
        return 1;
    }
    if (!external) {
        g_printerr("only --external-ui-mode is supported; plasma-nm asks for the secrets itself\n");
        return 1;
    }
    known = pnv_service_secrets(service);
    if (!known) {
        g_printerr("unknown service type %s\n", service ? service : "(none)");
        return 1;
    }
    if (!nm_vpn_service_plugin_read_vpn_details(STDIN_FILENO, &data, &secrets)) {
        g_printerr("cannot read the connection details\n");
        return 1;
    }

    s_vpn = NM_SETTING_VPN(nm_setting_vpn_new());
    g_object_set(s_vpn, NM_SETTING_VPN_DATA, data, NM_SETTING_VPN_SECRETS, secrets, NULL);
    g_key_file_set_integer(out, "VPN Plugin UI", "Version", 2);
    g_key_file_set_string(out, "VPN Plugin UI", "Title", "Authenticate VPN");
    description = g_strdup_printf("Secrets are required to connect to '%s'.", name ? name : uuid ? uuid : "the VPN");
    g_key_file_set_string(out, "VPN Plugin UI", "Description", description);
    for (const PnvSecret *secret = known; secret->key; secret++) {
        const char *value = nm_setting_vpn_get_secret(s_vpn, secret->key);

        g_key_file_set_string(out, secret->key, "Value", value ? value : "");
        g_key_file_set_string(out, secret->key, "Label", secret->label);
        g_key_file_set_boolean(out, secret->key, "IsSecret", TRUE);
        g_key_file_set_boolean(out, secret->key, "ShouldAsk",
                               interactive && (renew || pnv_secret_missing(s_vpn, secret->key)));
    }
    text = g_key_file_to_data(out, NULL, NULL);
    fputs(text, stdout);
    return fflush(stdout) == 0 ? 0 : 1;
}
