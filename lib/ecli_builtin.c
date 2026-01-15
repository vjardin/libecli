/*
 * CLI Built-in Commands
 *
 * Copyright (C) 2026 Free Mobile, Vincent Jardin <vjardin@free.fr>
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file provides built-in CLI commands that are automatically
 * available in all applications using the CLI library.
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <ecoli.h>

#include "ecli.h"
#include "ecli_cmd.h"
#include "ecli_types.h"
#include "ecli_yaml.h"

/*
 * "help" - display available commands
 */
ECLI_DEFUN(help, "help", "help", "show available commands")
{
    ecli_output(cli, "Press TAB for command completion and contextual help.\n\n");
    ecli_show_help(cli);
    return 0;
}

/* "?" - alias for help */
ECLI_DEFUN_ALIAS(question, "?", "show available commands (alias for help)", help);

/*
 * "quit" - exit the application
 */
ECLI_DEFUN(quit, "quit", "quit", "exit the application")
{
    ecli_output(cli, "Goodbye!\n");
    ecli_request_exit();
    return 0;
}

/* "exit" - alias for quit */
ECLI_DEFUN_ALIAS(exit_cmd, "exit", "exit the application (alias for quit)", quit);

/*
 * 'show' command group - display information
 * Exported so applications can add their own show subcommands
 */
ECLI_EXPORT_GROUP(show, "show", "display information");

/*
 * "show running-config" - display running configuration
 */
ECLI_DEFUN_SUB0(show, running_config, "show_running_config",
                "running-config", "display running configuration")
{
    ecli_dump_running_config(cli, NULL);
    return 0;
}

/*
 * "show run" - short alias for show running-config
 */
ECLI_DEFUN_SUB0(show, run, "show_run", "run", "display running configuration")
{
    ecli_dump_running_config(cli, NULL);
    return 0;
}

/*
 * "show version" - display library version and copyright
 */
ECLI_DEFUN_SUB0(show, version, "show_version", "version", "display version information")
{
    ecli_output(cli, "libecli version %s\n", ECLI_VERSION);
    ecli_output(cli, "Copyright (C) 2026 Free Mobile, Vincent Jardin\n");
    ecli_output(cli, "SPDX-License-Identifier: AGPL-3.0-or-later\n");
    return 0;
}

/*
 * "show doc" - display or export command documentation
 *
 * Syntax:
 *   show doc <cmd_name>                              - display to terminal
 *   show doc <cmd_name> file <filename>              - export to file (markdown)
 *   show doc <cmd_name> file <filename> format <fmt> - export with format
 *
 * Uses ECLI_DEFUN_SUB_NODE with optional arguments to avoid duplicate
 * "doc" entries in tab completion.
 */
#define ID_CMD_NAME    "cmd_name"
#define ID_DOC_FILE    "doc_filename"
#define ID_DOC_FMT     "doc_format"

ECLI_DEFUN_SUB_NODE(show, doc, "show_doc",
    "display or export command documentation",
    EC_NODE_SEQ(EC_NO_ID,
        _cli_sub_keyword("doc", "display or export command documentation"),
        _H("command name", ec_node_re(ID_CMD_NAME, "[a-zA-Z_][a-zA-Z0-9_]*")),
        ec_node_option(EC_NO_ID,
            EC_NODE_SEQ(EC_NO_ID,
                ec_node_str(EC_NO_ID, "file"),
                _H("output filename", ec_node_re(ID_DOC_FILE, "[^ ]+")),
                ec_node_option(EC_NO_ID,
                    EC_NODE_SEQ(EC_NO_ID,
                        ec_node_str(EC_NO_ID, "format"),
                        _H("format (md, rst, txt)",
                           ec_node_re(ID_DOC_FMT, "(md|rst|txt)"))))))))
{
    const char *cmd_name = ecli_arg_str(parse, ID_CMD_NAME);
    const char *filename = ecli_arg_str(parse, ID_DOC_FILE);
    const char *format = ecli_arg_str(parse, ID_DOC_FMT);

    if (!cmd_name) {
        ecli_output(cli, "Usage: show doc <command_name> [file <filename> [format <fmt>]]\n");
        ecli_output(cli, "Formats: md (markdown), rst (reStructuredText), txt (plain text)\n");
        return 0;
    }

    if (filename) {
        /* Export to file */
        ecli_doc_fmt_t fmt = ECLI_DOC_FMT_MD;
        if (format) {
            if (strcmp(format, "rst") == 0)
                fmt = ECLI_DOC_FMT_RST;
            else if (strcmp(format, "txt") == 0)
                fmt = ECLI_DOC_FMT_TXT;
        }
        ecli_show_doc_file(cli, cmd_name, filename, fmt);
    } else {
        /* Display to terminal */
        ecli_show_doc(cli, cmd_name);
    }

    return 0;
}

/*
 * 'write' command group - save/display configuration
 */
ECLI_DEFUN_GROUP(write, "write", "save configuration");

#define ID_FILENAME "filename"

ECLI_DEFUN_SUB0(write, terminal, "write_terminal", "terminal", "display config to terminal")
{
    ecli_dump_running_config(cli, NULL);
    return 0;
}

ECLI_DEFUN_SUB(write, file, "write_file", "file filename", "save config to file",
    _H("output filename", ec_node_re(ID_FILENAME, "[^ ]+")))
{
    const char *filename = ecli_arg_str(parse, ID_FILENAME);

    if (!filename) {
        ecli_output(cli, "Usage: write file <filename>\n");
        return 0;
    }

    FILE *fp = fopen(filename, "w");
    if (!fp) {
        ecli_output(cli, "Cannot open file: %s: %s\n", filename, strerror(errno));
        return 0;
    }

    ecli_dump_running_config(cli, fp);
    fclose(fp);

    ecli_output(cli, "Configuration saved to %s\n", filename);
    return 0;
}

ECLI_DEFUN_SUB(write, yaml, "write_yaml", "yaml filename", "export CLI grammar to YAML",
    _H("output filename", ec_node_re(ID_FILENAME, "[^ ]+")))
{
    const char *filename = ecli_arg_str(parse, ID_FILENAME);

    if (!filename) {
        ecli_output(cli, "Usage: write yaml <filename>\n");
        return 0;
    }

    ecli_yaml_export(cli, filename);
    return 0;
}
