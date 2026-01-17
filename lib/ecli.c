/*
 * CLI Infrastructure Library
 *
 * Copyright (C) 2026 Free Mobile, Vincent Jardin <vjardin@free.fr>
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/queue.h>
#include "queue-extension.h"

#include <event2/event.h>
#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>

#include <ecoli.h>

#include "ecli.h"
#include "ecli_cmd.h"
#include "ecli_yaml.h"

/* Context stack entry */
typedef struct context_entry {
    TAILQ_ENTRY(context_entry) next;
    char *name;
} context_entry_t;

/* CLI context structure */
struct eecli_ctx {
    ecli_mode_t            mode;
    ecli_config_t          config;
    struct event_base    *event_base;
    struct evconnlistener *listener;
    struct bufferevent   *client_bev;
    struct ec_editline   *editline;
    struct ec_node       *grammar;
    uint16_t              tcp_port;
    bool                  has_client;
    bool                  use_editline;
    bool                  use_yaml;
    /* Connected client address (for TCP mode) */
    struct sockaddr_storage client_addr;
    socklen_t             client_addrlen;
    /* Context mode support */
    TAILQ_HEAD(context_stack_head, context_entry) context_stack;
    int                   context_depth;
    char                  current_prompt[256];
};

/* Global CLI context */
static eecli_ctx_t *g_ecli_ctx = NULL;

/* Running flag pointer */
static volatile bool *g_running = NULL;

/* Context group registry - keywords that can be entered as contexts */
typedef struct context_group {
    SLIST_ENTRY(context_group) next;
    const char *keyword;
} context_group_t;

static SLIST_HEAD(, context_group) g_context_groups = SLIST_HEAD_INITIALIZER(g_context_groups);

/*
 * Register a keyword as a context group
 */
void ecli_register_context_group(const char *keyword)
{
    context_group_t *grp = malloc(sizeof(*grp));
    if (!grp) {
        fprintf(stderr, " Failed to allocate context group\n");
        return;
    }
    grp->keyword = keyword;
    SLIST_INSERT_HEAD(&g_context_groups, grp, next);
}

/*
 * Request CLI to exit
 */
void ecli_request_exit(void)
{
    if (g_running)
        *g_running = false;
}

/*
 * Check if a keyword is a registered context group
 */
static bool is_context_group(const char *keyword)
{
    context_group_t *grp;
    SLIST_FOREACH(grp, &g_context_groups, next) {
        if (strcmp(grp->keyword, keyword) == 0)
            return true;
    }
    return false;
}

