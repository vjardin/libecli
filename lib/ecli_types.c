/*
 * CLI Common Type Parsing Functions
 *
 * Copyright (C) 2026 Free Mobile, Vincent Jardin <vjardin@free.fr>
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Implementation of parsing helpers for common CLI types.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "ecli_types.h"

/*
 * Parse IPv4 address string to uint32_t (network byte order)
 *
 * @param str   IPv4 address string (e.g., "192.168.1.1")
 * @param addr  Output address in network byte order
 * @return 0 on success, -1 on error
 */
int ecli_parse_ipv4(const char *str, uint32_t *addr)
{
    struct in_addr in;

    if (!str || !addr)
        return -1;

    if (inet_pton(AF_INET, str, &in) != 1)
        return -1;

    *addr = in.s_addr;
    return 0;
}

/*
 * Parse IPv4 prefix string to address and prefix length
 *
 * @param str        IPv4 prefix string (e.g., "192.168.1.0/24")
 * @param addr       Output address in network byte order
 * @param prefix_len Output prefix length (0-32)
 * @return 0 on success, -1 on error
 */
int ecli_parse_ipv4_prefix(const char *str, uint32_t *addr, int *prefix_len)
{
    char buf[64];
    char *slash;
    long plen;

    if (!str || !addr || !prefix_len)
        return -1;

    /* Copy to mutable buffer */
    snprintf(buf, sizeof(buf), "%s", str);

    /* Find the slash */
    slash = strchr(buf, '/');
    if (!slash)
        return -1;

    *slash = '\0';

    /* Parse address */
    if (ecli_parse_ipv4(buf, addr) < 0)
        return -1;

    /* Parse prefix length */
    plen = strtol(slash + 1, NULL, 10);
    if (plen < 0 || plen > 32)
        return -1;

    *prefix_len = (int)plen;
    return 0;
}

/*
 * Parse IPv6 address string to in6_addr
 *
 * @param str  IPv6 address string (e.g., "2001:db8::1")
 * @param addr Output address structure
 * @return 0 on success, -1 on error
 */
int ecli_parse_ipv6(const char *str, struct in6_addr *addr)
{
    if (!str || !addr)
        return -1;

    if (inet_pton(AF_INET6, str, addr) != 1)
        return -1;

    return 0;
}

/*
 * Parse MAC address string to 6-byte array
 *
 * Accepts formats:
 *   - aa:bb:cc:dd:ee:ff (colon-separated)
 *   - aa-bb-cc-dd-ee-ff (dash-separated)
 *
 * @param str  MAC address string
 * @param mac  Output 6-byte array
 * @return 0 on success, -1 on error
 */
int ecli_parse_mac(const char *str, uint8_t mac[6])
{
    unsigned int bytes[6];
    int ret;

    if (!str || !mac)
        return -1;

    /* Try colon-separated format */
    ret = sscanf(str, "%x:%x:%x:%x:%x:%x",
                 &bytes[0], &bytes[1], &bytes[2],
                 &bytes[3], &bytes[4], &bytes[5]);
    if (ret != 6) {
        /* Try dash-separated format */
        ret = sscanf(str, "%x-%x-%x-%x-%x-%x",
                     &bytes[0], &bytes[1], &bytes[2],
                     &bytes[3], &bytes[4], &bytes[5]);
    }

    if (ret != 6)
        return -1;

    /* Validate range */
    for (int i = 0; i < 6; i++) {
        if (bytes[i] > 255)
            return -1;
        mac[i] = (uint8_t)bytes[i];
    }

    return 0;
}

/*
 * Parse boolean string
 *
 * Accepts: on/off, enable/disable, yes/no, true/false, 1/0
 *
 * @param str   Boolean string
 * @param value Output boolean value
 * @return 0 on success, -1 on error
 */
int ecli_parse_bool(const char *str, bool *value)
{
    if (!str || !value)
        return -1;

    if (strcasecmp(str, "on") == 0 ||
        strcasecmp(str, "enable") == 0 ||
        strcasecmp(str, "yes") == 0 ||
        strcasecmp(str, "true") == 0 ||
        strcmp(str, "1") == 0) {
        *value = true;
        return 0;
    }

    if (strcasecmp(str, "off") == 0 ||
        strcasecmp(str, "disable") == 0 ||
        strcasecmp(str, "no") == 0 ||
        strcasecmp(str, "false") == 0 ||
        strcmp(str, "0") == 0) {
        *value = false;
        return 0;
    }

    return -1;
}

/*
 * Format IPv4 address to string
 *
 * Uses static buffer - not thread-safe for multiple concurrent calls.
 *
 * @param addr IPv4 address in network byte order
 * @return Pointer to static string buffer
 */
const char *ecli_fmt_ipv4(uint32_t addr)
{
    static char buf[INET_ADDRSTRLEN];
    struct in_addr in;

    in.s_addr = addr;
    inet_ntop(AF_INET, &in, buf, sizeof(buf));

    return buf;
}

/*
 * Format MAC address to string
 *
 * Uses static buffer - not thread-safe for multiple concurrent calls.
 *
 * @param mac 6-byte MAC address
 * @return Pointer to static string buffer
 */
const char *ecli_fmt_mac(const uint8_t mac[6])
{
    static char buf[18];

    snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    return buf;
}
