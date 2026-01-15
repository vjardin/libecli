/*
 * CLI Command Definition Macros (DEFUN-like)
 *
 * Copyright (C) 2026 Free Mobile, Vincent Jardin <vjardin@free.fr>
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This library provides DEFUN macros for defining CLI commands using
 * libecoli. Commands are automatically registered at startup
 * using libecoli's initialization system.
 *
 * QUICK REFERENCE
 *
 * SETUP (required once per application):
 *   ECLI_CMD_CTX()                    - Initialize CLI command context
 *
 * COMMAND DEFINITION:
 *   ECLI_DEFUN(name, yaml_cb, cmd, help, args...)
 *       Define a top-level command (e.g., "quit", "help")
 *
 *   ECLI_DEFUN_ALIAS(name, cmd, target_cb)
 *       Define an alias for an existing command (e.g., "?" -> help)
 *
 *   ECLI_DEFUN_GROUP(grp, keyword, help)
 *       Define a command group (e.g., "show", "set", "vhost")
 *
 *   ECLI_DEFUN_SUB(grp, name, yaml_cb, cmd, help, args...)
 *       Define a subcommand within a group (e.g., "show status")
 *
 *   ECLI_DEFUN_SET(grp, name, yaml_cb, cmd, help, fmt, group, prio, args...)
 *       Define a config-changing subcommand with output support
 *
 *   ECLI_DEFUN_OUT(grp, name)
 *       Define the output function for a DEFUN_SET command
 *
 * OUTPUT HELPERS:
 *   ECLI_OUT(cli, fp, fmt, ...)       - Simple printf-style output
 *   ECLI_OUT_FMT(cli, fp, fmt, ...)   - Output with named {placeholders}
 *
 * FORMAT TYPE TAGS (for ECLI_OUT_FMT):
 *   FMT_STR   - const char *
 *   FMT_INT   - int (signed)
 *   FMT_UINT  - unsigned int
 *   FMT_LONG  - long
 *   FMT_ULONG - unsigned long
 *
 * ARCHITECTURE OVERVIEW
 *
 * The CLI uses libecoli's constructor-based initialization system. Commands
 * are registered automatically at startup without explicit registration calls.
 *
 * Initialization order (by priority):
 *   110 - Root "or" node created (ECLI_CMD_CTX)
 *   115 - Group "or" nodes created (ECLI_DEFUN_GROUP)
 *   120 - Commands registered (ECLI_DEFUN, ECLI_DEFUN_SUB, ECLI_DEFUN_SET)
 *   125 - Groups attached to root (ECLI_DEFUN_GROUP)
 *   190 - Grammar wrapped with sh_lex tokenizer (ECLI_CMD_CTX)
 *
 * Grammar structure:
 *   sh_lex
 *     or (root)
 *       cmd "quit"
 *       cmd "help"
 *       seq
 *         str "show"
 *         or (show group)
 *           cmd "status"
 *           cmd "config"
 *       seq
 *         str "vhost"
 *         or (vhost group)
 *           cmd "add hostname port docroot"
 *           cmd "del hostname"
 *
 * COMPLETE EXAMPLE APPLICATION
 *
 * // main.c - Minimal CLI application
 *
 * #include "ecli_cmd.h"
 * #include "ecli_types.h"
 *
 * // Required: Initialize CLI command context
 * ECLI_CMD_CTX()
 *
 * static bool g_running = true;
 *
 * // Simple top-level command
 * ECLI_DEFUN(quit, "quit", "quit", "exit the application")
 * {
 *     g_running = false;
 *     ecli_output(cli, "Goodbye!\n");
 *     return 0;
 * }
 *
 * // Alias: "exit" also quits
 * ECLI_DEFUN_ALIAS(exit_cmd, "exit", quit)
 *
 * // Command group for "show" commands
 * ECLI_DEFUN_GROUP(show, "show", "display information")
 *
 * // Subcommand: "show version"
 * ECLI_DEFUN_SUB(show, version, "show_version", "version", "display version")
 * {
 *     ecli_output(cli, "Version 1.0.0\n");
 *     return 0;
 * }
 *
 * int main(void) {
 *     ec_init();
 *     eecli_ctx_t *cli = ecli_init(&(ecli_config_t){ .prompt = "app> " });
 *     while (g_running) ecli_process(cli);
 *     ecli_shutdown(cli);
 *     return 0;
 * }
 *
 * DETAILED EXAMPLES
 *
 * Example 1: Simple command without arguments
 * --------------------------------------------
 *
 *   ECLI_DEFUN(help, "help", "help", "show available commands")
 *   {
 *       ecli_output(cli, "Available commands:\n");
 *       ecli_output(cli, "  help  - show this message\n");
 *       ecli_output(cli, "  quit  - exit the application\n");
 *       return 0;
 *   }
 *
 *   // Usage:
 *   httpd> help
 *   Available commands:
 *     help  - show this message
 *     quit  - exit the application
 *
 *
 * Example 2: Command with arguments
 * ----------------------------------
 *
 *   ECLI_DEFUN(listen, "listen", "listen address port",
 *       "set server listen address and port",
 *       ECLI_ARG_NAME("address", "IP address or hostname"),
 *       ECLI_ARG_PORT("port", "TCP port number"))
 *   {
 *       const char *addr = ec_pnode_get_str(parse, "address");
 *       int64_t port = ec_pnode_get_int(parse, "port");
 *       ecli_output(cli, "Server listening on %s:%ld\n", addr, port);
 *       return 0;
 *   }
 *
 *   // Usage:
 *   httpd> listen 0.0.0.0 8080
 *   Server listening on 0.0.0.0:8080
 *
 *
 * Example 3: Command alias
 * -------------------------
 *
 *   // Main help command
 *   ECLI_DEFUN(help, "help", "help", "show available commands")
 *   {
 *       // ... help implementation
 *       return 0;
 *   }
 *
 *   // "?" is an alias for help (no help text shown in command list)
 *   ECLI_DEFUN_ALIAS(qmark, "?", help)
 *
 *   // Usage:
 *   httpd> ?
 *   (same output as "help")
 *
 *
 * Example 4: Command group with subcommands
 * ------------------------------------------
 *
 *   // Define the "show" command group
 *   ECLI_DEFUN_GROUP(show, "show", "display server information")
 *
 *   // Subcommand: "show status"
 *   ECLI_DEFUN_SUB(show, status, "show_status", "status", "display server status")
 *   {
 *       ecli_output(cli, "Server Status: Running\n");
 *       ecli_output(cli, "Active connections: 42\n");
 *       return 0;
 *   }
 *
 *   // Subcommand: "show vhosts"
 *   ECLI_DEFUN_SUB(show, vhosts, "show_vhosts",
 *       "vhosts", "display virtual hosts")
 *   {
 *       ecli_output(cli, "api.example.com:443  -> /var/www/api\n");
 *       ecli_output(cli, "www.example.com:443  -> /var/www/html\n");
 *       return 0;
 *   }
 *
 *   // Usage:
 *   httpd> show status
 *   Server Status: Running
 *   Active connections: 42
 *
 *   httpd> show vhosts
 *   api.example.com:443  -> /var/www/api
 *   www.example.com:443  -> /var/www/html
 *
 *
 * Example 5: Subcommand with arguments
 * -------------------------------------
 *
 *   ECLI_DEFUN_GROUP(vhost, "vhost", "virtual host management")
 *
 *   ECLI_DEFUN_SUB(vhost, add, "vhost_add",
 *       "add hostname port docroot",
 *       "add a new virtual host",
 *       ECLI_ARG_NAME("hostname", "server hostname"),
 *       ECLI_ARG_PORT("port", "listen port"),
 *       ECLI_ARG_PATH("docroot", "document root path"))
 *   {
 *       const char *host = ec_pnode_get_str(parse, "hostname");
 *       int64_t port = ec_pnode_get_int(parse, "port");
 *       const char *root = ec_pnode_get_str(parse, "docroot");
 *       ecli_output(cli, "Adding vhost '%s:%ld' -> %s\n", host, port, root);
 *       return 0;
 *   }
 *
 *   ECLI_DEFUN_SUB(vhost, del, "vhost_del",
 *       "del hostname",
 *       "delete a virtual host",
 *       ECLI_ARG_NAME("hostname", "server hostname"))
 *   {
 *       const char *host = ec_pnode_get_str(parse, "hostname");
 *       ecli_output(cli, "Deleting vhost '%s'\n", host);
 *       return 0;
 *   }
 *
 *   // Usage:
 *   httpd> vhost add api.example.com 443 /var/www/api
 *   Adding vhost 'api.example.com:443' -> /var/www/api
 *
 *   httpd> vhost del api.example.com
 *   Deleting vhost 'api.example.com'
 *
 *
 * Example 6: Config command with "write terminal" support
 * --------------------------------------------------------
 *
 *   static struct {
 *       int max_connections;
 *       int keepalive_timeout;
 *   } g_config = { .max_connections = 1000, .keepalive_timeout = 60 };
 *
 *   ECLI_DEFUN_GROUP(set, "set", "configure server settings")
 *
 *   // Config command with output registration
 *   ECLI_DEFUN_SET(set, max_conn, "set_max_connections",
 *       "max-connections value",               // Command syntax
 *       "set maximum concurrent connections",  // Help text
 *       "set max-connections {value}\n",       // Output format
 *       "server",                              // Output group
 *       10,                                    // Output priority
 *       ECLI_ARG_UINT("value", 10000, "max connections (1-10000)"))
 *   {
 *       g_config.max_connections = (int)ec_pnode_get_int(parse, "value");
 *       ecli_output(cli, "Max connections set to %d\n", g_config.max_connections);
 *       return 0;
 *   }
 *
 *   // Output function - called by "write terminal"
 *   ECLI_DEFUN_OUT(set, max_conn)
 *   {
 *       ECLI_OUT_FMT(cli, fp, fmt,
 *           "value", FMT_INT, g_config.max_connections,
 *           NULL);
 *   }
 *
 *   // Usage:
 *   httpd> set max-connections 2000
 *   Max connections set to 2000
 *
 *   httpd> write terminal
 *   ! server configuration
 *   set max-connections 2000
 *
 *
 * Example 7: Multiple config commands with grouping
 * --------------------------------------------------
 *
 *   ECLI_DEFUN_SET(set, keepalive, "set_keepalive",
 *       "keepalive-timeout value",
 *       "set HTTP keep-alive timeout",
 *       "set keepalive-timeout {value}\n",
 *       "server",   // Same group as max-connections
 *       20,         // Higher priority = later in output
 *       ECLI_ARG_TIMEOUT("value", 300, "seconds (1-300)"))
 *   {
 *       g_config.keepalive_timeout = (int)ec_pnode_get_int(parse, "value");
 *       return 0;
 *   }
 *
 *   ECLI_DEFUN_OUT(set, keepalive)
 *   {
 *       ECLI_OUT_FMT(cli, fp, fmt,
 *           "value", FMT_INT, g_config.keepalive_timeout,
 *           NULL);
 *   }
 *
 *   // Usage:
 *   httpd> write terminal
 *   ! server configuration
 *   set max-connections 2000
 *   set keepalive-timeout 120
 *
 *
 * Example 8: Complex output with multiple placeholders
 * -----------------------------------------------------
 *
 *   static struct {
 *       char hostname[64];
 *       int port;
 *       char docroot[256];
 *   } g_vhost = { .hostname = "www.example.com", .port = 443, .docroot = "/var/www/html" };
 *
 *   ECLI_DEFUN_SET(vhost, create, "vhost_create",
 *       "create hostname port docroot",
 *       "create a virtual host",
 *       "vhost create {hostname} port {port} root {docroot}\n",
 *       "vhosts",
 *       100,
 *       ECLI_ARG_NAME("hostname", "server hostname"),
 *       ECLI_ARG_PORT("port", "listen port"),
 *       ECLI_ARG_PATH("docroot", "document root"))
 *   {
 *       snprintf(g_vhost.hostname, sizeof(g_vhost.hostname), "%s",
 *                ec_pnode_get_str(parse, "hostname"));
 *       g_vhost.port = (int)ec_pnode_get_int(parse, "port");
 *       snprintf(g_vhost.docroot, sizeof(g_vhost.docroot), "%s",
 *                ec_pnode_get_str(parse, "docroot"));
 *       return 0;
 *   }
 *
 *   ECLI_DEFUN_OUT(vhost, create)
 *   {
 *       ECLI_OUT_FMT(cli, fp, fmt,
 *           "hostname", FMT_STR, g_vhost.hostname,
 *           "port",     FMT_INT, g_vhost.port,
 *           "docroot",  FMT_STR, g_vhost.docroot,
 *           NULL);
 *   }
 *
 *   // Output with named placeholders allows translation to reorder:
 *   // English: "vhost create {hostname} port {port} root {docroot}\n"
 *   // French:  "vhost creer {hostname} racine {docroot} port {port}\n"
 *
 *
 * EXTRACTING ARGUMENTS IN HANDLERS
 *
 * Use libecoli's ec_pnode functions to extract argument values:
 *
 *   // Get string value (returns NULL if not found)
 *   const char *str = ec_pnode_get_str(parse, "id");
 *
 *   // Get integer value (returns 0 if not found or invalid)
 *   int64_t num = ec_pnode_get_int(parse, "id");
 *
 * Or use the convenience wrappers:
 *
 *   // Get string (returns NULL if not found)
 *   const char *str = ecli_arg_str(parse, "id");
 *
 *   // Get integer with default value
 *   int num = ecli_arg_int(parse, "id", default_value);
 *
 * The "id" parameter must match the id in your ECLI_ARG_* macro:
 *
 *   ECLI_DEFUN_SUB(show, port, "show_port",
 *       "port slot",
 *       "display port info",
 *       ECLI_ARG_SLOT("slot", 48, "port number"))  // <-- "slot" is the id
 *   {
 *       int slot = ecli_arg_int(parse, "slot", 1);  // <-- use same id
 *       // ...
 *   }
 *
 *
 * YAML CALLBACK NAMING
 *
 * The yaml_cb parameter creates a mapping between callback names and functions.
 * This enables:
 *   1. YAML grammar export with human-readable callback names
 *   2. Grammar translation/customization via YAML files
 *   3. Output format overrides in companion YAML files
 *
 * Naming conventions:
 *   - Use snake_case: "show_status", "vhost_add", "set_keepalive"
 *   - Include the group prefix: "show_version" not just "version"
 *   - Be descriptive: "vhost_set_docroot" not "vh_root"
 *
 * The yaml_cb is stored in the grammar tree and exported with "write yaml".
 * When loading a translated grammar, callbacks are matched by name.
 *
 *
 * OUTPUT FORMAT STRINGS
 *
 * ECLI_OUT_FMT uses {name} placeholders that are substituted at runtime:
 *
 *   Format string: "set debounce {value}\n"
 *   Arguments:     "value", FMT_INT, 50, NULL
 *   Output:        "set debounce 50\n"
 *
 * Benefits of named placeholders:
 *   - Translations can reorder parameters
 *   - Self-documenting format strings
 *   - Type-safe with explicit FMT_* tags
 *
 * Type tags:
 *   FMT_STR   - String (const char *)
 *   FMT_INT   - Signed int
 *   FMT_UINT  - Unsigned int
 *   FMT_LONG  - Signed long
 *   FMT_ULONG - Unsigned long
 *
 * The argument list must end with NULL.
 *
 *
 * WRITE TERMINAL/FILE ARCHITECTURE
 *
 * The "write terminal" and "write file" commands dump the running configuration
 * as executable CLI commands. This enables:
 *   - Configuration backup and restore
 *   - Configuration transfer between devices
 *   - Audit trails of configuration changes
 *
 * SYSTEM OVERVIEW
 * ---------------
 *
 *   ┌─────────────────┐     ┌──────────────────┐     ┌─────────────────┐
 *   │  ECLI_DEFUN_SET  │────▶│  Output Registry │────▶│ write terminal  │
 *   │  (registers     │     │  (linked list    │     │ write file      │
 *   │   output func)  │     │   sorted by      │     │ (iterates and   │
 *   └─────────────────┘     │   priority)      │     │  calls funcs)   │
 *                           └──────────────────┘     └─────────────────┘
 *           │                        │                        │
 *           ▼                        ▼                        ▼
 *   ┌─────────────────┐     ┌──────────────────┐     ┌─────────────────┐
 *   │  ECLI_DEFUN_OUT  │     │  Format Lookup   │     │  Output Stream  │
 *   │  (output func   │     │  (YAML override  │     │  (CLI or FILE)  │
 *   │   implementation│     │   or default)    │     │                 │
 *   └─────────────────┘     └──────────────────┘     └─────────────────┘
 *
 * HOW IT WORKS
 * ------------
 *
 * 1. REGISTRATION (at startup):
 *    ECLI_DEFUN_SET automatically calls ecli_out_register() which adds
 *    the output function to a global linked list, sorted by priority.
 *
 * 2. EXECUTION (when user types "write terminal"):
 *    ecli_dump_running_config() iterates through the registry, calling
 *    each output function in priority order. Functions are grouped
 *    by their "group" parameter for organized output.
 *
 * 3. OUTPUT FUNCTION:
 *    Each ECLI_DEFUN_OUT function reads current state and outputs
 *    the CLI command(s) that would recreate that state.
 *
 * REGISTRATION PARAMETERS
 * -----------------------
 *
 * ECLI_DEFUN_SET(grp, name, yaml_cb, cmdstr, helpstr,
 *               out_fmt, out_group, out_prio, args...)
 *
 *   out_fmt   - Default format string with {name} placeholders
 *               Example: "vhost add {hostname} port {port}\n"
 *
 *   out_group - Group name for organizing output sections
 *               Example: "vhosts", "server", "network"
 *               Output is grouped: "! vhosts" header, then all
 *               vhost-related commands together.
 *
 *   out_prio  - Priority for output ordering (lower = earlier)
 *               Example: 10 = early, 100 = middle, 500 = late
 *               Use priorities to ensure dependent config comes first.
 *
 * OUTPUT ORGANIZATION
 * -------------------
 *
 * The "write terminal" output is organized by groups:
 *
 *   ! server
 *   set max-connections 2000
 *   set keepalive-timeout 120
 *   ! network
 *   set hostname www.example.com
 *   set listen-address 0.0.0.0
 *   ! vhosts
 *   vhost add api.example.com port 443 root /var/www/api
 *   vhost add www.example.com port 443 root /var/www/html
 *
 * Groups are determined by the out_group parameter in ECLI_DEFUN_SET.
 * Within each group, commands appear in priority order.
 *
 * PRIORITY GUIDELINES
 * -------------------
 *
 *   Priority Range    Usage
 *   ──────────────    ─────────────────────────────────
 *   1-50              Core system settings (hostname, etc.)
 *   51-100            Infrastructure (interfaces, VLANs)
 *   101-200           Services (routing, DHCP, DNS)
 *   201-500           Application config (users, policies)
 *   501+              Dependent config (requires earlier items)
 *
 * Choose priorities so that when the output is replayed, dependencies
 * are satisfied. For example, a VLAN must exist before assigning
 * ports to it.
 *
 * COMPLETE IMPLEMENTATION EXAMPLE
 * -------------------------------
 *
 *   // Application state
 *   static struct {
 *       char server_name[64];
 *       int  max_connections;
 *       struct {
 *           char hostname[64];
 *           int  port;
 *           char docroot[256];
 *       } vhosts[10];
 *       int vhost_count;
 *   } g_config = {
 *       .server_name = "httpd",
 *       .max_connections = 1000,
 *       .vhost_count = 0
 *   };
 *
 *   // Define "set" group
 *   ECLI_DEFUN_GROUP(set_cmd, "set", "configure settings")
 *
 *   // Server name configuration (early priority)
 *   ECLI_DEFUN_SET(set_cmd, server_name, "set_server_name",
 *       "server-name name",
 *       "set server name",
 *       "set server-name {name}\n",
 *       "server",              // Group: server settings
 *       10,                    // Priority: very early
 *       ECLI_ARG_NAME("name", "server name"))
 *   {
 *       snprintf(g_config.server_name, sizeof(g_config.server_name),
 *                "%s", ec_pnode_get_str(parse, "name"));
 *       ecli_output(cli, "Server name set to '%s'\n", g_config.server_name);
 *       return 0;
 *   }
 *
 *   ECLI_DEFUN_OUT(set_cmd, server_name)
 *   {
 *       // Only output if different from default
 *       if (strcmp(g_config.server_name, "httpd") != 0) {
 *           ECLI_OUT_FMT(cli, fp, fmt,
 *               "name", FMT_STR, g_config.server_name,
 *               NULL);
 *       }
 *   }
 *
 *   // Max connections configuration
 *   ECLI_DEFUN_SET(set_cmd, max_conn, "set_max_connections",
 *       "max-connections value",
 *       "set maximum connections",
 *       "set max-connections {value}\n",
 *       "server",              // Same group as server-name
 *       20,                    // After server-name
 *       ECLI_ARG_UINT("value", 10000, "max connections"))
 *   {
 *       g_config.max_connections = (int)ec_pnode_get_int(parse, "value");
 *       ecli_output(cli, "Max connections set to %d\n", g_config.max_connections);
 *       return 0;
 *   }
 *
 *   ECLI_DEFUN_OUT(set_cmd, max_conn)
 *   {
 *       if (g_config.max_connections != 1000) {  // Skip default
 *           ECLI_OUT_FMT(cli, fp, fmt,
 *               "value", FMT_INT, g_config.max_connections,
 *               NULL);
 *       }
 *   }
 *
 *   // Virtual host management group
 *   ECLI_DEFUN_GROUP(vhost_cmd, "vhost", "virtual host management")
 *
 *   // Add vhost (later priority, depends on nothing)
 *   ECLI_DEFUN_SET(vhost_cmd, add, "vhost_add",
 *       "add hostname port docroot",
 *       "add a virtual host",
 *       "vhost add {hostname} port {port} root {docroot}\n",
 *       "vhosts",              // Group: vhosts
 *       100,                   // Priority: middle
 *       ECLI_ARG_NAME("hostname", "server hostname"),
 *       ECLI_ARG_PORT("port", "listen port"),
 *       ECLI_ARG_PATH("docroot", "document root"))
 *   {
 *       int idx = g_config.vhost_count;
 *       if (idx >= 10) {
 *           ecli_output(cli, "Error: maximum vhosts reached\n");
 *           return -1;
 *       }
 *       snprintf(g_config.vhosts[idx].hostname,
 *                sizeof(g_config.vhosts[idx].hostname),
 *                "%s", ec_pnode_get_str(parse, "hostname"));
 *       g_config.vhosts[idx].port = (int)ec_pnode_get_int(parse, "port");
 *       snprintf(g_config.vhosts[idx].docroot,
 *                sizeof(g_config.vhosts[idx].docroot),
 *                "%s", ec_pnode_get_str(parse, "docroot"));
 *       g_config.vhost_count++;
 *       ecli_output(cli, "Vhost '%s:%d' added -> %s\n",
 *                  g_config.vhosts[idx].hostname,
 *                  g_config.vhosts[idx].port,
 *                  g_config.vhosts[idx].docroot);
 *       return 0;
 *   }
 *
 *   // Output ALL vhosts (loop in output function)
 *   ECLI_DEFUN_OUT(vhost_cmd, add)
 *   {
 *       for (int i = 0; i < g_config.vhost_count; i++) {
 *           ECLI_OUT_FMT(cli, fp, fmt,
 *               "hostname", FMT_STR, g_config.vhosts[i].hostname,
 *               "port",     FMT_INT, g_config.vhosts[i].port,
 *               "docroot",  FMT_STR, g_config.vhosts[i].docroot,
 *               NULL);
 *       }
 *   }
 *
 * EXAMPLE OUTPUT
 * --------------
 *
 *   httpd> set server-name myserver
 *   Server name set to 'myserver'
 *   httpd> set max-connections 2000
 *   Max connections set to 2000
 *   httpd> vhost add api.example.com 443 /var/www/api
 *   Vhost 'api.example.com:443' added -> /var/www/api
 *   httpd> vhost add www.example.com 443 /var/www/html
 *   Vhost 'www.example.com:443' added -> /var/www/html
 *   httpd> write terminal
 *   ! server
 *   set server-name myserver
 *   set max-connections 2000
 *   ! vhosts
 *   vhost add api.example.com port 443 root /var/www/api
 *   vhost add www.example.com port 443 root /var/www/html
 *
 * FILE VS TERMINAL OUTPUT
 * -----------------------
 *
 * The output functions receive both cli and fp parameters:
 *
 *   - "write terminal": fp is NULL, output goes to ecli_output()
 *   - "write file X":   fp is open FILE*, output goes to fprintf()
 *
 * ECLI_OUT_FMT and ECLI_OUT handle this automatically:
 *
 *   ECLI_OUT_FMT(cli, fp, fmt, ...);  // Auto-routes based on fp
 *   ECLI_OUT(cli, fp, "text\n");      // Same auto-routing
 *
 * CONDITIONAL OUTPUT
 * ------------------
 *
 * Output functions can skip outputting default values:
 *
 *   ECLI_DEFUN_OUT(set_cmd, timeout)
 *   {
 *       // Only output if not the default value
 *       if (g_config.timeout_sec != DEFAULT_TIMEOUT) {
 *           ECLI_OUT_FMT(cli, fp, fmt,
 *               "seconds", FMT_INT, g_config.timeout_sec,
 *               NULL);
 *       }
 *   }
 *
 * This keeps "write terminal" output minimal and clean.
 *
 * YAML FORMAT OVERRIDES
 * ---------------------
 *
 * Format strings can be overridden via YAML for translation:
 *
 *   // Default (English) in C code:
 *   "vhost add {hostname} port {port} root {docroot}\n"
 *
 *   // Override in grammar_formats.yaml:
 *   output_formats:
 *     vhost_add: "vhost ajouter {hostname} racine {docroot} port {port}\n"
 *
 * The ECLI_DEFUN_OUT receives the correct format string (override or
 * default) via the fmt parameter. Named placeholders {name} allow
 * translations to reorder parameters.
 *
 * FUNCTIONS REFERENCE
 * -------------------
 *
 *   ecli_out_register(name, group, default_fmt, func, priority)
 *       Register an output function. Called by ECLI_DEFUN_SET.
 *
 *   ecli_dump_running_config(cli, fp)
 *       Dump all registered config. Called by "write terminal/file".
 *
 *   ecli_out_get_fmt(name, default_fmt)
 *       Get format string, checking for YAML override.
 *
 *   ecli_out_fmt(cli, fp, fmt, ...)
 *       Output with {placeholder} substitution (variadic).
 *
 *
 * CONTEXT MODES
 *
 * Commands can enter context modes using ecli_enter_context():
 *
 *   ECLI_DEFUN_SUB(interface_cmd, select, "interface_select",
 *       "interface ifname",
 *       "enter interface configuration mode",
 *       ECLI_ARG_IFNAME("ifname", "interface name"))
 *   {
 *       const char *ifname = ec_pnode_get_str(parse, "ifname");
 *       ecli_enter_context(cli, "interface", ifname);
 *       return 0;
 *   }
 *
 * In context mode:
 *   - Prompt changes: "app> " -> "app(interface-eth0)> "
 *   - "exit" returns to parent context
 *   - Context-specific commands become available
 *
 * Register a group as a context with ecli_register_context_group()
 * (done automatically by ECLI_DEFUN_GROUP).
 *
 *
 * ERROR HANDLING
 *
 * Command handlers return:
 *   0  - Success
 *   -1 - Error (error message should be printed by handler)
 *
 * Example:
 *   ECLI_DEFUN_SUB(vhost_cmd, del, "vhost_del",
 *       "del hostname", "delete a virtual host",
 *       ECLI_ARG_NAME("hostname", "server hostname"))
 *   {
 *       const char *hostname = ec_pnode_get_str(parse, "hostname");
 *       if (!vhost_exists(hostname)) {
 *           ecli_output(cli, "Error: vhost '%s' not found\n", hostname);
 *           return -1;
 *       }
 *       vhost_delete(hostname);
 *       return 0;
 *   }
 *

 */