static void ecli_write(eecli_ctx_t *cli, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

static void ecli_write(eecli_ctx_t *cli, const char *fmt, ...)
{
    va_list args;
    char buf[1024];

    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (cli->mode == ECLI_MODE_STDIN) {
        fputs(buf, stdout);
        fflush(stdout);
    } else if (cli->client_bev) {
        bufferevent_write(cli->client_bev, buf, strlen(buf));
    }
}

static void ecli_update_prompt(eecli_ctx_t *cli)
{
    if (cli->context_depth == 0) {
        snprintf(cli->current_prompt, sizeof(cli->current_prompt),
                 "%s", cli->config.prompt);
    } else {
        /* Build prompt with context path */
        char context_path[128] = "";
        context_entry_t *entry;
        int first = 1;
        TAILQ_FOREACH(entry, &cli->context_stack, next) {
            if (!first)
                strncat(context_path, "-", sizeof(context_path) - strlen(context_path) - 1);
            strncat(context_path, entry->name,
                    sizeof(context_path) - strlen(context_path) - 1);
            first = 0;
        }
        /* Remove trailing "> " or "# " from base prompt and add context */
        char base[64];
        strncpy(base, cli->config.prompt, sizeof(base) - 1);
        base[sizeof(base) - 1] = '\0';
        size_t len = strlen(base);
        if (len >= 2 && (base[len-2] == '>' || base[len-2] == '#')) {
            base[len-2] = '\0';
        } else if (len >= 1 && (base[len-1] == '>' || base[len-1] == '#')) {
            base[len-1] = '\0';
        }
        snprintf(cli->current_prompt, sizeof(cli->current_prompt),
                 "%.*s(%.128s)> ", (int)(sizeof(base) - 5), base, context_path);
    }
}

static void ecli_prompt(eecli_ctx_t *cli)
{
    ecli_write(cli, "%s", cli->current_prompt);
}

/*
 * Enter a context mode
 */
static int ecli_enter_context(eecli_ctx_t *cli, const char *context)
{
    context_entry_t *entry = malloc(sizeof(*entry));
    if (!entry)
        return -1;

    entry->name = strdup(context);
    if (!entry->name) {
        free(entry);
        return -1;
    }

    TAILQ_INSERT_TAIL(&cli->context_stack, entry, next);

    cli->context_depth++;
    ecli_update_prompt(cli);
    return 0;
}

/*
 * Exit current context mode (remove last entry from stack)
 */
static int ecli_exit_context(eecli_ctx_t *cli)
{
    if (cli->context_depth == 0) {
        ecli_write(cli, "Already at top level\n");
        return -1;
    }

    /* Remove the last entry */
    context_entry_t *entry = TAILQ_LAST(&cli->context_stack, context_stack_head);
    if (entry) {
        TAILQ_REMOVE(&cli->context_stack, entry, next);
        free(entry->name);
        free(entry);
        cli->context_depth--;
    }

    ecli_update_prompt(cli);
    return 0;
}

/*
 * Exit all contexts and return to top level
 */
static void ecli_exit_all_contexts(eecli_ctx_t *cli)
{
    context_entry_t *entry, *tmp;
    TAILQ_FOREACH_SAFE(entry, &cli->context_stack, next, tmp) {
        TAILQ_REMOVE(&cli->context_stack, entry, next);
        free(entry->name);
        free(entry);
    }
    cli->context_depth = 0;
    ecli_update_prompt(cli);
}

/*
 * Build full command by prepending context prefix
 */
static void ecli_build_full_command(eecli_ctx_t *cli, const char *line, char *full_cmd, size_t size)
{
    if (cli->context_depth == 0) {
        strncpy(full_cmd, line, size - 1);
        full_cmd[size - 1] = '\0';
        return;
    }

    /* Prepend all context levels */
    full_cmd[0] = '\0';
    context_entry_t *entry;
    TAILQ_FOREACH(entry, &cli->context_stack, next) {
        strncat(full_cmd, entry->name, size - strlen(full_cmd) - 1);
        strncat(full_cmd, " ", size - strlen(full_cmd) - 1);
    }
    strncat(full_cmd, line, size - strlen(full_cmd) - 1);
}

/*
 * Try to expand a single token at the end of a partial command.
 * Returns the expanded token or NULL if no unique expansion exists.
 */
static char *expand_single_token(eecli_ctx_t *cli, const char *partial_cmd)
{
    struct ec_comp *comp = ec_complete(cli->grammar, partial_cmd);
    if (!comp)
        return NULL;

    /* Count completions */
    size_t count = ec_comp_count(comp, EC_COMP_FULL | EC_COMP_PARTIAL);

    if (count != 1) {
        ec_comp_free(comp);
        return NULL;
    }

    /* Get the single completion item */
    struct ec_comp_item *item = ec_comp_iter_first(comp, EC_COMP_FULL | EC_COMP_PARTIAL);
    if (!item) {
        ec_comp_free(comp);
        return NULL;
    }

    /* Get the full string that this token should become */
    const char *str = ec_comp_item_get_str(item);
    char *result = str ? strdup(str) : NULL;

    ec_comp_free(comp);
    return result;
}

/*
 * Try to expand abbreviated tokens to their full form.
 * Expands each token in sequence, so "sh run" -> "show run".
 *
 * Returns: newly allocated expanded string, or NULL if no expansion needed/possible.
 * Caller must free the returned string.
 */
static char *expand_prefixes(eecli_ctx_t *cli, const char *cmd)
{
    if (!cli || !cli->grammar || !cmd || !*cmd)
        return NULL;

    /* Make a working copy */
    char *work = strdup(cmd);
    if (!work)
        return NULL;

    /* Result buffer */
    char result[4096];
    result[0] = '\0';
    bool expanded_any = false;

    /* Tokenize and try to expand each token */
    char *saveptr = NULL;
    char *token = strtok_r(work, " \t", &saveptr);

    while (token) {
        /* Build partial command up to this token */
        size_t partial_len = strlen(result) + strlen(token) + 2;
        char *partial = malloc(partial_len);
        if (!partial) {
            free(work);
            return NULL;
        }
        if (result[0]) {
            snprintf(partial, partial_len, "%s %s", result, token);
        } else {
            snprintf(partial, partial_len, "%s", token);
        }

        /* Try to expand this token */
        char *expanded_token = expand_single_token(cli, partial);
        free(partial);

        if (expanded_token) {
            /* Token was expanded - use the expanded form */
            if (result[0]) {
                strncat(result, " ", sizeof(result) - strlen(result) - 1);
            }
            strncat(result, expanded_token, sizeof(result) - strlen(result) - 1);
            if (strcmp(token, expanded_token) != 0) {
                expanded_any = true;
            }
            free(expanded_token);
        } else {
            /* No expansion - use original token */
            if (result[0]) {
                strncat(result, " ", sizeof(result) - strlen(result) - 1);
            }
            strncat(result, token, sizeof(result) - strlen(result) - 1);
        }

        token = strtok_r(NULL, " \t", &saveptr);
    }

    free(work);

    /* Return result only if something was expanded */
    if (expanded_any) {
        return strdup(result);
    }

    return NULL;
}

/*
 * Custom editline interactive loop with prefix expansion support.
 * Similar to ec_editline_interact() but tries to expand abbreviated
 * tokens before showing parse errors.
 */
static int editline_interact_with_expansion(eecli_ctx_t *cli)
{
    struct ec_editline_help *helps = NULL;
    struct ec_pnode *parse = NULL;
    size_t char_idx = 0;
    char *line = NULL;
    ssize_t n;

    while (g_running && *g_running) {
        line = ec_editline_gets(cli->editline);
        if (line == NULL) {
            fprintf(stderr, "\n");
            break;
        }

        /* Skip empty lines */
        char *trimmed = line;
        while (*trimmed == ' ' || *trimmed == '\t') trimmed++;
        if (*trimmed == '\0' || *trimmed == '\n') {
            free(line);
            continue;
        }

        /* Remove trailing newline */
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n')
            line[len - 1] = '\0';

        /* Build full command with context prefixes */
        char full_cmd[1024];
        ecli_build_full_command(cli, trimmed, full_cmd, sizeof(full_cmd));

        /* Try to parse the command */
        parse = ec_parse(cli->grammar, full_cmd);
        if (parse == NULL) {
            fprintf(stderr, "Failed to parse command\n");
            free(line);
            continue;
        }

        if (ec_pnode_matches(parse)) {
            /* Direct match - execute callback */
            ecli_cmd_cb_t cb = ecli_cmd_lookup_callback(parse);
            if (cb) {
                cb(cli, parse);
            } else {
                fprintf(stderr, "No handler for command\n");
            }
            ec_pnode_free(parse);
            free(line);
            continue;
        }

        /* No direct match - try prefix expansion */
        ec_pnode_free(parse);
        parse = NULL;

        char *expanded = expand_prefixes(cli, full_cmd);
        if (expanded) {
            struct ec_pnode *exp_parse = ec_parse(cli->grammar, expanded);
            if (exp_parse && ec_pnode_matches(exp_parse)) {
                /* Expanded command matches - execute it */
                ecli_cmd_cb_t cb = ecli_cmd_lookup_callback(exp_parse);
                if (cb) {
                    cb(cli, exp_parse);
                } else {
                    fprintf(stderr, "No handler for command\n");
                }
                ec_pnode_free(exp_parse);
                free(expanded);
                free(line);
                continue;
            }
            if (exp_parse)
                ec_pnode_free(exp_parse);
            free(expanded);
        }

        /* Show error helps */
        n = ec_editline_get_error_helps(cli->editline, &helps, &char_idx);
        if (n >= 0) {
            ec_editline_print_error_helps(cli->editline, helps, n, char_idx);
            ec_editline_free_helps(helps, n);
        } else {
            fprintf(stderr, "Invalid command\n");
        }
        helps = NULL;

        free(line);
    }

    return 0;
}

static void process_line(eecli_ctx_t *cli, char *line)
{
    /* Trim whitespace */
    while (*line == ' ' || *line == '\t') line++;
    char *end = line + strlen(line) - 1;
    while (end > line && (*end == '\n' || *end == '\r' || *end == ' ')) {
        *end-- = '\0';
    }

    if (*line == '\0') {
        ecli_prompt(cli);
        return;
    }

    /* Handle reserved commands for context navigation */
    if (strcmp(line, "end") == 0) {
        ecli_exit_all_contexts(cli);
        ecli_prompt(cli);
        return;
    }
    if (strcmp(line, "exit") == 0 && cli->context_depth > 0) {
        /* In context mode, "exit" returns to parent context */
        ecli_exit_context(cli);
        ecli_prompt(cli);
        return;
    }
    /* At top level, "exit" is handled by the grammar (alias to quit) */

    /* Build full command with context prefix */
    char full_cmd[512];
    ecli_build_full_command(cli, line, full_cmd, sizeof(full_cmd));

    /* Parse using libecoli grammar */
    struct ec_pnode *parse = ec_parse(cli->grammar, full_cmd);
    if (!parse) {
        ecli_err(cli, "Parse error\n");
        ecli_prompt(cli);
        return;
    }

    /* Check if command matches */
    if (!ec_pnode_matches(parse)) {
        ec_pnode_free(parse);

        /* Try to expand prefixes (e.g., "write term" -> "write terminal") */
        char *expanded = expand_prefixes(cli, full_cmd);
        if (expanded) {
            struct ec_pnode *exp_parse = ec_parse(cli->grammar, expanded);
            if (exp_parse && ec_pnode_matches(exp_parse)) {
                /* Expanded command matches - execute it */
                int ret;
                if (cli->use_yaml) {
                    ret = ecli_yaml_dispatch(cli, exp_parse);
                } else {
                    ecli_cmd_cb_t cb = ecli_cmd_lookup_callback(exp_parse);
                    ret = cb ? cb(cli, exp_parse) : -1;
                }
                if (ret < 0) {
                    ecli_err(cli, "No handler for command\n");
                }
                ec_pnode_free(exp_parse);
                free(expanded);
                ecli_prompt(cli);
                return;
            }
            if (exp_parse)
                ec_pnode_free(exp_parse);
            free(expanded);
        }

        /* Single word that doesn't match - check if it's a registered context group */
        if (strchr(line, ' ') == NULL && is_context_group(line)) {
            ecli_enter_context(cli, line);
            ecli_prompt(cli);
            return;
        }

        ecli_err(cli, "Unknown command: %s\n", line);
        ecli_prompt(cli);
        return;
    }

    /* Find and execute callback */
    int ret;
    if (cli->use_yaml) {
        ret = ecli_yaml_dispatch(cli, parse);
    } else {
        ecli_cmd_cb_t cb = ecli_cmd_lookup_callback(parse);
        ret = cb ? cb(cli, parse) : -1;
    }
    if (ret < 0) {
        ecli_err(cli, "No handler for command\n");
    }

    ec_pnode_free(parse);
    ecli_prompt(cli);
}

/* TCP callbacks */
static void tcp_read_cb(struct bufferevent *bev, void *arg)
{
    eecli_ctx_t *cli = arg;
    struct evbuffer *input = bufferevent_get_input(bev);

    char *line;
    while ((line = evbuffer_readln(input, NULL, EVBUFFER_EOL_ANY)) != NULL) {
        process_line(cli, line);
        free(line);
    }
}

static void tcp_event_cb(struct bufferevent *bev, short events, void *arg)
{
    eecli_ctx_t *cli = arg;
    (void)bev;

    if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR)) {
        bufferevent_free(cli->client_bev);
        cli->client_bev = NULL;
        cli->has_client = false;
    }
}

