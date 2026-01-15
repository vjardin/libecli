/*
 * CLI YAML Grammar Import/Export
 *
 * Copyright (C) 2026 Free Mobile, Vincent Jardin <vjardin@free.fr>
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Provides YAML-based CLI grammar import/export for localization support.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <limits.h>
#include <sys/queue.h>
#include "queue-extension.h"

#include <yaml.h>
#include <ecoli.h>
#include <ecoli/yaml.h>

#include "ecli_yaml.h"
#include "ecli.h"
#include "ecli_cmd.h"

/* Attribute key for callback name in YAML */
#define ECLI_YAML_CB_ATTR "callback"

/* Callback registry entry */
struct cb_entry {
    SLIST_ENTRY(cb_entry) next;
    const char *name;
    ecli_yaml_cb_t callback;
};

/* Output format override entry */
struct output_fmt_entry {
    SLIST_ENTRY(output_fmt_entry) next;
    char *callback_name;
    char *fmt;
};

/* Callback registry */
static SLIST_HEAD(, cb_entry) cb_registry = SLIST_HEAD_INITIALIZER(cb_registry);
static size_t cb_count = 0;

/* Output format override registry */
static SLIST_HEAD(, output_fmt_entry) output_fmt_registry = SLIST_HEAD_INITIALIZER(output_fmt_registry);

static bool initialized = false;

int ecli_yaml_init(void)
{
    if (initialized)
        return 0;

    SLIST_INIT(&cb_registry);
    cb_count = 0;
    SLIST_INIT(&output_fmt_registry);
    initialized = true;

    return 0;
}

void ecli_yaml_cleanup(void)
{
    /* Free callback entries */
    struct cb_entry *cb, *cb_tmp;
    SLIST_FOREACH_SAFE(cb, &cb_registry, next, cb_tmp) {
        SLIST_REMOVE(&cb_registry, cb, cb_entry, next);
        free(cb);
    }
    cb_count = 0;

    /* Free output format entries */
    struct output_fmt_entry *fmt, *fmt_tmp;
    SLIST_FOREACH_SAFE(fmt, &output_fmt_registry, next, fmt_tmp) {
        SLIST_REMOVE(&output_fmt_registry, fmt, output_fmt_entry, next);
        free(fmt->callback_name);
        free(fmt->fmt);
        free(fmt);
    }

    initialized = false;
}

int ecli_yaml_register(const char *name, ecli_yaml_cb_t callback)
{
    if (!initialized) {
        if (ecli_yaml_init() < 0)
            return -1;
    }

    if (name == NULL || callback == NULL) {
        errno = EINVAL;
        return -1;
    }

    /* Check for duplicate */
    struct cb_entry *entry;
    SLIST_FOREACH(entry, &cb_registry, next) {
        if (strcmp(entry->name, name) == 0) {
            entry->callback = callback;
            return 0;
        }
    }

    /* Allocate new entry */
    entry = malloc(sizeof(*entry));
    if (!entry) {
        errno = ENOMEM;
        return -1;
    }

    entry->name = name;
    entry->callback = callback;
    SLIST_INSERT_HEAD(&cb_registry, entry, next);
    cb_count++;

    return 0;
}

static ecli_yaml_cb_t lookup_callback(const char *name)
{
    if (name == NULL)
        return NULL;

    struct cb_entry *entry;
    SLIST_FOREACH(entry, &cb_registry, next) {
        if (strcmp(entry->name, name) == 0)
            return entry->callback;
    }

    return NULL;
}

const char *ecli_yaml_get_callback_name(const struct ec_pnode *parse)
{
    const struct ec_pnode *p;

    if (parse == NULL)
        return NULL;

    /* Walk the parse tree looking for callback attribute */
    EC_PNODE_FOREACH(p, parse) {
        const struct ec_node *node = ec_pnode_get_node(p);
        struct ec_dict *attrs = ec_node_attrs(node);

        if (attrs != NULL) {
            const char *cb_name = ec_dict_get(attrs, ECLI_YAML_CB_ATTR);
            if (cb_name != NULL)
                return cb_name;
        }
    }

    return NULL;
}

