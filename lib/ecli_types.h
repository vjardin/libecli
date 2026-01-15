/*
 * CLI Common Type Definitions
 *
 * Copyright (C) 2026 Free Mobile, Vincent Jardin <vjardin@free.fr>
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This header provides reusable libecoli node macros for typical CLI argument
 * types. These macros create properly configured ec_node objects with help
 * strings and validation patterns.
 *
 * QUICK REFERENCE
 *
 * IDENTIFIERS & STRINGS:
 *   ECLI_ARG_NAME(id, help)      - Generic identifier (e.g., "api-server", "my-vlan")
 *   ECLI_ARG_HOSTNAME(id, help)  - RFC 1123 hostname (e.g., "server01", "web-01")
 *   ECLI_ARG_IFNAME(id, help)    - Interface name (e.g., "eth0", "bond0.100")
 *   ECLI_ARG_FILENAME(id, help)  - Filename without spaces
 *   ECLI_ARG_PATH(id, help)      - File path (absolute or relative)
 *
 * NETWORK ADDRESSES:
 *   ECLI_ARG_IPV4(id, help)        - IPv4 address (e.g., "192.168.1.1")
 *   ECLI_ARG_IPV4_PREFIX(id, help) - IPv4 CIDR (e.g., "10.0.0.0/8")
 *   ECLI_ARG_IPV6(id, help)        - IPv6 address (e.g., "2001:db8::1")
 *   ECLI_ARG_IPV6_PREFIX(id, help) - IPv6 CIDR (e.g., "2001:db8::/32")
 *   ECLI_ARG_MAC(id, help)         - MAC address (e.g., "aa:bb:cc:dd:ee:ff")
 *   ECLI_ARG_MAC_ANY(id, help)     - MAC with any separator (: or -)
 *
 * INTEGERS:
 *   ECLI_ARG_UINT(id, max, help)      - Unsigned integer 0..max
 *   ECLI_ARG_INT(id, min, max, help)  - Signed integer min..max
 *   ECLI_ARG_COUNT(id, max, help)     - Count 1..max
 *   ECLI_ARG_PORT(id, help)           - TCP/UDP port 1..65535
 *   ECLI_ARG_PORT_ANY(id, help)       - TCP/UDP port 0..65535
 *   ECLI_ARG_PORT_COUNT(id, help)     - Port count 1..256
 *   ECLI_ARG_VLAN(id, help)           - VLAN ID 1..4094
 *   ECLI_ARG_VLAN_ANY(id, help)       - VLAN ID 0..4095
 *   ECLI_ARG_PRIORITY(id, help)       - 802.1p priority 0..7
 *   ECLI_ARG_DSCP(id, help)           - DSCP value 0..63
 *   ECLI_ARG_MTU(id, help)            - MTU 64..65535
 *   ECLI_ARG_PERCENT(id, help)        - Percentage 0..100
 *   ECLI_ARG_TIMEOUT(id, max, help)   - Timeout in seconds 1..max
 *   ECLI_ARG_INDEX(id, max, help)     - Zero-based index 0..max
 *   ECLI_ARG_SLOT(id, max, help)      - One-based slot 1..max
 *
 * BOOLEAN/CHOICE:
 *   ECLI_ARG_ONOFF(id, help)   - "on" or "off"
 *   ECLI_ARG_ENABLE(id, help)  - "enable" or "disable"
 *   ECLI_ARG_YESNO(id, help)   - "yes" or "no"
 *   ECLI_ARG_BOOL(id, help)    - "true" or "false"
 *
 * SPECIAL:
 *   ECLI_ARG_HEX(id, help)            - Hexadecimal (e.g., "0x1a2b", "ff")
 *   ECLI_ARG_ANY(id, help)            - Any single token
 *   ECLI_ARG_REGEX(id, pattern, help) - Custom regex pattern
 *
 * USAGE EXAMPLES
 *
 * Example 1: Simple command with a name argument
 * -----------------------------------------------
 *   ECLI_DEFUN_SUB(vlan_cmd, add, "vlan_add",
 *       "add name",
 *       "create a new VLAN",
 *       ECLI_ARG_NAME("name", "VLAN name (e.g., management, guest)"))
 *   {
 *       const char *name = ec_pnode_get_str(parse, "name");
 *       printf("Creating VLAN: %s\n", name);
 *       return 0;
 *   }
 *   // Usage: vlan add management
 *   // Usage: vlan add guest-wifi
 *
 * Example 2: Command with integer range
 * --------------------------------------
 *   ECLI_DEFUN_SUB(vlan_cmd, set_id, "vlan_set_id",
 *       "set name id vlan_id",
 *       "assign VLAN ID to a named VLAN",
 *       ECLI_ARG_NAME("name", "VLAN name"),
 *       ECLI_ARG_VLAN("vlan_id", "VLAN ID (1-4094)"))
 *   {
 *       const char *name = ec_pnode_get_str(parse, "name");
 *       int64_t id = ec_pnode_get_int(parse, "vlan_id");
 *       printf("Setting %s to VLAN %ld\n", name, id);
 *       return 0;
 *   }
 *   // Usage: vlan set management id 100
 *   // Usage: vlan set guest id 200
 *
 * Example 3: Network configuration with IP address
 * -------------------------------------------------
 *   ECLI_DEFUN_SUB(iface_cmd, ip, "interface_ip",
 *       "ip address prefix",
 *       "configure interface IP address",
 *       ECLI_ARG_IPV4_PREFIX("prefix", "IP address with prefix (e.g., 192.168.1.1/24)"))
 *   {
 *       const char *prefix_str = ec_pnode_get_str(parse, "prefix");
 *       uint32_t addr;
 *       int prefix_len;
 *       if (ecli_parse_ipv4_prefix(prefix_str, &addr, &prefix_len) == 0) {
 *           printf("Configured: %s (prefix length: %d)\n", prefix_str, prefix_len);
 *       }
 *       return 0;
 *   }
 *   // Usage: ip address 192.168.1.1/24
 *   // Usage: ip address 10.0.0.1/8
 *
 * Example 4: Boolean toggle
 * --------------------------
 *   ECLI_DEFUN_SUB(port_cmd, admin, "port_admin",
 *       "admin state",
 *       "set port administrative state",
 *       ECLI_ARG_ENABLE("state", "enable or disable the port"))
 *   {
 *       const char *state = ec_pnode_get_str(parse, "state");
 *       bool enabled;
 *       ecli_parse_bool(state, &enabled);
 *       printf("Port %s\n", enabled ? "enabled" : "disabled");
 *       return 0;
 *   }
 *   // Usage: admin enable
 *   // Usage: admin disable
 *
 * Example 5: MAC address binding
 * -------------------------------
 *   ECLI_DEFUN_SUB(arp_cmd, static_entry, "arp_static",
 *       "static ip mac",
 *       "add static ARP entry",
 *       ECLI_ARG_IPV4("ip", "IP address"),
 *       ECLI_ARG_MAC("mac", "MAC address (aa:bb:cc:dd:ee:ff)"))
 *   {
 *       const char *ip_str = ec_pnode_get_str(parse, "ip");
 *       const char *mac_str = ec_pnode_get_str(parse, "mac");
 *       uint32_t ip;
 *       uint8_t mac[6];
 *       ecli_parse_ipv4(ip_str, &ip);
 *       ecli_parse_mac(mac_str, mac);
 *       printf("ARP: %s -> %s\n", ip_str, mac_str);
 *       return 0;
 *   }
 *   // Usage: static 192.168.1.1 aa:bb:cc:dd:ee:ff
 *
 * Example 6: QoS configuration with multiple parameters
 * -------------------------------------------------------
 *   ECLI_DEFUN_SUB(qos_cmd, policy, "qos_policy",
 *       "policy name dscp dscp_val priority prio_val",
 *       "create QoS policy with DSCP and priority",
 *       ECLI_ARG_NAME("name", "policy name"),
 *       ECLI_ARG_DSCP("dscp_val", "DSCP value (0-63)"),
 *       ECLI_ARG_PRIORITY("prio_val", "802.1p priority (0-7)"))
 *   {
 *       const char *name = ec_pnode_get_str(parse, "name");
 *       int64_t dscp = ec_pnode_get_int(parse, "dscp_val");
 *       int64_t prio = ec_pnode_get_int(parse, "prio_val");
 *       printf("Policy %s: DSCP=%ld, Priority=%ld\n", name, dscp, prio);
 *       return 0;
 *   }
 *   // Usage: policy voice dscp 46 priority 5
 *   // Usage: policy bulk dscp 0 priority 1
 *
 * Example 7: Custom regex pattern
 * --------------------------------
 *   // Match a version string like "v1.2.3" or "1.2.3"
 *   #define RE_VERSION "v?[0-9]+\\.[0-9]+\\.[0-9]+"
 *
 *   ECLI_DEFUN_SUB(sys_cmd, upgrade, "system_upgrade",
 *       "upgrade version",
 *       "upgrade system firmware",
 *       ECLI_ARG_REGEX("version", RE_VERSION, "version (e.g., v1.2.3)"))
 *   {
 *       const char *ver = ec_pnode_get_str(parse, "version");
 *       printf("Upgrading to version: %s\n", ver);
 *       return 0;
 *   }
 *   // Usage: upgrade v1.2.3
 *   // Usage: upgrade 2.0.0
 *
 * Example 8: Port range with timeout
 * ------------------------------------
 *   ECLI_DEFUN_SUB(scan_cmd, ports, "scan_ports",
 *       "ports start end timeout secs",
 *       "scan port range with timeout",
 *       ECLI_ARG_PORT("start", "starting port number"),
 *       ECLI_ARG_PORT("end", "ending port number"),
 *       ECLI_ARG_TIMEOUT("secs", 300, "timeout in seconds (max 300)"))
 *   {
 *       int64_t start = ec_pnode_get_int(parse, "start");
 *       int64_t end = ec_pnode_get_int(parse, "end");
 *       int64_t timeout = ec_pnode_get_int(parse, "secs");
 *       printf("Scanning ports %ld-%ld (timeout: %lds)\n", start, end, timeout);
 *       return 0;
 *   }
 *   // Usage: ports 1 1024 timeout 60
 *   // Usage: ports 80 443 timeout 10
 *
 * EXTRACTING VALUES IN HANDLERS
 *
 * Use these libecoli functions to extract argument values:
 *
 *   const char *str = ec_pnode_get_str(parse, "id");   // Get string value
 *   int64_t num = ec_pnode_get_int(parse, "id");       // Get integer value
 *
 * The "id" parameter must match the id you passed to the ECLI_ARG_* macro.
 *
 * For complex parsing (IP addresses, MACs, etc.), use the helper functions:
 *
 *   uint32_t ip;
 *   ecli_parse_ipv4(ec_pnode_get_str(parse, "ip"), &ip);
 *
 *   uint8_t mac[6];
 *   ecli_parse_mac(ec_pnode_get_str(parse, "mac"), mac);
 *
 *   bool enabled;
 *   ecli_parse_bool(ec_pnode_get_str(parse, "state"), &enabled);
 *

 */
