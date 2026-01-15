/*
 * Minimal ECLI Example
 *
 * Copyright (C) 2026 Free Mobile, Vincent Jardin <vjardin@free.fr>
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Demonstrates basic usage of the ECLI framework:
 * - Simple commands (quit, help)
 * - Command groups (show, set)
 * - Arguments with validation
 * - Configuration output (write terminal)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include <event2/event.h>
#include <ecoli.h>

#include "ecli.h"
#include "ecli_cmd.h"
#include "ecli_types.h"

/* Required: Initialize CLI command context */
ECLI_CMD_CTX()

/* Application state */
static volatile bool g_running = true;
static char g_name[64] = "world";
static char g_address[32] = "";  /* IPv4 address */

/* Event base for signal handling */
static struct event_base *g_event_base = NULL;
static struct event *g_sigint_event = NULL;
static struct event *g_sigterm_event = NULL;

/*
 * Use the library's 'show' group to add application-specific subcommands.
 * The group is defined in ecli_builtin.c with ECLI_EXPORT_GROUP.
 */
ECLI_USE_GROUP(show);

/* Subcommand: show name */
ECLI_DEFUN_SUB0(show, name, "show_name", "name", "display current name")
{
    ecli_output(cli, "Name: %s\n", g_name);
    return 0;
}

/* Command group: set */
ECLI_DEFUN_GROUP(set, "set", "configure settings")

/* Config command: set name (with write terminal support) */
ECLI_DEFUN_SET(set, name, "set_name",
    "name value",
    "set the greeting name",
    "set name {value}\n",
    "greeting",
    10,
    ECLI_ARG_NAME("value", "name to greet"))
{
    const char *value = ecli_arg_str(parse, "value");
    if (value) {
        snprintf(g_name, sizeof(g_name), "%s", value);
        ecli_output(cli, "Name set to '%s'\n", g_name);
    }
    return 0;
}

/* Output function for set name */
ECLI_DEFUN_OUT(set, name)
{
    if (strcmp(g_name, "world") != 0) {
        ECLI_OUT_FMT(cli, fp, fmt,
            "value", FMT_STR, g_name,
            NULL);
    }
}

/* Subcommand: show address */
ECLI_DEFUN_SUB0(show, address, "show_address", "address", "display configured IPv4 address")
{
    if (g_address[0]) {
        ecli_output(cli, "Address: %s\n", g_address);
    } else {
        ecli_output(cli, "Address: not configured\n");
    }
    return 0;
}

/* Config command: set address (with write terminal support) */
ECLI_DEFUN_SET(set, address, "set_address",
    "address ipv4",
    "set the IPv4 address",
    "set address {ipv4}\n",
    "network",
    20,
    ECLI_ARG_IPV4("ipv4", "IPv4 address (e.g., 192.168.1.1)"))
{
    const char *value = ecli_arg_str(parse, "ipv4");
    if (value) {
        snprintf(g_address, sizeof(g_address), "%s", value);
        ecli_output(cli, "Address set to '%s'\n", g_address);
    }
    return 0;
}

/* Output function for set address */
ECLI_DEFUN_OUT(set, address)
{
    if (g_address[0]) {
        ECLI_OUT_FMT(cli, fp, fmt,
            "ipv4", FMT_STR, g_address,
            NULL);
    }
}

/* Command group: del */
ECLI_DEFUN_GROUP(del, "del", "delete configuration")

/* Delete command: del address <ipv4> */
ECLI_DEFUN_SUB(del, address, "del_address",
    "address ipv4",
    "delete the IPv4 address",
    ECLI_ARG_IPV4("ipv4", "IPv4 address to delete"))
{
    const char *value = ecli_arg_str(parse, "ipv4");
    if (!value) {
        ecli_output(cli, "Usage: del address <ipv4>\n");
        return 0;
    }
    if (g_address[0] && strcmp(g_address, value) == 0) {
        ecli_output(cli, "Address '%s' deleted\n", g_address);
        g_address[0] = '\0';
    } else if (g_address[0]) {
        ecli_output(cli, "Address '%s' not found (configured: %s)\n", value, g_address);
    } else {
        ecli_output(cli, "No address configured\n");
    }
    return 0;
}

/* Simple command: hello */
ECLI_DEFUN(hello, "hello", "hello", "say hello")
{
    ecli_output(cli, "Hello, %s!\n", g_name);
    return 0;
}

/* Libevent signal callback */
static void signal_cb(evutil_socket_t fd, short events, void *arg)
{
    (void)fd;
    (void)events;
    (void)arg;

    fprintf(stderr, "\nSignal received, shutting down...\n");
    g_running = false;
    ecli_request_exit();
    event_base_loopbreak(g_event_base);
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    /* Note: ec_init() is called internally by ecli_init() */

    /* Create event base for signal handling */
    g_event_base = event_base_new();
    if (!g_event_base) {
        fprintf(stderr, "Failed to create event base\n");
        return 1;
    }

    /* Setup signal handlers */
    g_sigint_event = evsignal_new(g_event_base, SIGINT, signal_cb, NULL);
    g_sigterm_event = evsignal_new(g_event_base, SIGTERM, signal_cb, NULL);
    if (!g_sigint_event || !g_sigterm_event) {
        fprintf(stderr, "Failed to create signal events\n");
        event_base_free(g_event_base);
        return 1;
    }
    event_add(g_sigint_event, NULL);
    event_add(g_sigterm_event, NULL);

    /* Initialize CLI in foreground mode */
    ecli_config_t config = {
        .prompt = "minimal> ",
        .banner = "ECLI Minimal Example",
        .version = "1.0.0",
    };

    if (ecli_init(&config) < 0) {
        fprintf(stderr, "Failed to initialize CLI\n");
        event_free(g_sigint_event);
        event_free(g_sigterm_event);
        event_base_free(g_event_base);
        return 1;
    }

    printf("ECLI Minimal Example\n");
    printf("Type 'help' for available commands, 'quit' to exit.\n\n");

    /* Run CLI event loop */
    ecli_run(&g_running);

    /* Cleanup */
    ecli_shutdown();
    event_free(g_sigint_event);
    event_free(g_sigterm_event);
    event_base_free(g_event_base);

    return 0;
}