static void tcp_accept_cb(struct evconnlistener *listener, evutil_socket_t fd,
                          struct sockaddr *addr, int socklen, void *arg)
{
    eecli_ctx_t *cli = arg;
    (void)listener;

    if (cli->has_client) {
        /* Only allow one client - show who is connected */
        char msg[128];
        char ip_str[INET6_ADDRSTRLEN];
        uint16_t port = 0;

        if (cli->client_addr.ss_family == AF_INET) {
            struct sockaddr_in *sin = (struct sockaddr_in *)&cli->client_addr;
            inet_ntop(AF_INET, &sin->sin_addr, ip_str, sizeof(ip_str));
            port = ntohs(sin->sin_port);
        } else if (cli->client_addr.ss_family == AF_INET6) {
            struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)&cli->client_addr;
            inet_ntop(AF_INET6, &sin6->sin6_addr, ip_str, sizeof(ip_str));
            port = ntohs(sin6->sin6_port);
        } else {
            snprintf(ip_str, sizeof(ip_str), "unknown");
        }

        snprintf(msg, sizeof(msg),
                 "Another session is active from %s:%u\r\n", ip_str, port);
        ssize_t ret = write(fd, msg, strlen(msg));
        (void)ret;
        close(fd);
        return;
    }

    cli->client_bev = bufferevent_socket_new(
        evconnlistener_get_base(listener), fd, BEV_OPT_CLOSE_ON_FREE);
    if (!cli->client_bev) {
        close(fd);
        return;
    }

    /* Store the client address */
    memcpy(&cli->client_addr, addr, socklen);
    cli->client_addrlen = socklen;

    bufferevent_setcb(cli->client_bev, tcp_read_cb, NULL, tcp_event_cb, cli);
    bufferevent_enable(cli->client_bev, EV_READ | EV_WRITE);

    cli->has_client = true;

    if (cli->config.banner) {
        ecli_write(cli, "%s v%s\r\n", cli->config.banner, cli->config.version);
    }
    ecli_prompt(cli);
}


static int ecli_init_common(eecli_ctx_t *cli, const ecli_config_t *config)
{
    /* Apply configuration */
    if (config) {
        cli->config = *config;
    } else {
        cli->config = (ecli_config_t)ECLI_CONFIG_DEFAULT;
    }

    /* Set defaults for missing values */
    if (!cli->config.prompt)
        cli->config.prompt = "cli> ";
    if (!cli->config.version)
        cli->config.version = "1.0.0";
    if (!cli->config.grammar_env)
        cli->config.grammar_env = "ECLI_GRAMMAR";

    /* Initialize libecoli */
    if (ec_init() < 0) {
        fprintf(stderr, " Failed to initialize libecoli: %s\n", strerror(errno));
        return -1;
    }

    /* Try to load YAML grammar if specified */
    const char *yaml_file = getenv(cli->config.grammar_env);
    if (yaml_file && yaml_file[0]) {
        cli->grammar = ecli_yaml_load(yaml_file);
        if (cli->grammar) {
            cli->use_yaml = true;
        }
    }

    /* Fall back to C macro-based grammar */
    if (!cli->grammar) {
        cli->grammar = ecli_cmd_get_commands();
        if (!cli->grammar) {
            fprintf(stderr, " Failed to create CLI grammar\n");
            return -1;
        }
    }

    return 0;
}

int ecli_init(const ecli_config_t *config)
{
    if (g_ecli_ctx) {
        fprintf(stderr, " Already initialized\n");
        return -1;
    }

    eecli_ctx_t *cli = calloc(1, sizeof(*cli));
    if (!cli) {
        return -ENOMEM;
    }

    cli->mode = ECLI_MODE_STDIN;
    cli->use_editline = false;
    cli->use_yaml = false;
    cli->context_depth = 0;
    TAILQ_INIT(&cli->context_stack);

    if (ecli_init_common(cli, config) < 0) {
        free(cli);
        return -1;
    }

    /* Initialize prompt */
    ecli_update_prompt(cli);

    /* Foreground mode - try to use libecoli editline */
    if (isatty(STDIN_FILENO)) {
        cli->editline = ec_editline("cli", stdin, stdout, stderr, 0);
        if (cli->editline) {
            if (ec_editline_set_prompt(cli->editline, cli->config.prompt) < 0) {
                fprintf(stderr, "Failed to set editline prompt\n");
            }
            ec_editline_set_node(cli->editline, cli->grammar);
            cli->use_editline = true;
        }
    }

    if (cli->config.banner) {
        printf("%s v%s\n", cli->config.banner, cli->config.version);
    }
    printf("Type 'help' for commands, TAB for completion.\n");

    g_ecli_ctx = cli;
    return 0;
}