#pragma once

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ecoli.h>
#include <ecoli/editline.h>
#include "ecli.h"
#include "ecli_yaml.h"

typedef struct eecli_ctx eecli_ctx_t;

typedef int (*ecli_cmd_cb_t)(eecli_ctx_t *cli, const struct ec_pnode *parse);

typedef void (*ecli_out_t)(eecli_ctx_t *cli, FILE *fp, const char *fmt);

typedef struct ecli_out_entry {
    struct ecli_out_entry *next;
    const char   *name;        /* Callback name (matches yaml_cb) */
    const char   *group;       /* Group name for organized output */
    const char   *default_fmt; /* Default format string from C code */
    ecli_out_t     func;        /* Output function pointer */
    int           priority;    /* Output order (lower = earlier) */
} ecli_out_entry_t;

typedef enum {
    ECLI_FMT_END = 0,   /* Sentinel - marks end of argument list */
    ECLI_FMT_STR,       /* const char * - string value */
    ECLI_FMT_INT,       /* int - signed integer */
    ECLI_FMT_UINT,      /* unsigned int - unsigned integer */
    ECLI_FMT_LONG,      /* long - signed long integer */
    ECLI_FMT_ULONG,     /* unsigned long - unsigned long integer */
} ecli_fmt_type_t;

