/*
 * Queue Extensions for sys/queue.h
 *
 * Copyright (C) 2026 Free Mobile, Vincent Jardin <vjardin@free.fr>
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file extends the standard sys/queue.h with safe iteration macros
 * that allow element removal during iteration.
 */

#pragma once

#include <sys/queue.h>

/*
 * TAILQ_FOREACH_SAFE - safely iterate over a tail queue, allowing removal
 *
 * @var:    the loop variable (pointer to element type)
 * @head:   pointer to the queue head
 * @field:  name of the TAILQ_ENTRY field in the element structure
 * @tvar:   temporary variable for safe iteration
 */
#ifndef TAILQ_FOREACH_SAFE
#define TAILQ_FOREACH_SAFE(var, head, field, tvar)                      \
    for ((var) = TAILQ_FIRST((head));                                   \
         (var) && ((tvar) = TAILQ_NEXT((var), field), 1);               \
         (var) = (tvar))
#endif

/*
 * SLIST_FOREACH_SAFE - safely iterate over a singly-linked list
 */
#ifndef SLIST_FOREACH_SAFE
#define SLIST_FOREACH_SAFE(var, head, field, tvar)                      \
    for ((var) = SLIST_FIRST((head));                                   \
         (var) && ((tvar) = SLIST_NEXT((var), field), 1);               \
         (var) = (tvar))
#endif

/*
 * STAILQ_FOREACH_SAFE - safely iterate over a singly-linked tail queue
 */
#ifndef STAILQ_FOREACH_SAFE
#define STAILQ_FOREACH_SAFE(var, head, field, tvar)                     \
    for ((var) = STAILQ_FIRST((head));                                  \
         (var) && ((tvar) = STAILQ_NEXT((var), field), 1);              \
         (var) = (tvar))
#endif

/*
 * LIST_FOREACH_SAFE - safely iterate over a doubly-linked list
 */
#ifndef LIST_FOREACH_SAFE
#define LIST_FOREACH_SAFE(var, head, field, tvar)                       \
    for ((var) = LIST_FIRST((head));                                    \
         (var) && ((tvar) = LIST_NEXT((var), field), 1);                \
         (var) = (tvar))
#endif