int ecli_init_tcp(const ecli_config_t *config, struct event_base *event_base, uint16_t port)
{
    if (g_ecli_ctx) {
        fprintf(stderr, " Already initialized\n");
        return -1;
    }

    eecli_ctx_t *cli = calloc(1, sizeof(*cli));
    if (!cli) {
        return -ENOMEM;
    }

    cli->mode = ECLI_MODE_TCP;
    cli->event_base = event_base;
    cli->tcp_port = port;
    cli->use_editline = false;
    cli->use_yaml = false;
    cli->context_depth = 0;
    TAILQ_INIT(&cli->context_stack);

    if (ecli_init_common(cli, config) < 0) {
        free(cli);
        return -1;
    }

    /* Initialize prompt */
    ecli_update_prompt(cli);

    /* Set up TCP listener */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);

    cli->listener = evconnlistener_new_bind(event_base,
                                             tcp_accept_cb, cli,
                                             LEV_OPT_CLOSE_ON_FREE |
                                             LEV_OPT_REUSEABLE,
                                             -1,
                                             (struct sockaddr *)&addr,
                                             sizeof(addr));
    if (!cli->listener) {
        fprintf(stderr, "Failed to create TCP listener on port %u: %s\n",
                port, strerror(errno));
        ec_node_free(cli->grammar);
        free(cli);
        return -1;
    }

    g_ecli_ctx = cli;
    return 0;
}

void ecli_shutdown(void)
{
    eecli_ctx_t *cli = g_ecli_ctx;

    if (!cli) return;

    /* Free context stack */
    context_entry_t *entry, *tmp;
    TAILQ_FOREACH_SAFE(entry, &cli->context_stack, next, tmp) {
        TAILQ_REMOVE(&cli->context_stack, entry, next);
        free(entry->name);
        free(entry);
    }

    if (cli->listener) {
        evconnlistener_free(cli->listener);
    }
    if (cli->client_bev) {
        bufferevent_free(cli->client_bev);
    }
    if (cli->editline) {
        ec_editline_free(cli->editline);
    }
    if (cli->grammar && !cli->use_yaml) {
        ec_node_free(cli->grammar);
    }

    free(cli);
    g_ecli_ctx = NULL;
}

int ecli_run(volatile bool *running)
{
    eecli_ctx_t *cli = g_ecli_ctx;

    if (!cli)
        return -1;

    g_running = running;

    /* TCP mode - run the event loop */
    if (cli->mode == ECLI_MODE_TCP) {
        while (running && *running) {
            event_base_loop(cli->event_base, EVLOOP_ONCE);
        }
        return 0;
    }

    /* Foreground mode with editline - use custom loop with prefix expansion */
    if (cli->use_editline) {
        return editline_interact_with_expansion(cli);
    }

    /* Basic stdin loop when editline is not available */
    char line[1024];

    ecli_prompt(cli);
    while (running && *running && fgets(line, sizeof(line), stdin) != NULL) {
        process_line(cli, line);
    }

    return 0;
}

bool ecli_uses_editline(void)
{
    return g_ecli_ctx && g_ecli_ctx->use_editline;
}

ecli_mode_t ecli_get_mode(void)
{
    return g_ecli_ctx ? g_ecli_ctx->mode : ECLI_MODE_STDIN;
}

void ecli_output(eecli_ctx_t *cli, const char *fmt, ...)
{
    va_list args;
    char buf[1024];

    /* Use global context if cli is NULL */
    if (!cli)
        cli = g_ecli_ctx;
    if (!cli)
        return;

    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    ecli_write(cli, "%s", buf);
}

void ecli_err(eecli_ctx_t *cli, const char *fmt, ...)
{
    va_list args;
    char buf[1024];

    /* Use global context if cli is NULL */
    if (!cli)
        cli = g_ecli_ctx;
    if (!cli)
        return;

    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    ecli_write(cli, "Error: %s", buf);
}

/*
 * Argument helpers for CLI commands
 */
const char *ecli_arg_str(const struct ec_pnode *parse, const char *id)
{
    const struct ec_pnode *node = ec_pnode_find(parse, id);
    if (!node)
        return NULL;
    const struct ec_strvec *vec = ec_pnode_get_strvec(node);
    if (!vec || ec_strvec_len(vec) == 0)
        return NULL;
    return ec_strvec_val(vec, 0);
}

int ecli_arg_int(const struct ec_pnode *parse, const char *id, int def)
{
    const char *str = ecli_arg_str(parse, id);
    if (!str)
        return def;
    return atoi(str);
}

/*
 * Output function registration for write terminal/file
 */
static ecli_out_entry_t *g_cli_out_head = NULL;

void ecli_out_register(const char *name, const char *group,
                      const char *default_fmt, ecli_out_t func, int priority)
{
    ecli_out_entry_t *entry = calloc(1, sizeof(*entry));
    if (!entry) {
        fprintf(stderr, " Failed to allocate output registration for %s\n", name);
        return;
    }

    entry->name = name;
    entry->group = group;
    entry->default_fmt = default_fmt;
    entry->func = func;
    entry->priority = priority;

    /* Insert sorted by priority (lower = earlier) */
    ecli_out_entry_t **pp = &g_cli_out_head;
    while (*pp && (*pp)->priority <= priority) {
        pp = &(*pp)->next;
    }
    entry->next = *pp;
    *pp = entry;
}

/*
 * Get output format string - YAML override or default
 */
const char *ecli_out_get_fmt(const char *name, const char *default_fmt)
{
    /* Check for YAML override */
    const char *yaml_fmt = ecli_yaml_get_output_fmt(name);
    return yaml_fmt ? yaml_fmt : default_fmt;
}

/*
 * Dump all registered outputs (for write terminal)
 */
void ecli_dump_running_config(eecli_ctx_t *cli, FILE *fp)
{
    const char *current_group = NULL;

    /* Header */
    ECLI_OUT(cli, fp, "! running configuration\n");
    ECLI_OUT(cli, fp, "!\n");

    for (ecli_out_entry_t *e = g_cli_out_head; e; e = e->next) {
        /* Print group separator if group changed */
        if (e->group && (!current_group || strcmp(current_group, e->group) != 0)) {
            if (current_group)
                ECLI_OUT(cli, fp, "! end %s\n", current_group);
            ECLI_OUT(cli, fp, "! %s configuration\n", e->group);
            current_group = e->group;
        }

        /* Call the output function with format string */
        if (e->func) {
            const char *fmt = ecli_out_get_fmt(e->name, e->default_fmt);
            e->func(cli, fp, fmt);
        }
    }

    /* End the last group if any */
    if (current_group)
        ECLI_OUT(cli, fp, "! end %s\n", current_group);

    ECLI_OUT(cli, fp, "!\n");
    ECLI_OUT(cli, fp, "! end\n");
}

/*
 * Named parameter value storage for ecli_out_fmt
 */
#define ECLI_FMT_MAX_PARAMS 16

typedef struct {
    const char     *name;
    ecli_fmt_type_t  type;
    union {
        const char    *str;
        int            i;
        unsigned int   u;
        long           l;
        unsigned long  ul;
    } val;
} ecli_fmt_param_t;

/*
 * ecli_out_fmt - output with named {param} substitution
 *
 * Parses format string for {name} placeholders and substitutes
 * with provided key-value pairs.
 */