/* Attribute keys for storing CLI metadata on ec_node */
#define ECLI_HELP_ATTR    "help"
#define ECLI_CB_ATTR      "cli.callback"
#define ECLI_CB_NAME_ATTR "callback"

/* Shorthand type tags for ECLI_OUT_FMT */
#define FMT_END   ECLI_FMT_END
#define FMT_STR   ECLI_FMT_STR
#define FMT_INT   ECLI_FMT_INT
#define FMT_UINT  ECLI_FMT_UINT
#define FMT_LONG  ECLI_FMT_LONG
#define FMT_ULONG ECLI_FMT_ULONG

static inline struct ec_node *
_cli_attr_help(const char *help, struct ec_node *node)
{
    if (node == NULL)
        return NULL;
    struct ec_dict *attrs = ec_node_attrs(node);
    if (attrs == NULL)
        goto fail;

    /* Set help attribute for ecli_show_help() */
    if (ec_dict_set(attrs, ECLI_HELP_ATTR, (void *)help, NULL) < 0)
        goto fail;

    /* Set editline help for tab completion display (second column) */
    if (ec_dict_set(attrs, EC_EDITLINE_HELP_ATTR, (void *)help, NULL) < 0)
        goto fail;

    /*
     * For non-str nodes (arguments like re, int, etc.), also set DESC_ATTR
     * to show a meaningful name in the first column instead of "<re>".
     * For str nodes, ec_node_desc() already returns the keyword.
     *
     * Use the node's ID if available (e.g., "ipv4" -> "<ipv4>"), otherwise
     * use a truncated version of the help text.
     */
    const char *node_type = ec_node_type_name(ec_node_type(node));
    if (strcmp(node_type, "str") != 0) {
        char desc_buf[32];
        const char *node_id = ec_node_id(node);
        if (node_id && strcmp(node_id, EC_NO_ID) != 0) {
            /* Use node ID for short description: <ipv4>, <filename>, etc. */
            snprintf(desc_buf, sizeof(desc_buf), "<%s>", node_id);
        } else {
            /* Fallback: use first word of help text */
            snprintf(desc_buf, sizeof(desc_buf), "<%s>", help);
            /* Truncate at first space or 18 chars */
            char *space = strchr(desc_buf + 1, ' ');
            if (space && space < desc_buf + 18)
                strcpy(space, ">");
            else if (strlen(desc_buf) > 18)
                strcpy(desc_buf + 15, "...>");
        }
        char *desc_copy = strdup(desc_buf);
        if (desc_copy == NULL)
            goto fail;
        if (ec_dict_set(attrs, EC_EDITLINE_DESC_ATTR, desc_copy, free) < 0) {
            free(desc_copy);
            goto fail;
        }
    }

    return node;
fail:
    ec_node_free(node);
    return NULL;
}