#pragma once

#include <ecoli.h>
#include "ecli_cmd.h"

#define ECLI_RE_NAME         "[a-zA-Z][a-zA-Z0-9_-]*"

#define ECLI_RE_HOSTNAME     "[a-zA-Z0-9]([a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?"

#define ECLI_RE_IFNAME       "[a-zA-Z][a-zA-Z0-9_.-]*"

#define ECLI_RE_FILENAME     "[^ ]+"

#define ECLI_RE_PATH         "[^ ]+"

#define ECLI_RE_IPV4         "[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}"

#define ECLI_RE_IPV4_PREFIX  "[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}/[0-9]{1,2}"

#define ECLI_RE_IPV6         "[0-9a-fA-F:.]+"

#define ECLI_RE_IPV6_PREFIX  "[0-9a-fA-F:.]+/[0-9]{1,3}"

#define ECLI_RE_MAC          "[0-9a-fA-F]{1,2}(:[0-9a-fA-F]{1,2}){5}"

#define ECLI_RE_MAC_DASH     "[0-9a-fA-F]{1,2}(-[0-9a-fA-F]{1,2}){5}"

#define ECLI_RE_MAC_ANY      "[0-9a-fA-F]{1,2}([-:][0-9a-fA-F]{1,2}){5}"

#define ECLI_RE_HEX          "(0[xX])?[0-9a-fA-F]+"