void ecli_out_fmt(eecli_ctx_t *cli, FILE *fp, const char *fmt, ...)
{
    va_list ap;
    ecli_fmt_param_t params[ECLI_FMT_MAX_PARAMS];
    int num_params = 0;
    char output[1024];
    char *out = output;
    char *out_end = output + sizeof(output) - 1;
    const char *p = fmt;

    if (!fmt)
        return;

    /* Parse varargs into params array */
    va_start(ap, fmt);
    while (num_params < ECLI_FMT_MAX_PARAMS) {
        const char *name = va_arg(ap, const char *);
        if (!name)
            break;  /* NULL sentinel */

        ecli_fmt_type_t type = va_arg(ap, ecli_fmt_type_t);
        params[num_params].name = name;
        params[num_params].type = type;

        switch (type) {
        case ECLI_FMT_STR:
            params[num_params].val.str = va_arg(ap, const char *);
            break;
        case ECLI_FMT_INT:
            params[num_params].val.i = va_arg(ap, int);
            break;
        case ECLI_FMT_UINT:
            params[num_params].val.u = va_arg(ap, unsigned int);
            break;
        case ECLI_FMT_LONG:
            params[num_params].val.l = va_arg(ap, long);
            break;
        case ECLI_FMT_ULONG:
            params[num_params].val.ul = va_arg(ap, unsigned long);
            break;
        default:
            break;
        }
        num_params++;
    }
    va_end(ap);

    /* Process format string */
    while (*p && out < out_end) {
        if (*p == '{') {
            /* Find closing brace */
            const char *end = strchr(p + 1, '}');
            if (end) {
                size_t name_len = end - p - 1;
                char name_buf[64];
                int found = 0;

                if (name_len < sizeof(name_buf)) {
                    memcpy(name_buf, p + 1, name_len);
                    name_buf[name_len] = '\0';

                    /* Look up parameter */
                    for (int i = 0; i < num_params; i++) {
                        if (strcmp(params[i].name, name_buf) == 0) {
                            int written = 0;
                            switch (params[i].type) {
                            case ECLI_FMT_STR:
                                written = snprintf(out, out_end - out, "%s",
                                    params[i].val.str ? params[i].val.str : "(null)");
                                break;
                            case ECLI_FMT_INT:
                                written = snprintf(out, out_end - out, "%d",
                                    params[i].val.i);
                                break;
                            case ECLI_FMT_UINT:
                                written = snprintf(out, out_end - out, "%u",
                                    params[i].val.u);
                                break;
                            case ECLI_FMT_LONG:
                                written = snprintf(out, out_end - out, "%ld",
                                    params[i].val.l);
                                break;
                            case ECLI_FMT_ULONG:
                                written = snprintf(out, out_end - out, "%lu",
                                    params[i].val.ul);
                                break;
                            default:
                                break;
                            }
                            if (written > 0)
                                out += written;
                            found = 1;
                            break;
                        }
                    }
                }

                if (!found) {
                    /* Unknown param - output as-is */
                    while (p <= end && out < out_end)
                        *out++ = *p++;
                    continue;
                }
                p = end + 1;
                continue;
            }
        }
        *out++ = *p++;
    }
    *out = '\0';

    /* Output result */
    ECLI_OUT(cli, fp, "%s", output);
}

/*
 * Helper to get child node (libecoli uses output param)
 */
static struct ec_node *get_child(const struct ec_node *node, size_t i)
{
    struct ec_node *child = NULL;
    if (ec_node_get_child(node, i, &child) == 0)
        return child;
    return NULL;
}

/*
 * Helper to get string from a str node's config
 */
static const char *get_str_value(const struct ec_node *node)
{
    const struct ec_config *config = ec_node_get_config(node);
    if (!config)
        return NULL;
    struct ec_config *str_cfg = ec_config_dict_get(config, "string");
    if (!str_cfg || str_cfg->type != EC_CONFIG_TYPE_STRING)
        return NULL;
    return str_cfg->string;
}

/*
 * Helper to get command string from a cmd node's config
 */
static const char *get_cmd_expr(const struct ec_node *node)
{
    const struct ec_config *config = ec_node_get_config(node);
    if (!config)
        return NULL;
    struct ec_config *expr_cfg = ec_config_dict_get(config, "expr");
    if (!expr_cfg || expr_cfg->type != EC_CONFIG_TYPE_STRING)
        return NULL;
    return expr_cfg->string;
}

/*
 * Recursive helper to walk the node tree and display help
 */
static void show_help_recursive(eecli_ctx_t *cli, const struct ec_node *node,
                                char *prefix, size_t prefix_size)
{
    if (!node)
        return;

    const char *node_type = ec_node_type_name(ec_node_type(node));
    struct ec_dict *attrs = ec_node_attrs(node);
    const char *help = attrs ? ec_dict_get(attrs, ECLI_HELP_ATTR) : NULL;

    if (strcmp(node_type, "cmd") == 0) {
        /* CMD node - get command string and display if has help */
        const char *cmd_str = get_cmd_expr(node);
        if (cmd_str && help) {
            if (prefix[0]) {
                ecli_output(cli, "  %s %s - %s\n", prefix, cmd_str, help);
            } else {
                ecli_output(cli, "  %s - %s\n", cmd_str, help);
            }
        }
        return;
    }

    if (strcmp(node_type, "str") == 0) {
        /* STR node - display if has help (used for aliases) */
        const char *str_val = get_str_value(node);
        if (str_val && help) {
            if (prefix[0]) {
                ecli_output(cli, "  %s %s - %s\n", prefix, str_val, help);
            } else {
                ecli_output(cli, "  %s - %s\n", str_val, help);
            }
        }
        return;
    }

    if (strcmp(node_type, "seq") == 0) {
        /* SEQ node - first child is usually keyword, rest are args */
        size_t nchildren = ec_node_get_children_count(node);
        if (nchildren > 0) {
            struct ec_node *first = get_child(node, 0);
            if (first) {
                const char *first_type = ec_node_type_name(ec_node_type(first));

                /* If first child is a string (keyword), add to prefix */
                if (strcmp(first_type, "str") == 0) {
                    const char *str_val = get_str_value(first);
                    if (str_val) {
                        char new_prefix[128];
                        if (prefix[0]) {
                            snprintf(new_prefix, sizeof(new_prefix), "%s %s",
                                     prefix, str_val);
                        } else {
                            snprintf(new_prefix, sizeof(new_prefix), "%s",
                                     str_val);
                        }

                        /*
                         * Check if this is a group (has "or" child) or a leaf command.
                         * Groups recurse to show subcommands.
                         * Leaf commands with help display directly (handles
                         * ECLI_DEFUN_SUB_NODE with complex argument structures).
                         */
                        bool is_group = false;
                        for (size_t i = 1; i < nchildren; i++) {
                            struct ec_node *child = get_child(node, i);
                            if (child) {
                                const char *child_type = ec_node_type_name(
                                    ec_node_type(child));
                                if (strcmp(child_type, "or") == 0) {
                                    is_group = true;
                                    break;
                                }
                            }
                        }

                        if (help && !is_group) {
                            /* Leaf command with help - display it */
                            ecli_output(cli, "  %s - %s\n", new_prefix, help);
                            return;
                        }

                        /* Recurse into remaining children with new prefix */
                        for (size_t i = 1; i < nchildren; i++) {
                            show_help_recursive(cli, get_child(node, i),
                                               new_prefix, sizeof(new_prefix));
                        }
                        return;
                    }
                }
            }
        }
        /* Fallthrough: recurse into all children */
    }

    if (strcmp(node_type, "or") == 0 || strcmp(node_type, "sh_lex") == 0) {
        /* OR/sh_lex - iterate children with same prefix */
        size_t nchildren = ec_node_get_children_count(node);
        for (size_t i = 0; i < nchildren; i++) {
            show_help_recursive(cli, get_child(node, i), prefix, prefix_size);
        }
        return;
    }

    /* For other node types, recurse into children */
    size_t nchildren = ec_node_get_children_count(node);
    for (size_t i = 0; i < nchildren; i++) {
        show_help_recursive(cli, get_child(node, i), prefix, prefix_size);
    }
}