static inline struct ec_node *
_cli_attr_callback(ecli_cmd_cb_t cb, const char *cb_name, struct ec_node *node)
{
    if (node == NULL)
        return NULL;
    struct ec_dict *attrs = ec_node_attrs(node);
    if (attrs == NULL)
        goto fail;
    if (ec_dict_set(attrs, ECLI_CB_ATTR, (void *)cb, NULL) < 0)
        goto fail;
    if (cb_name && ec_dict_set(attrs, ECLI_CB_NAME_ATTR, (void *)cb_name, NULL) < 0)
        goto fail;
    /* Also set libecoli's editline callback attribute for ec_editline_interact() */
    if (ec_dict_set(attrs, EC_EDITLINE_CB_ATTR,
                    (void *)ecli_editline_cmd_wrapper, NULL) < 0)
        goto fail;
    return node;
fail:
    ec_node_free(node);
    return NULL;
}

#define _H(helpstr, node) _cli_attr_help((helpstr), (node))

/*
 * _cli_sub_keyword - Create a keyword str node with description for tab completion
 *
 * Use this as the first element when building custom command nodes with
 * ECLI_DEFUN_SUB_NODE. The description will appear in tab completion.
 *
 * Example:
 *   ECLI_DEFUN_SUB_NODE(show, doc, "show_doc", "display documentation",
 *       EC_NODE_SEQ(EC_NO_ID,
 *           _cli_sub_keyword("doc", "display documentation"),
 *           _H("command name", ec_node_re("cmd", "[a-z]+"))))
 */