int ecli_yaml_dispatch(eecli_ctx_t *cli, const struct ec_pnode *parse)
{
    const char *cb_name;
    ecli_yaml_cb_t callback;

    cb_name = ecli_yaml_get_callback_name(parse);
    if (cb_name == NULL) {
        ecli_err(cli, "No callback attribute found in parse tree\n");
        return -1;
    }

    callback = lookup_callback(cb_name);
    if (callback == NULL) {
        ecli_err(cli, "No handler registered for callback: %s\n", cb_name);
        return -1;
    }

    return callback(cli, parse);
}

/*
 * Register an output format override
 */
static int ecli_yaml_register_output_fmt(const char *callback_name, const char *fmt)
{
    if (!initialized) {
        if (ecli_yaml_init() < 0)
            return -1;
    }

    if (callback_name == NULL || fmt == NULL) {
        errno = EINVAL;
        return -1;
    }

    /* Check for existing entry and update */
    struct output_fmt_entry *entry;
    SLIST_FOREACH(entry, &output_fmt_registry, next) {
        if (strcmp(entry->callback_name, callback_name) == 0) {
            free(entry->fmt);
            entry->fmt = strdup(fmt);
            return entry->fmt ? 0 : -1;
        }
    }

    /* Allocate new entry */
    entry = malloc(sizeof(*entry));
    if (!entry) {
        errno = ENOMEM;
        return -1;
    }

    entry->callback_name = strdup(callback_name);
    entry->fmt = strdup(fmt);

    if (!entry->callback_name || !entry->fmt) {
        free(entry->callback_name);
        free(entry->fmt);
        free(entry);
        return -1;
    }

    SLIST_INSERT_HEAD(&output_fmt_registry, entry, next);

    return 0;
}

/*
 * Parse output_formats section from YAML file
 *
 * Expected format:
 *   output_formats:
 *     switch_add: "switch add {name} ports {ports}\n"
 *     show_switch: "afficher switch {name} avec {ports} ports\n"
 */
static int parse_output_formats(const char *filename)
{
    FILE *fp;
    yaml_parser_t parser;
    yaml_event_t event;
    int in_output_formats = 0;
    int expect_value = 0;
    char *current_key = NULL;
    int count = 0;

    fp = fopen(filename, "r");
    if (!fp)
        return 0;  /* Not an error - output_formats is optional */

    if (!yaml_parser_initialize(&parser)) {
        fclose(fp);
        return -1;
    }

    yaml_parser_set_input_file(&parser, fp);

    while (1) {
        if (!yaml_parser_parse(&parser, &event))
            break;

        if (event.type == YAML_STREAM_END_EVENT) {
            yaml_event_delete(&event);
            break;
        }

        if (event.type == YAML_SCALAR_EVENT) {
            const char *value = (const char *)event.data.scalar.value;

            if (!in_output_formats) {
                /* Look for output_formats key */
                if (strcmp(value, "output_formats") == 0) {
                    in_output_formats = 1;
                }
            } else if (expect_value && current_key) {
                /* This is the format string value */
                if (ecli_yaml_register_output_fmt(current_key, value) == 0) {
                    count++;
                }
                free(current_key);
                current_key = NULL;
                expect_value = 0;
            } else {
                /* This is a callback name key */
                free(current_key);
                current_key = strdup(value);
                expect_value = 1;
            }
        } else if (event.type == YAML_MAPPING_END_EVENT && in_output_formats) {
            /* End of output_formats section */
            in_output_formats = 0;
        }

        yaml_event_delete(&event);
    }

    free(current_key);
    yaml_parser_delete(&parser);
    fclose(fp);

    return 0;
}

int ecli_yaml_load_formats(const char *filename)
{
    if (filename == NULL) {
        errno = EINVAL;
        return -1;
    }

    return parse_output_formats(filename);
}

struct ec_node *ecli_yaml_load(const char *filename)
{
    struct ec_node *grammar;
    struct ec_node *shlex;
    char formats_file[PATH_MAX];

    if (filename == NULL) {
        errno = EINVAL;
        return NULL;
    }

    grammar = ec_yaml_import(filename);
    if (grammar == NULL) {
        return NULL;
    }