/*
 * Display help by walking the CLI command tree
 */
void ecli_show_help(eecli_ctx_t *cli)
{
    eecli_ctx_t *ctx = cli ? cli : g_ecli_ctx;
    if (!ctx || !ctx->grammar) {
        ecli_output(cli, "No commands available\n");
        return;
    }

    ecli_output(cli, "Commands:\n");
    char prefix[128] = "";
    show_help_recursive(ctx, ctx->grammar, prefix, sizeof(prefix));
}

/*
 * Callback lookup - searches parse tree for our callback attribute
 */
ecli_cmd_cb_t ecli_cmd_lookup_callback(const struct ec_pnode *parse)
{
    const struct ec_pnode *p;

    EC_PNODE_FOREACH(p, parse) {
        const struct ec_node *node = ec_pnode_get_node(p);
        struct ec_dict *attrs = ec_node_attrs(node);
        if (attrs) {
            ecli_cmd_cb_t cb = ec_dict_get(attrs, ECLI_CB_ATTR);
            if (cb)
                return cb;
        }
    }

    return NULL;
}

/*
 * Wrapper callback for libecoli editline integration
 *
 * This function bridges libecoli's editline callback mechanism (which expects
 * a function taking only the parse tree) with libecli's command handlers
 * (which also need the CLI context).
 *
 * When ec_editline_interact() successfully parses a command, it looks for
 * a callback stored under the EC_EDITLINE_CB_ATTR ("_cb") attribute on
 * matched nodes. This wrapper is stored there and performs:
 * 1. Retrieves the global CLI context
 * 2. Looks up the actual libecli callback from ECLI_CB_ATTR
 * 3. Invokes it with the CLI context and parse tree
 */
int ecli_editline_cmd_wrapper(const struct ec_pnode *parse)
{
    eecli_ctx_t *cli = g_ecli_ctx;
    if (!cli)
        return -1;

    int ret;
    if (cli->use_yaml) {
        ret = ecli_yaml_dispatch(cli, parse);
    } else {
        ecli_cmd_cb_t cb = ecli_cmd_lookup_callback(parse);
        ret = cb ? cb(cli, parse) : -1;
    }

    if (ret < 0) {
        ecli_err(cli, "No handler for command\n");
    }

    return ret;
}

/*
 * Execute a single command (for config file loading)
 *
 * Similar to process_line() but without interactive features:
 * - No prompt output
 * - No context navigation (end/exit)
 * - Returns success/failure for error counting
 *
 * Returns:
 *   0 on success
 *   -1 on error (parse failure, no handler, handler error)
 */
static int execute_command(eecli_ctx_t *cli, const char *line)
{
    /* Build full command with context prefix */
    char full_cmd[512];
    ecli_build_full_command(cli, line, full_cmd, sizeof(full_cmd));

    /* Parse using libecoli grammar */
    struct ec_pnode *parse = ec_parse(cli->grammar, full_cmd);
    if (!parse) {
        fprintf(stderr, " Config: parse error for: %s\n", line);
        return -1;
    }

    /* Check if command matches */
    if (!ec_pnode_matches(parse)) {
        ec_pnode_free(parse);
        fprintf(stderr, " Config: unknown command: %s\n", line);
        return -1;
    }

    /* Find and execute callback */
    int ret;
    if (cli->use_yaml) {
        ret = ecli_yaml_dispatch(cli, parse);
    } else {
        ecli_cmd_cb_t cb = ecli_cmd_lookup_callback(parse);
        ret = cb ? cb(cli, parse) : -1;
    }

    if (ret < 0) {
        fprintf(stderr, " Config: command failed: %s\n", line);
    }

    ec_pnode_free(parse);
    return ret;
}

/*
 * ecli_load_config - Load and replay configuration from a file
 */
int ecli_load_config(const char *filename)
{
    eecli_ctx_t *cli = g_ecli_ctx;
    if (!cli) {
        fprintf(stderr, " ecli_load_config: CLI not initialized\n");
        return -1;
    }

    FILE *fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "Cannot open config file: %s: %s\n",
                filename, strerror(errno));
        return -1;
    }

    char line[1024];
    int line_num = 0;
    int error_count = 0;
    int cmd_count = 0;

    while (fgets(line, sizeof(line), fp) != NULL) {
        line_num++;

        /* Trim leading whitespace */
        char *p = line;
        while (*p == ' ' || *p == '\t')
            p++;

        /* Trim trailing whitespace/newline */
        char *end = p + strlen(p) - 1;
        while (end > p && (*end == '\n' || *end == '\r' || *end == ' ' || *end == '\t'))
            *end-- = '\0';

        /* Skip empty lines */
        if (*p == '\0')
            continue;

        /* Skip comments (lines starting with '!' or '#') */
        if (*p == '!' || *p == '#')
            continue;

        /* Execute the command */
        cmd_count++;
        if (execute_command(cli, p) < 0) {
            fprintf(stderr, " Config error at line %d: %s\n", line_num, p);
            error_count++;
        }
    }

    fclose(fp);

    return error_count;
}

/*
 * CLI Documentation System
 *
 * Documentation entries are stored in the "ecli_doc" ELF section.
 * We use weak symbols for the section markers so the code works
 * even when no ECLI_DOC entries exist or when the section is stripped.
 */

/* Section markers (defined by linker for "ecli_doc" section)
 * Weak symbols allow the code to work when documentation is stripped. */
extern const ecli_doc_entry_t __start_ecli_doc[] __attribute__((weak));
extern const ecli_doc_entry_t __stop_ecli_doc[] __attribute__((weak));

/*
 * ecli_doc_lookup - Find documentation entry by command name
 *
 * Returns NULL if:
 *   - cmd_name is NULL
 *   - Documentation section was stripped
 *   - Command not found in documentation
 */