#define _cli_sub_keyword(keyword, desc) \
    _H((desc), ec_node_str(EC_NO_ID, (keyword)))

/*
 * _cli_make_sub_node - Create a subcommand node with proper description handling
 *
 * For simple keywords (no spaces), uses ec_node_str with description visible
 * in tab completion. For commands with arguments, uses EC_NODE_CMD.
 */
static inline struct ec_node *
_cli_make_sub_node(const char *helpstr, const char *cmdstr)
{
    /* Simple keyword without args - use str node for tab completion desc */
    if (!strchr(cmdstr, ' ')) {
        return _H(helpstr, ec_node_str(EC_NO_ID, cmdstr));
    }
    /* Command with args - use EC_NODE_CMD (description on outer node) */
    return _H(helpstr, EC_NODE_CMD(EC_NO_ID, cmdstr));
}

/* Root node is provided by the library (ecli_root.c) */
extern struct ec_node *__cli_root;

/*
 * ECLI_CMD_CTX - Deprecated, no longer needed
 *
 * The library now provides the root node management automatically.
 * This macro is kept for backwards compatibility but does nothing.
 */
#define ECLI_CMD_CTX() /* provided by library */

#define ECLI_DEFUN(name, yaml_cb, cmdstr, helpstr, args...) \
    static int _cb_##name(eecli_ctx_t *cli, const struct ec_pnode *parse); \
    static int _reg_##name(void) { \
        ecli_yaml_register((yaml_cb), _cb_##name); \
        return ec_node_or_add(__cli_root, \
            _cli_attr_callback(_cb_##name, (yaml_cb), \
                _H((helpstr), EC_NODE_CMD(EC_NO_ID, (cmdstr), ##args)))); \
    } \
    static struct ec_init _init_##name = { \
        .init = _reg_##name, .exit = NULL, .priority = 120 \
    }; \
    EC_INIT_REGISTER(_init_##name); \
    static int _cb_##name( \
        eecli_ctx_t *cli __attribute__((unused)), \
        const struct ec_pnode *parse __attribute__((unused)))

/*
 * ECLI_DEFUN_ALIAS - Create an alias for an existing command
 *
 * Parameters:
 *   name    - Unique identifier for this alias
 *   cmdstr  - The alias string (e.g., "?" or "exit")
 *   helpstr - Help text for the alias (shown in help command)
 *   target  - The DEFUN name to alias (e.g., 'help' to alias ECLI_DEFUN(help,...))
 */
#define ECLI_DEFUN_ALIAS(name, cmdstr, helpstr, target) \
    static int _reg_alias_##name(void) { \
        return ec_node_or_add(__cli_root, \
            _cli_attr_callback(_cb_##target, NULL, \
                _H((helpstr), ec_node_str(EC_NO_ID, (cmdstr))))); \
    } \
    static struct ec_init _init_alias_##name = { \
        .init = _reg_alias_##name, .exit = NULL, .priority = 120 \
    }; \
    EC_INIT_REGISTER(_init_alias_##name)

/*
 * ECLI_DEFUN_GROUP - Define a command group (local to compilation unit)
 */
#define ECLI_DEFUN_GROUP(grp, keyword, helpstr) \
    static struct ec_node *__grp_##grp = NULL; \
    static int _grp_init_##grp(void) { \
        __grp_##grp = ec_node("or", EC_NO_ID); \
        ecli_register_context_group((keyword)); \
        return __grp_##grp ? 0 : -1; \
    } \
    static struct ec_init _grp_init_s_##grp = { \
        .init = _grp_init_##grp, .exit = NULL, .priority = 115 \
    }; \
    EC_INIT_REGISTER(_grp_init_s_##grp); \
    \
    static int _grp_add_##grp(void) { \
        struct ec_node *seq = EC_NODE_SEQ(EC_NO_ID, \
            ec_node_str(EC_NO_ID, (keyword)), \
            __grp_##grp); \
        if (!seq) return -1; \
        return ec_node_or_add(__cli_root, _H((helpstr), seq)); \
    } \
    static struct ec_init _grp_add_s_##grp = { \
        .init = _grp_add_##grp, .exit = NULL, .priority = 125 \
    }; \
    EC_INIT_REGISTER(_grp_add_s_##grp)

/*
 * ECLI_EXPORT_GROUP - Define a command group exported for use by applications
 *
 * Use this in the library to define groups that applications can extend.
 * The group node is non-static and declared extern in the header.
 */
#define ECLI_EXPORT_GROUP(grp, keyword, helpstr) \
    struct ec_node *__grp_##grp = NULL; \
    static int _grp_init_##grp(void) { \
        __grp_##grp = ec_node("or", EC_NO_ID); \
        ecli_register_context_group((keyword)); \
        return __grp_##grp ? 0 : -1; \
    } \
    static struct ec_init _grp_init_s_##grp = { \
        .init = _grp_init_##grp, .exit = NULL, .priority = 115 \
    }; \
    EC_INIT_REGISTER(_grp_init_s_##grp); \
    \
    static int _grp_add_##grp(void) { \
        struct ec_node *seq = EC_NODE_SEQ(EC_NO_ID, \
            ec_node_str(EC_NO_ID, (keyword)), \
            __grp_##grp); \
        if (!seq) return -1; \
        return ec_node_or_add(__cli_root, _H((helpstr), seq)); \
    } \
    static struct ec_init _grp_add_s_##grp = { \
        .init = _grp_add_##grp, .exit = NULL, .priority = 125 \
    }; \
    EC_INIT_REGISTER(_grp_add_s_##grp)

/*
 * ECLI_USE_GROUP - Declare an external group defined by the library
 *
 * Use this in applications to access groups exported by the library.
 * After this declaration, use ECLI_DEFUN_SUB to add subcommands.
 */
#define ECLI_USE_GROUP(grp) \
    extern struct ec_node *__grp_##grp

/*
 * ECLI_DEFUN_SUB0 - Define a simple subcommand without arguments
 *
 * Use this for subcommands that are just a keyword with no arguments.
 * The description will properly appear in tab completion.
 *
 * Example:
 *   ECLI_DEFUN_SUB0(show, version, "show_version", "version", "display version")
 *   {
 *       ecli_output(cli, "Version 1.0.0\n");
 *       return 0;
 *   }
 */
#define ECLI_DEFUN_SUB0(grp, name, yaml_cb, cmdstr, helpstr) \
    static int _cb_##grp##_##name(eecli_ctx_t *cli, const struct ec_pnode *parse); \
    static int _reg_##grp##_##name(void) { \
        ecli_yaml_register((yaml_cb), _cb_##grp##_##name); \
        return ec_node_or_add(__grp_##grp, \
            _cli_attr_callback(_cb_##grp##_##name, (yaml_cb), \
                _cli_make_sub_node((helpstr), (cmdstr)))); \
    } \
    static struct ec_init _init_##grp##_##name = { \
        .init = _reg_##grp##_##name, .exit = NULL, .priority = 120 \
    }; \
    EC_INIT_REGISTER(_init_##grp##_##name); \
    static int _cb_##grp##_##name( \
        eecli_ctx_t *cli __attribute__((unused)), \
        const struct ec_pnode *parse __attribute__((unused)))