#define ECLI_RE_DECIMAL      "[0-9]+"

#define ECLI_ARG_NAME(id, help) \
    _H((help), ec_node_re((id), ECLI_RE_NAME))

#define ECLI_ARG_HOSTNAME(id, help) \
    _H((help), ec_node_re((id), ECLI_RE_HOSTNAME))

#define ECLI_ARG_IFNAME(id, help) \
    _H((help), ec_node_re((id), ECLI_RE_IFNAME))

#define ECLI_ARG_FILENAME(id, help) \
    _H((help), ec_node_re((id), ECLI_RE_FILENAME))

#define ECLI_ARG_PATH(id, help) \
    _H((help), ec_node_re((id), ECLI_RE_PATH))

#define ECLI_ARG_IPV4(id, help) \
    _H((help), ec_node_re((id), ECLI_RE_IPV4))

#define ECLI_ARG_IPV4_PREFIX(id, help) \
    _H((help), ec_node_re((id), ECLI_RE_IPV4_PREFIX))

#define ECLI_ARG_IPV6(id, help) \
    _H((help), ec_node_re((id), ECLI_RE_IPV6))

#define ECLI_ARG_IPV6_PREFIX(id, help) \
    _H((help), ec_node_re((id), ECLI_RE_IPV6_PREFIX))

