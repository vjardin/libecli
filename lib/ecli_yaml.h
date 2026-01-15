/*
 * CLI YAML Grammar Import/Export
 *
 * Copyright (C) 2026 Free Mobile, Vincent Jardin <vjardin@free.fr>
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This module provides YAML-based CLI grammar import/export for runtime
 * CLI translation and customization without recompiling.
 *
 * QUICK REFERENCE
 *
 * INITIALIZATION:
 *   ecli_yaml_init()                     - Initialize YAML subsystem
 *   ecli_yaml_cleanup()                  - Clean up YAML resources
 *
 * CALLBACK REGISTRATION:
 *   ecli_yaml_register(name, callback)   - Register callback by name
 *   ecli_yaml_dispatch(cli, parse)       - Execute callback for parsed command
 *   ecli_yaml_get_callback_name(parse)   - Get callback name from parse tree
 *
 * GRAMMAR IMPORT:
 *   ecli_yaml_load(filename)             - Load grammar from YAML file
 *   ecli_yaml_load_formats(filename)     - Load output format overrides
 *
 * GRAMMAR EXPORT:
 *   ecli_yaml_export(cli, filename)      - Export grammar to YAML file
 *   ecli_yaml_export_fp(cli, fp)         - Export grammar to FILE stream
 *
 * FORMAT LOOKUP:
 *   ecli_yaml_get_output_fmt(name)       - Get overridden output format
 *
 * TRANSLATION WORKFLOW
 *
 * The YAML grammar system enables CLI translation without recompiling:
 *
 * Step 1: Export the current grammar as a translation template
 * ------------------------------------------------------------
 *   app> write yaml grammar.yaml
 *   CLI grammar exported to grammar.yaml
 *
 *   This creates a YAML file with all commands, help strings, and
 *   output formats that can be translated.
 *
 * Step 2: Translate the YAML file
 * --------------------------------
 *   Edit grammar.yaml to translate:
 *   - Command syntax (string: values)
 *   - Help text (help: values)
 *   - Output formats (in companion _formats.yaml file)
 *
 *   IMPORTANT: Keep "callback:" values unchanged - they link to C code.
 *
 * Step 3: Create output format overrides (optional)
 * --------------------------------------------------
 *   Create grammar_formats.yaml:
 *
 *   output_formats:
 *     vhost_add: "vhost ajouter {hostname} racine {docroot} port {port}\n"
 *     show_vhosts: "Vhost: {hostname}:{port} -> {docroot}\n"
 *
 * Step 4: Load the translated grammar
 * ------------------------------------
 *   Set environment variable and restart:
 *
 *   $ ECLI_GRAMMAR=grammar_french.yaml ./myapp
 *
 *   Or load programmatically:
 *
 *   struct ec_node *grammar = ecli_yaml_load("grammar_french.yaml");
 *   ecli_yaml_load_formats("grammar_french_formats.yaml");
 *
 * YAML GRAMMAR FORMAT
 *
 * The exported YAML follows libecoli's grammar structure:
 *
 *   type: or                           # Root node type
 *   children:
 *     - type: cmd                      # Command node
 *       attrs:
 *         help: "exit the application" # Help text (translate this)
 *         callback: "quit"             # C callback name (DO NOT change)
 *       expr: "quit"                   # Command syntax (translate this)
 *
 *     - type: seq                      # Sequence node (for groups)
 *       children:
 *         - type: str
 *           string: "show"             # Group keyword (translate this)
 *         - type: or
 *           children:
 *             - type: cmd
 *               attrs:
 *                 help: "display status"
 *                 callback: "show_status"
 *               expr: "status"
 *
 * Node types:
 *   or     - Alternative (matches any child)
 *   seq    - Sequence (matches children in order)
 *   cmd    - Command with expression
 *   str    - Literal string
 *   int    - Integer with min/max/base
 *   re     - Regular expression pattern
 *
 * TRANSLATION EXAMPLES
 *
 * English to French translation:
 *
 * Original (English):
 *   - type: cmd
 *     attrs:
 *       help: "display all virtual hosts"
 *       callback: "show_vhosts"
 *     expr: "show vhosts"
 *
 * Translated (French):
 *   - type: cmd
 *     attrs:
 *       help: "afficher tous les hotes virtuels"
 *       callback: "show_vhosts"           # Keep unchanged!
 *     expr: "afficher vhosts"
 *
 * With format override (grammar_formats.yaml):
 *   output_formats:
 *     vhost_add: "vhost ajouter {hostname} racine {docroot} port {port}\n"
 *
 * CALLBACK NAMING CONVENTIONS
 *
 * Callback names create the link between YAML grammar and C code:
 *
 *   YAML:  callback: "show_status"
 *   C:     ECLI_DEFUN_SUB(show, status, "show_status", ...)
 *                                        ^^^^^^^^^^^^
 *                                        Must match exactly
 *
 * Best practices:
 *   - Use snake_case: "show_status", "vhost_add", "set_hostname"
 *   - Include group prefix: "show_version" not just "version"
 *   - Be consistent across the application
 *   - Document callback names in your code
 *
 * USAGE EXAMPLE - EXPORTING GRAMMAR
 *
 *   // Built-in command (already provided in ecli_builtin.c)
 *   ECLI_DEFUN_SUB(write_cmd, yaml, "write_yaml",
 *       "yaml filename",
 *       "export CLI grammar to YAML",
 *       ECLI_ARG_FILENAME("filename", "output YAML file"))
 *   {
 *       const char *filename = ec_pnode_get_str(parse, "filename");
 *       if (ecli_yaml_export(cli, filename) < 0) {
 *           ecli_output(cli, "Error: failed to export grammar\n");
 *           return -1;
 *       }
 *       ecli_output(cli, "CLI grammar exported to %s\n", filename);
 *       return 0;
 *   }
 *
 * USAGE EXAMPLE - LOADING TRANSLATED GRAMMAR
 *
 *   #include "ecli_yaml.h"
 *
 *   int main(int argc, char *argv[])
 *   {
 *       ec_init();
 *       ecli_yaml_init();
 *
 *       // Check for translated grammar
 *       const char *grammar_file = getenv("ECLI_GRAMMAR");
 *       struct ec_node *grammar = NULL;
 *
 *       if (grammar_file) {
 *           // Load translated grammar from YAML
 *           grammar = ecli_yaml_load(grammar_file);
 *           if (grammar) {
 *               printf("Loaded grammar from %s\n", grammar_file);
 *
 *               // Also load format overrides if available
 *               char formats_file[256];
 *               snprintf(formats_file, sizeof(formats_file),
 *                        "%s_formats.yaml", grammar_file);
 *               ecli_yaml_load_formats(formats_file);
 *           }
 *       }
 *
 *       if (!grammar) {
 *           // Fall back to compiled grammar
 *           grammar = ecli_cmd_get_commands();
 *       }
 *
 *       // Continue with CLI initialization...
 *       ecli_init(NULL);
 *       ecli_run(&g_running);
 *       ecli_yaml_cleanup();
 *       return 0;
 *   }
 *
 * USAGE EXAMPLE - OUTPUT FORMAT OVERRIDES
 *
 * Output format overrides allow translating "write terminal" output:
 *
 *   // C code defines default format:
 *   ECLI_DEFUN_SET(vhost_cmd, add, "vhost_add",
 *       "add hostname port docroot",
 *       "add a virtual host",
 *       "vhost add {hostname} port {port} root {docroot}\n",
 *       "vhosts", 100,
 *       ECLI_ARG_NAME("hostname", "server hostname"),
 *       ECLI_ARG_PORT("port", "listen port"),
 *       ECLI_ARG_PATH("docroot", "document root"))
 *   { ... }
 *
 *   // YAML override (grammar_formats.yaml):
 *   output_formats:
 *     vhost_add: "vhost ajouter {hostname} racine {docroot} port {port}\n"
 *
 *   // In the output function, use ecli_out_get_fmt() to get override:
 *   ECLI_DEFUN_OUT(vhost_cmd, add)
 *   {
 *       // fmt parameter already contains override if available
 *       ECLI_OUT_FMT(cli, fp, fmt,
 *           "hostname", FMT_STR, vhost_hostname,
 *           "port",     FMT_INT, vhost_port,
 *           "docroot",  FMT_STR, vhost_docroot,
 *           NULL);
 *   }
 *
 * Note: Named placeholders {hostname}, {port}, {docroot} allow translations
 * to reorder parameters while keeping the same ECLI_OUT_FMT call.
 *
 * ERROR HANDLING
 *
 * Functions return:
 *   0 / non-NULL  - Success
 *   -1 / NULL     - Error
 *
 * Common errors:
 *   - File not found
 *   - Invalid YAML syntax
 *   - Unknown callback name (warning, continues)
 *   - Missing required node attributes
 *
 * Example:
 *
 *   struct ec_node *grammar = ecli_yaml_load("grammar.yaml");
 *   if (!grammar) {
 *       fprintf(stderr, "Failed to load YAML grammar\n");
 *       // Fall back to compiled grammar
 *       grammar = ecli_cmd_get_commands();
 *   }
 *
 * INTEGRATION WITH ECLI_CMD.H
 *
 * The YAML system integrates with ECLI_DEFUN macros automatically:
 *
 * 1. ECLI_DEFUN/ECLI_DEFUN_SUB/ECLI_DEFUN_SET register callbacks:
 *    - Calls ecli_yaml_register(yaml_cb, callback_func) internally
 *    - Creates the name -> function pointer mapping
 *
 * 2. Grammar nodes include callback name:
 *    - attrs.callback = "callback_name" in YAML
 *    - Used for dispatch and format lookups
 *
 * 3. Output functions use format overrides:
 *    - ecli_out_get_fmt() checks for YAML override
 *    - Falls back to default format from ECLI_DEFUN_SET
 *

 */
#pragma once

#include <stdio.h>
#include <ecoli.h>

typedef struct eecli_ctx eecli_ctx_t;

typedef int (*ecli_yaml_cb_t)(eecli_ctx_t *cli, const struct ec_pnode *parse);

int ecli_yaml_init(void);

void ecli_yaml_cleanup(void);

int ecli_yaml_register(const char *name, ecli_yaml_cb_t callback);

int ecli_yaml_dispatch(eecli_ctx_t *cli, const struct ec_pnode *parse);

const char *ecli_yaml_get_callback_name(const struct ec_pnode *parse);

struct ec_node *ecli_yaml_load(const char *filename);

int ecli_yaml_load_formats(const char *filename);

int ecli_yaml_export(eecli_ctx_t *cli, const char *filename);

int ecli_yaml_export_fp(eecli_ctx_t *cli, FILE *fp);

const char *ecli_yaml_get_output_fmt(const char *name);