/*
 * ECLI_DEFUN_SUB - Define a subcommand with optional arguments
 *
 * For commands with arguments, use ECLI_ARG_* macros.
 * For simple keywords without arguments, prefer ECLI_DEFUN_SUB0 for
 * better tab completion descriptions.
 */
#define ECLI_DEFUN_SUB(grp, name, yaml_cb, cmdstr, helpstr, args...) \
    static int _cb_##grp##_##name(eecli_ctx_t *cli, const struct ec_pnode *parse); \
    static int _reg_##grp##_##name(void) { \
        ecli_yaml_register((yaml_cb), _cb_##grp##_##name); \
        return ec_node_or_add(__grp_##grp, \
            _cli_attr_callback(_cb_##grp##_##name, (yaml_cb), \
                _H((helpstr), EC_NODE_CMD(EC_NO_ID, (cmdstr), ##args)))); \
    } \
    static struct ec_init _init_##grp##_##name = { \
        .init = _reg_##grp##_##name, .exit = NULL, .priority = 120 \
    }; \
    EC_INIT_REGISTER(_init_##grp##_##name); \
    static int _cb_##grp##_##name( \
        eecli_ctx_t *cli __attribute__((unused)), \
        const struct ec_pnode *parse __attribute__((unused)))

/*
 * ECLI_DEFUN_SUB_NODE - Define a subcommand with custom grammar node
 *
 * Use this when you need complex argument handling (optional arguments,
 * alternatives, etc.) that can't be expressed with EC_NODE_CMD.
 *
 * Parameters:
 *   grp       - Parent group name (from ECLI_DEFUN_GROUP)
 *   name      - Unique identifier for this command
 *   yaml_cb   - Callback name for YAML export
 *   helpstr   - Help text shown in command list
 *   node_expr - Expression that builds the ec_node grammar
 *
 * Example:
 *   ECLI_DEFUN_SUB_NODE(show, doc, "show_doc",
 *       "display or export command documentation",
 *       EC_NODE_SEQ(EC_NO_ID,
 *           ec_node_str(EC_NO_ID, "doc"),
 *           _H("command name", ec_node_re("cmd", "[a-z]+")),
 *           ec_node_option(EC_NO_ID,
 *               EC_NODE_SEQ(EC_NO_ID,
 *                   ec_node_str(EC_NO_ID, "file"),
 *                   _H("filename", ec_node_re("file", "[^ ]+"))))))
 *   {
 *       // handler implementation
 *   }
 */
#define ECLI_DEFUN_SUB_NODE(grp, name, yaml_cb, helpstr, node_expr) \
    static int _cb_##grp##_##name(eecli_ctx_t *cli, const struct ec_pnode *parse); \
    static int _reg_##grp##_##name(void) { \
        ecli_yaml_register((yaml_cb), _cb_##grp##_##name); \
        struct ec_node *_node = (node_expr); \
        if (!_node) return -1; \
        return ec_node_or_add(__grp_##grp, \
            _cli_attr_callback(_cb_##grp##_##name, (yaml_cb), \
                _H((helpstr), _node))); \
    } \
    static struct ec_init _init_##grp##_##name = { \
        .init = _reg_##grp##_##name, .exit = NULL, .priority = 120 \
    }; \
    EC_INIT_REGISTER(_init_##grp##_##name); \
    static int _cb_##grp##_##name( \
        eecli_ctx_t *cli __attribute__((unused)), \
        const struct ec_pnode *parse __attribute__((unused)))

#define ECLI_DEFUN_SET(grp, name, yaml_cb, cmdstr, helpstr, out_fmt, out_group, out_prio, args...) \
    static void _out_##grp##_##name(eecli_ctx_t *cli, FILE *fp, const char *fmt); \
    static int _cb_##grp##_##name(eecli_ctx_t *cli, const struct ec_pnode *parse); \
    static int _reg_##grp##_##name(void) { \
        ecli_yaml_register((yaml_cb), _cb_##grp##_##name); \
        ecli_out_register((yaml_cb), (out_group), (out_fmt), \
                         _out_##grp##_##name, (out_prio)); \
        return ec_node_or_add(__grp_##grp, \
            _cli_attr_callback(_cb_##grp##_##name, (yaml_cb), \
                _H((helpstr), EC_NODE_CMD(EC_NO_ID, (cmdstr), ##args)))); \
    } \
    static struct ec_init _init_##grp##_##name = { \
        .init = _reg_##grp##_##name, .exit = NULL, .priority = 120 \
    }; \
    EC_INIT_REGISTER(_init_##grp##_##name); \
    static int _cb_##grp##_##name( \
        eecli_ctx_t *cli __attribute__((unused)), \
        const struct ec_pnode *parse __attribute__((unused)))

#define ECLI_DEFUN_OUT(grp, name) \
    static void _out_##grp##_##name( \
        eecli_ctx_t *cli __attribute__((unused)), \
        FILE *fp __attribute__((unused)), \
        const char *fmt __attribute__((unused)))

#define ECLI_OUT_FMT(cli, fp, fmt, ...) \
    ecli_out_fmt((cli), (fp), (fmt), ##__VA_ARGS__)

#define ECLI_OUT(cli, fp, fmt, ...) do { \
    if (fp) fprintf(fp, fmt, ##__VA_ARGS__); \
    else ecli_output(cli, fmt, ##__VA_ARGS__); \
} while(0)

/*
 * ECLI_DOC - Add long description and examples to a command
 *
 * Documentation is stored in a dedicated ELF section (.ecli_doc) that can
 * be stripped for production builds to reduce binary size.
 *
 * Usage:
 *   ECLI_DOC(group, name, "Long description...", "example1\nexample2\n")
 *
 * The group and name must match a ECLI_DEFUN_SUB or ECLI_DEFUN_SET definition.
 * At runtime, "show doc <command>" displays the full documentation including
 * command syntax and arguments (extracted from the macro) plus the long
 * description and examples provided here.
 *
 * To strip documentation from release builds:
 *   objcopy --remove-section=.ecli_doc binary binary-stripped
 */

typedef struct ecli_doc_entry {
    const char *cmd_name;    /* Command name (yaml_cb from DEFUN) */
    const char *long_desc;   /* Long description text */
    const char *examples;    /* Example usage (newline separated) */
} __attribute__((aligned(32))) ecli_doc_entry_t;

/* Section markers (defined by linker)
 * Weak symbols allow the code to detect if documentation was stripped. */
extern const ecli_doc_entry_t __start_cli_doc[] __attribute__((weak));
extern const ecli_doc_entry_t __stop_cli_doc[] __attribute__((weak));

/*
 * ECLI_DOC - Add documentation for a command
 *
 * Parameters:
 *   yaml_cb      - Command name (must match the yaml_cb from ECLI_DEFUN*)
 *   long_desc    - Detailed description string
 *   examples     - Example usage string (newline separated)
 *
 * When ECLI_NO_DOC is defined, this macro becomes a no-op for builds
 * that don't need embedded documentation.
 */
#ifdef ECLI_NO_DOC
#define ECLI_DOC(yaml_cb, long_desc_str, examples_str) \
    /* Documentation disabled */
#else
#define ECLI_DOC(yaml_cb, long_desc_str, examples_str) \
    static const ecli_doc_entry_t _doc_##yaml_cb \
    __attribute__((section("ecli_doc"), used)) = { \
        .cmd_name = #yaml_cb, \
        .long_desc = long_desc_str, \
        .examples = examples_str \
    }
#endif

/* Lookup documentation by command name */
const ecli_doc_entry_t *ecli_doc_lookup(const char *cmd_name);

/* Documentation output formats */
typedef enum {
    ECLI_DOC_FMT_MD,    /* Markdown (default) */
    ECLI_DOC_FMT_RST,   /* reStructuredText */
    ECLI_DOC_FMT_TXT,   /* Plain text */
} ecli_doc_fmt_t;

/* Display documentation for a command */
void ecli_show_doc(eecli_ctx_t *cli, const char *cmd_name);

/* Display documentation to a file with format */
void ecli_show_doc_file(eecli_ctx_t *cli, const char *cmd_name,
                       const char *filename, ecli_doc_fmt_t fmt);

extern struct ec_node *ecli_cmd_get_commands(void);

static inline struct ec_node *ecli_cmd_get_root(void)
{
    extern struct ec_node *__cli_root;
    return __cli_root;
}

ecli_cmd_cb_t ecli_cmd_lookup_callback(const struct ec_pnode *parse);

const char *ecli_arg_str(const struct ec_pnode *parse, const char *id);

int ecli_arg_int(const struct ec_pnode *parse, const char *id, int def);

void ecli_out_register(const char *name, const char *group,
                      const char *default_fmt, ecli_out_t func, int priority);

void ecli_dump_running_config(eecli_ctx_t *cli, FILE *fp);

const char *ecli_out_get_fmt(const char *name, const char *default_fmt);

void ecli_out_fmt(eecli_ctx_t *cli, FILE *fp, const char *fmt, ...);