#define ECLI_ARG_MAC(id, help) \
    _H((help), ec_node_re((id), ECLI_RE_MAC))

#define ECLI_ARG_MAC_ANY(id, help) \
    _H((help), ec_node_re((id), ECLI_RE_MAC_ANY))

#define ECLI_ARG_UINT(id, max, help) \
    _H((help), ec_node_int((id), 0, (max), 10))

#define ECLI_ARG_INT(id, min, max, help) \
    _H((help), ec_node_int((id), (min), (max), 10))

#define ECLI_ARG_COUNT(id, max, help) \
    _H((help), ec_node_int((id), 1, (max), 10))

#define ECLI_ARG_PORT_COUNT(id, help) \
    _H((help), ec_node_int((id), 1, 256, 10))

#define ECLI_ARG_PORT(id, help) \
    _H((help), ec_node_int((id), 1, 65535, 10))

#define ECLI_ARG_PORT_ANY(id, help) \
    _H((help), ec_node_int((id), 0, 65535, 10))

#define ECLI_ARG_VLAN(id, help) \
    _H((help), ec_node_int((id), 1, 4094, 10))

#define ECLI_ARG_VLAN_ANY(id, help) \
    _H((help), ec_node_int((id), 0, 4095, 10))

#define ECLI_ARG_PRIORITY(id, help) \
    _H((help), ec_node_int((id), 0, 7, 10))

#define ECLI_ARG_DSCP(id, help) \
    _H((help), ec_node_int((id), 0, 63, 10))

#define ECLI_ARG_MTU(id, help) \
    _H((help), ec_node_int((id), 64, 65535, 10))

#define ECLI_ARG_PERCENT(id, help) \
    _H((help), ec_node_int((id), 0, 100, 10))

#define ECLI_ARG_TIMEOUT(id, max, help) \
    _H((help), ec_node_int((id), 1, (max), 10))

#define ECLI_ARG_INDEX(id, max, help) \
    _H((help), ec_node_int((id), 0, (max), 10))

#define ECLI_ARG_SLOT(id, max, help) \
    _H((help), ec_node_int((id), 1, (max), 10))

#define ECLI_ARG_ONOFF(id, help) \
    _H((help), EC_NODE_OR((id), \
        ec_node_str(EC_NO_ID, "on"), \
        ec_node_str(EC_NO_ID, "off")))

#define ECLI_ARG_ENABLE(id, help) \
    _H((help), EC_NODE_OR((id), \
        ec_node_str(EC_NO_ID, "enable"), \
        ec_node_str(EC_NO_ID, "disable")))

#define ECLI_ARG_YESNO(id, help) \
    _H((help), EC_NODE_OR((id), \
        ec_node_str(EC_NO_ID, "yes"), \
        ec_node_str(EC_NO_ID, "no")))

#define ECLI_ARG_BOOL(id, help) \
    _H((help), EC_NODE_OR((id), \
        ec_node_str(EC_NO_ID, "true"), \
        ec_node_str(EC_NO_ID, "false")))

#define ECLI_ARG_HEX(id, help) \
    _H((help), ec_node_re((id), ECLI_RE_HEX))

#define ECLI_ARG_ANY(id, help) \
    _H((help), ec_node_any((id), EC_NO_ID))

#define ECLI_ARG_REGEX(id, pattern, help) \
    _H((help), ec_node_re((id), (pattern)))

/* Documentation format choice (md, rst, txt) */
#define ECLI_ARG_DOC_FMT(id, help) \
    _H((help), EC_NODE_OR((id), \
        ec_node_str(EC_NO_ID, "md"), \
        ec_node_str(EC_NO_ID, "rst"), \
        ec_node_str(EC_NO_ID, "txt")))

int ecli_parse_ipv4(const char *str, uint32_t *addr);

int ecli_parse_ipv4_prefix(const char *str, uint32_t *addr, int *prefix_len);

struct in6_addr;
int ecli_parse_ipv6(const char *str, struct in6_addr *addr);

int ecli_parse_mac(const char *str, uint8_t mac[6]);

int ecli_parse_bool(const char *str, bool *value);

const char *ecli_fmt_ipv4(uint32_t addr);

const char *ecli_fmt_mac(const uint8_t mac[6]);