    /*
     * Look for companion output formats file.
     * If grammar is "foo.yaml", look for "foo_formats.yaml"
     */
    const char *ext = strrchr(filename, '.');
    if (ext) {
        size_t base_len = ext - filename;
        snprintf(formats_file, sizeof(formats_file), "%.*s_formats%s",
                 (int)base_len, filename, ext);
        if (access(formats_file, R_OK) == 0) {
            parse_output_formats(formats_file);
        }
    }

    /* Wrap with sh_lex for shell-like tokenization */
    shlex = ec_node_sh_lex(EC_NO_ID, grammar);
    if (shlex == NULL) {
        ec_node_free(grammar);
        return NULL;
    }

    return shlex;
}

const char *ecli_yaml_get_output_fmt(const char *callback_name)
{
    if (callback_name == NULL)
        return NULL;

    struct output_fmt_entry *entry;
    SLIST_FOREACH(entry, &output_fmt_registry, next) {
        if (strcmp(entry->callback_name, callback_name) == 0)
            return entry->fmt;
    }

    return NULL;
}

/*
 * Print YAML header with instructions
 */
static void print_yaml_header(FILE *fp, const char *app_name)
{
    fprintf(fp,
"# %s CLI Grammar Template\n"
"#\n"
"# This file defines the CLI grammar in YAML format for libecoli.\n"
"# You can customize this file to create an alternate CLI interface.\n"
"#\n"
"# USAGE:\n"
"#   1. Export this template:  write yaml grammar.yaml\n"
"#   2. Edit the file to customize command names and help strings\n"
"#   3. Set environment: ECLI_GRAMMAR=translated.yaml\n"
"#   4. Restart application - it will use the translated grammar\n"
"#\n"
"# TRANSLATION EXAMPLE:\n"
"#   To translate the CLI to French:\n"
"#     - Change 'string: help' to 'string: aide'\n"
"#     - Change 'string: quit' to 'string: quitter'\n"
"#     - Change 'string: show' to 'string: afficher'\n"
"#     - Translate all 'help:' strings to French\n"
"#\n"
"# IMPORTANT:\n"
"#   - Keep all 'attrs: callback:' values unchanged (they link to C code)\n"
"#   - Keep 'id:' values unchanged (they are used for argument extraction)\n"
"#   - Only modify 'string:', 'help:', and 'pattern:' values\n"
"#\n"
"# OUTPUT FORMATS:\n"
"#   Create a companion file 'grammar_formats.yaml' with:\n"
"#     output_formats:\n"
"#       switch_add: \"switch add {name} ports {ports}\\n\"\n"
"#   These override the default output for 'write terminal'.\n"
"#\n"
"# =============================================================================\n"
"\n", app_name ? app_name : "CLI");
}

int ecli_yaml_export_fp(eecli_ctx_t *cli, FILE *fp)
{
    struct ec_node *root;

    if (fp == NULL)
        return -1;

    /* Get the raw grammar root (before sh_lex wrapping) */
    root = ecli_cmd_get_root();
    if (root == NULL) {
        if (cli)
            ecli_output(cli, "Error: No CLI grammar available\n");
        return -1;
    }

    /* Print header with instructions */
    print_yaml_header(fp, "VDSA");

    /* Export using libecoli's YAML export */
    if (ec_yaml_export(fp, root) < 0) {
        if (cli)
            ecli_output(cli, "Error: Failed to export grammar\n");
        return -1;
    }

    return 0;
}

int ecli_yaml_export(eecli_ctx_t *cli, const char *filename)
{
    FILE *fp;
    int ret;

    if (filename == NULL) {
        if (cli)
            ecli_output(cli, "Error: No filename specified\n");
        return -1;
    }

    fp = fopen(filename, "w");
    if (!fp) {
        if (cli)
            ecli_output(cli, "Error: Cannot open file: %s: %s\n",
                      filename, strerror(errno));
        return -1;
    }

    ret = ecli_yaml_export_fp(cli, fp);
    fclose(fp);

    if (ret == 0 && cli)
        ecli_output(cli, "CLI grammar exported to %s\n", filename);

    return ret;
}