const ecli_doc_entry_t *ecli_doc_lookup(const char *cmd_name)
{
    if (!cmd_name)
        return NULL;

    /* Check if documentation is available (weak symbols are NULL when no ecli_doc section) */
    if ((const void *)__start_ecli_doc == NULL ||
        (const void *)__stop_ecli_doc == NULL ||
        (const void *)__start_ecli_doc >= (const void *)__stop_ecli_doc)
        return NULL;

    /* Iterate through documentation entries */
    for (const ecli_doc_entry_t *doc = __start_ecli_doc;
         doc < __stop_ecli_doc; doc++) {
        if (doc->cmd_name == NULL)
            continue;
        if (strcmp(doc->cmd_name, cmd_name) == 0)
            return doc;
    }
    return NULL;
}

/*
 * Helper to extract command info from the grammar tree
 */
static const char *find_cmd_help(const struct ec_node *node, const char *cb_name)
{
    if (!node)
        return NULL;

    /* Check this node */
    struct ec_dict *attrs = ec_node_attrs(node);
    if (attrs) {
        const char *node_cb = ec_dict_get(attrs, ECLI_CB_NAME_ATTR);
        if (node_cb && strcmp(node_cb, cb_name) == 0) {
            return ec_dict_get(attrs, ECLI_HELP_ATTR);
        }
    }

    /* Recurse into children */
    size_t n = ec_node_get_children_count(node);
    for (size_t i = 0; i < n; i++) {
        struct ec_node *child = NULL;
        if (ec_node_get_child(node, i, &child) == 0 && child) {
            const char *help = find_cmd_help(child, cb_name);
            if (help)
                return help;
        }
    }
    return NULL;
}

/*
 * Build syntax string from a node tree recursively
 */
static void build_syntax_recursive(const struct ec_node *node, char *buf,
                                   size_t size, size_t *pos)
{
    if (!node || *pos >= size - 1)
        return;

    const char *type = ec_node_type_name(ec_node_type(node));
    struct ec_dict *attrs = ec_node_attrs(node);

    if (strcmp(type, "str") == 0) {
        /* Keyword - output as-is */
        const char *str = get_str_value(node);
        if (str) {
            int n = snprintf(buf + *pos, size - *pos, "%s%s",
                            *pos > 0 ? " " : "", str);
            if (n > 0) *pos += n;
        }
    } else if (strcmp(type, "int") == 0 || strcmp(type, "uint") == 0 ||
               strcmp(type, "re") == 0) {
        /* Argument - use ID or help text */
        const char *id = ec_node_id(node);
        const char *help = attrs ? ec_dict_get(attrs, ECLI_HELP_ATTR) : NULL;
        const char *name = (id && strcmp(id, EC_NO_ID) != 0) ? id :
                          (help ? help : type);
        int n = snprintf(buf + *pos, size - *pos, "%s<%s>",
                        *pos > 0 ? " " : "", name);
        if (n > 0) *pos += n;
    } else if (strcmp(type, "option") == 0) {
        /* Optional - wrap in brackets */
        size_t start = *pos;
        int n = snprintf(buf + *pos, size - *pos, "%s[",
                        *pos > 0 ? " " : "");
        if (n > 0) *pos += n;
        size_t inner_start = *pos;
        size_t nchildren = ec_node_get_children_count(node);
        for (size_t i = 0; i < nchildren; i++) {
            build_syntax_recursive(get_child(node, i), buf, size, pos);
        }
        /* Remove leading space inside brackets if any */
        if (*pos > inner_start && buf[inner_start] == ' ') {
            memmove(buf + inner_start, buf + inner_start + 1, *pos - inner_start);
            (*pos)--;
        }
        n = snprintf(buf + *pos, size - *pos, "]");
        if (n > 0) *pos += n;
        (void)start;
    } else if (strcmp(type, "seq") == 0) {
        /* Sequence - output children in order */
        size_t nchildren = ec_node_get_children_count(node);
        for (size_t i = 0; i < nchildren; i++) {
            build_syntax_recursive(get_child(node, i), buf, size, pos);
        }
    } else if (strcmp(type, "or") == 0) {
        /* Alternatives - join with | */
        size_t nchildren = ec_node_get_children_count(node);
        if (nchildren > 1) {
            int n = snprintf(buf + *pos, size - *pos, "%s(",
                            *pos > 0 ? " " : "");
            if (n > 0) *pos += n;
        }
        for (size_t i = 0; i < nchildren; i++) {
            if (i > 0) {
                int n = snprintf(buf + *pos, size - *pos, "|");
                if (n > 0) *pos += n;
            }
            size_t child_start = *pos;
            build_syntax_recursive(get_child(node, i), buf, size, pos);
            /* Remove leading space after | */
            if (*pos > child_start && buf[child_start] == ' ') {
                memmove(buf + child_start, buf + child_start + 1,
                       *pos - child_start);
                (*pos)--;
            }
        }
        if (nchildren > 1) {
            int n = snprintf(buf + *pos, size - *pos, ")");
            if (n > 0) *pos += n;
        }
    } else {
        /* Other node types - recurse into children */
        size_t nchildren = ec_node_get_children_count(node);
        for (size_t i = 0; i < nchildren; i++) {
            build_syntax_recursive(get_child(node, i), buf, size, pos);
        }
    }
}

/*
 * Find command node by callback name, returning prefix path
 */
static const struct ec_node *find_cmd_node_with_prefix(const struct ec_node *node,
                                                       const char *cb_name,
                                                       char *prefix, size_t prefix_size)
{
    if (!node)
        return NULL;

    const char *type = ec_node_type_name(ec_node_type(node));
    struct ec_dict *attrs = ec_node_attrs(node);

    /* Check if this node has the callback we're looking for */
    if (attrs) {
        const char *node_cb = ec_dict_get(attrs, ECLI_CB_NAME_ATTR);
        if (node_cb && strcmp(node_cb, cb_name) == 0) {
            return node;
        }
    }

    /* For seq nodes, first child might be a keyword to add to prefix */
    if (strcmp(type, "seq") == 0) {
        size_t n = ec_node_get_children_count(node);
        if (n > 0) {
            struct ec_node *first = get_child(node, 0);
            if (first) {
                const char *first_type = ec_node_type_name(ec_node_type(first));
                if (strcmp(first_type, "str") == 0) {
                    const char *keyword = get_str_value(first);
                    if (keyword) {
                        /* Save current prefix length */
                        size_t orig_len = strlen(prefix);
                        /* Append keyword to prefix */
                        if (orig_len > 0) {
                            strncat(prefix, " ", prefix_size - strlen(prefix) - 1);
                        }
                        strncat(prefix, keyword, prefix_size - strlen(prefix) - 1);

                        /* Search remaining children */
                        for (size_t i = 1; i < n; i++) {
                            const struct ec_node *found =
                                find_cmd_node_with_prefix(get_child(node, i),
                                                         cb_name, prefix, prefix_size);
                            if (found)
                                return found;
                        }

                        /* Not found - restore prefix */
                        prefix[orig_len] = '\0';
                    }
                }
            }
        }
    }

    /* Recurse into children */
    size_t n = ec_node_get_children_count(node);
    for (size_t i = 0; i < n; i++) {
        struct ec_node *child = NULL;
        if (ec_node_get_child(node, i, &child) == 0 && child) {
            const struct ec_node *found =
                find_cmd_node_with_prefix(child, cb_name, prefix, prefix_size);
            if (found)
                return found;
        }
    }
    return NULL;
}

