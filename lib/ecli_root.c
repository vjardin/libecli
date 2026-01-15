/*
 * ECLI Root Node Management
 *
 * Copyright (C) 2026 Free Mobile, Vincent Jardin <vjardin@free.fr>
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file provides the default CLI root node initialization.
 * It implements what ECLI_CMD_CTX() macro would generate.
 */

#include <ecoli.h>
#include "ecli_cmd.h"

/* Global root node for CLI grammar */
struct ec_node *__cli_root = NULL;

/* Finalized commands (wrapped with sh_lex tokenizer) */
static struct ec_node *__cli_commands = NULL;

/*
 * Initialize root "or" node
 * Priority 110 - runs early
 */
static int _cli_cmd_init_root(void)
{
    __cli_root = ec_node("or", EC_NO_ID);
    return __cli_root ? 0 : -1;
}

static struct ec_init _cli_init_root = {
    .init = _cli_cmd_init_root,
    .exit = NULL,
    .priority = 110
};
EC_INIT_REGISTER(_cli_init_root);

/*
 * Finalize grammar by wrapping with sh_lex tokenizer
 * Priority 190 - runs after all commands are registered
 */
static int _cli_cmd_finalize(void)
{
    if (__cli_root == NULL)
        return -1;
    __cli_commands = ec_node_sh_lex(EC_NO_ID, __cli_root);
    return __cli_commands ? 0 : -1;
}

static struct ec_init _cli_finit = {
    .init = _cli_cmd_finalize,
    .exit = NULL,
    .priority = 190
};
EC_INIT_REGISTER(_cli_finit);

/*
 * Get the finalized command grammar
 */
struct ec_node *ecli_cmd_get_commands(void)
{
    return __cli_commands;
}
