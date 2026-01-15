/*
 * CLI Infrastructure Library
 *
 * Copyright (C) 2026 Free Mobile, Vincent Jardin <vjardin@free.fr>
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This library provides CLI infrastructure for interactive and TCP-based
 * command-line interfaces using libecoli for parsing and completion.
 *
 * QUICK REFERENCE
 *
 * INITIALIZATION:
 *   ecli_init(config)                    - Start CLI in foreground/interactive mode
 *   ecli_init_tcp(config, base, port)    - Start CLI in TCP daemon mode
 *   ecli_shutdown()                      - Clean up and shutdown CLI
 *
 * MAIN LOOP:
 *   ecli_run(running)                    - Run CLI event loop until *running=false
 *   ecli_request_exit()                  - Request CLI to stop (sets running=false)
 *
 * OUTPUT:
 *   ecli_output(cli, fmt, ...)           - Printf-style output to CLI client
 *   ecli_show_help(cli)                  - Display available commands
 *
 * CONFIG:
 *   ecli_load_config(filename)           - Load and replay config file at startup
 *
 * QUERY:
 *   ecli_get_mode()                      - Get current mode (STDIN or TCP)
 *   ecli_uses_editline()                 - Check if editline is available
 *
 * CONTEXT:
 *   ecli_register_context_group(keyword) - Register group for context mode
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Library version */
#define ECLI_VERSION "1.0.0"

/* Forward declarations */
struct event_base;

/*
 * eecli_ctx_t - CLI context (opaque)
 *
 * Represents a CLI session. In foreground mode, there's one global context.
 * In TCP mode, each connected client has its own context.
 */
typedef struct eecli_ctx eecli_ctx_t;

/*
 * ecli_mode_t - CLI operational mode
 */
typedef enum {
    ECLI_MODE_STDIN,  /* Interactive foreground mode (stdin/stdout) */
    ECLI_MODE_TCP,    /* TCP daemon mode (libevent-based server) */
} ecli_mode_t;

/*
 * ecli_config_t - CLI configuration
 *
 * Pass to ecli_init() or ecli_init_tcp() to customize CLI behavior.
 */
typedef struct ecli_config {
    const char *prompt;       /* Command prompt (default: "cli> ") */
    const char *banner;       /* Welcome message (default: NULL) */
    const char *version;      /* Version string (default: "1.0.0") */
    const char *grammar_env;  /* Env var for YAML grammar (default: "ECLI_GRAMMAR") */
    bool use_yaml;            /* Try YAML grammar first (default: false) */
} ecli_config_t;

/*
 * ECLI_CONFIG_DEFAULT - Default configuration initializer
 */
#define ECLI_CONFIG_DEFAULT { \
    .prompt = "cli> ", \
    .banner = NULL, \
    .version = "1.0.0", \
    .grammar_env = "ECLI_GRAMMAR", \
    .use_yaml = false \
}

/*
 * ecli_init - Initialize CLI in foreground (interactive) mode
 *
 * Returns: 0 on success, -1 on error
 */
int ecli_init(const ecli_config_t *config);

/*
 * ecli_init_tcp - Initialize CLI in TCP daemon mode
 *
 * Returns: 0 on success, -1 on error
 */
int ecli_init_tcp(const ecli_config_t *config, struct event_base *event_base, uint16_t port);

/*
 * ecli_shutdown - Shutdown CLI subsystem
 */
void ecli_shutdown(void);

/*
 * ecli_run - Run CLI main loop
 *
 * Returns: 0 on normal exit, -1 on error
 */
int ecli_run(volatile bool *running);

/*
 * ecli_request_exit - Request CLI to stop
 */
void ecli_request_exit(void);

/*
 * ecli_uses_editline - Check if editline is available
 */
bool ecli_uses_editline(void);

/*
 * ecli_get_mode - Get current CLI mode
 */
ecli_mode_t ecli_get_mode(void);

/*
 * ecli_output - Output text to CLI client
 */
void ecli_output(eecli_ctx_t *cli, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

/*
 * ecli_err - Output error message to CLI client
 *
 * Same as ecli_output but prefixed with "Error: ".
 * Use for user-facing error messages.
 */
void ecli_err(eecli_ctx_t *cli, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

/*
 * ecli_show_help - Display available commands
 */
void ecli_show_help(eecli_ctx_t *cli);

/*
 * ecli_register_context_group - Register a keyword as a context group
 */
void ecli_register_context_group(const char *keyword);

/*
 * ecli_load_config - Load and replay configuration from a file
 *
 * Reads a configuration file line by line and executes each line as
 * a CLI command. Used to restore configuration at startup.
 *
 * Returns:
 *   >= 0 - Number of commands that failed (0 = all succeeded)
 *   -1   - File could not be opened
 */
int ecli_load_config(const char *filename);

/*
 * ecli_editline_cmd_wrapper - Wrapper callback for libecoli editline
 *
 * This function bridges libecoli's editline callback mechanism with
 * libecli's command handlers. It is automatically registered on command
 * nodes and should not be called directly.
 */
struct ec_pnode;
int ecli_editline_cmd_wrapper(const struct ec_pnode *parse);