/*
 * Build syntax string for a command - caller must free result
 */
static char *build_cmd_syntax(const struct ec_node *grammar, const char *cb_name)
{
    char prefix[256] = "";
    const struct ec_node *cmd_node =
        find_cmd_node_with_prefix(grammar, cb_name, prefix, sizeof(prefix));
    if (!cmd_node)
        return NULL;

    /* First check if node has an explicit expr config */
    const struct ec_config *cfg = ec_node_get_config(cmd_node);
    if (cfg) {
        struct ec_config *expr = ec_config_dict_get(cfg, "expr");
        if (expr && expr->type == EC_CONFIG_TYPE_STRING)
            return strdup(expr->string);
    }

    /* Build syntax from node tree */
    char *buf = malloc(512);
    if (!buf)
        return NULL;

    buf[0] = '\0';
    size_t pos = 0;

    /* Start with prefix (group keywords) */
    if (prefix[0]) {
        int n = snprintf(buf, 512, "%s", prefix);
        if (n > 0) pos = n;
    }

    /* Add command syntax from children */
    size_t nchildren = ec_node_get_children_count(cmd_node);
    for (size_t i = 0; i < nchildren; i++) {
        build_syntax_recursive(get_child(cmd_node, i), buf, 512, &pos);
    }

    if (pos == 0) {
        free(buf);
        return NULL;
    }

    return buf;
}

/*
 * ecli_show_doc - Display documentation for a command
 */
void ecli_show_doc(eecli_ctx_t *cli, const char *cmd_name)
{
    eecli_ctx_t *ctx = cli ? cli : g_ecli_ctx;
    if (!ctx)
        return;

    const ecli_doc_entry_t *doc = ecli_doc_lookup(cmd_name);

    /* Get command syntax and help from grammar */
    char *cmd_syntax = NULL;
    const char *cmd_help = NULL;

    if (ctx->grammar) {
        cmd_syntax = build_cmd_syntax(ctx->grammar, cmd_name);
        cmd_help = find_cmd_help(ctx->grammar, cmd_name);
    }

    /* Output documentation */
    ecli_output(ctx, "\n");
    ecli_output(ctx, "Syntax:\n");

    if (cmd_syntax) {
        ecli_output(ctx, "    %s\n", cmd_syntax);
        free(cmd_syntax);
    } else {
        ecli_output(ctx, "    %s\n", cmd_name);
    }

    ecli_output(ctx, "\n");

    if (cmd_help) {
        ecli_output(ctx, "    %s\n", cmd_help);
        ecli_output(ctx, "\n");
    }

    if (doc) {
        if (doc->long_desc) {
            ecli_output(ctx, "Description:\n");
            ecli_output(ctx, "    %s\n", doc->long_desc);
            ecli_output(ctx, "\n");
        }

        if (doc->examples) {
            ecli_output(ctx, "Examples:\n");
            /* Print each line with indentation */
            const char *p = doc->examples;
            while (*p) {
                const char *eol = strchr(p, '\n');
                if (eol) {
                    ecli_output(ctx, "    %.*s\n", (int)(eol - p), p);
                    p = eol + 1;
                } else {
                    ecli_output(ctx, "    %s\n", p);
                    break;
                }
            }
            ecli_output(ctx, "\n");
        }
    } else {
        ecli_output(ctx, "  (no extended documentation available)\n\n");
    }
}

/*
 * ecli_show_doc_file - Write documentation to a file
 */
void ecli_show_doc_file(eecli_ctx_t *cli, const char *cmd_name,
                       const char *filename, ecli_doc_fmt_t fmt)
{
    FILE *fp = fopen(filename, "w");
    if (!fp) {
        ecli_output(cli, "Error: cannot open file '%s': %s\n", filename, strerror(errno));
        return;
    }

    eecli_ctx_t *ctx = cli ? cli : g_ecli_ctx;
    const ecli_doc_entry_t *doc = ecli_doc_lookup(cmd_name);

    char *cmd_syntax = NULL;
    const char *cmd_help = NULL;

    if (ctx && ctx->grammar) {
        cmd_syntax = build_cmd_syntax(ctx->grammar, cmd_name);
        cmd_help = find_cmd_help(ctx->grammar, cmd_name);
    }

    switch (fmt) {
    case ECLI_DOC_FMT_MD:
        /* Markdown format */
        fprintf(fp, "# %s\n\n", cmd_name);
        if (cmd_syntax)
            fprintf(fp, "## Syntax\n\n```\n%s\n```\n\n", cmd_syntax);
        if (cmd_help)
            fprintf(fp, "## Summary\n\n%s\n\n", cmd_help);
        if (doc) {
            if (doc->long_desc)
                fprintf(fp, "## Description\n\n%s\n\n", doc->long_desc);
            if (doc->examples)
                fprintf(fp, "## Examples\n\n```\n%s```\n\n", doc->examples);
        }
        break;

    case ECLI_DOC_FMT_RST:
        /* reStructuredText format */
        fprintf(fp, "%s\n", cmd_name);
        for (size_t i = 0; i < strlen(cmd_name); i++)
            fputc('=', fp);
        fprintf(fp, "\n\n");
        if (cmd_syntax) {
            fprintf(fp, "Syntax\n------\n\n::\n\n    %s\n\n", cmd_syntax);
        }
        if (cmd_help) {
            fprintf(fp, "Summary\n-------\n\n%s\n\n", cmd_help);
        }
        if (doc) {
            if (doc->long_desc)
                fprintf(fp, "Description\n-----------\n\n%s\n\n", doc->long_desc);
            if (doc->examples)
                fprintf(fp, "Examples\n--------\n\n::\n\n    %s\n", doc->examples);
        }
        break;

    case ECLI_DOC_FMT_TXT:
        /* Plain text format */
        fprintf(fp, "%s\n", cmd_name);
        for (size_t i = 0; i < strlen(cmd_name); i++)
            fputc('-', fp);
        fprintf(fp, "\n\n");
        if (cmd_syntax)
            fprintf(fp, "SYNTAX:\n    %s\n\n", cmd_syntax);
        if (cmd_help)
            fprintf(fp, "SUMMARY:\n    %s\n\n", cmd_help);
        if (doc) {
            if (doc->long_desc)
                fprintf(fp, "DESCRIPTION:\n    %s\n\n", doc->long_desc);
            if (doc->examples)
                fprintf(fp, "EXAMPLES:\n    %s\n", doc->examples);
        }
        break;
    }

    free(cmd_syntax);
    fclose(fp);

    const char *fmt_name = (fmt == ECLI_DOC_FMT_MD) ? "Markdown" :
                           (fmt == ECLI_DOC_FMT_RST) ? "reStructuredText" : "plain text";
    ecli_output(cli, "Documentation written to '%s' (%s)\n", filename, fmt_name);
}
