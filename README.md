# libecli

A CLI framework based on libecoli providing command definition macros, tab completion, and
configuration management.


## Overview

libecli is a library that simplifies the creation of interactive command-line interfaces. It builds
on top of libecoli to provide automatic tab completion, contextual help, and a familiar DEFUN-style
macro system.

The library supports both interactive foreground mode with readline-like editing and a TCP daemon
mode where clients can connect via netcat or telnet. Commands can be organized into groups and
subcommands, with automatic help generation and argument validation.

Configuration management is built-in through the "write terminal" and "write file" commands, which
output the current configuration as executable CLI commands. This enables configuration backup,
restore, and transfer between instances.

YAML grammar import/export allows runtime CLI translation without recompilation, making it possible
to localize the CLI to different languages or customize command syntax for specific deployments.


## Requirements

The library requires libecoli for CLI parsing and completion, libevent for event-driven I/O in TCP
mode, and libyaml for YAML grammar processing. A C11-compliant compiler and meson version 0.56.0 or
later are needed for building.


## Building

Configure the build directory with meson. By default, a shared library is built:

```
meson setup build
```

To build a static library instead, use the default_library option:

```
meson setup build -Ddefault_library=static
```

Compile the library and examples with ninja:

```
ninja -C build
```

Optionally install the library system-wide:

```
sudo ninja -C build install
```


## Examples

The examples directory contains sample applications demonstrating libecli usage. The minimal example
shows basic command definition, groups, subcommands, and configuration output.

To compile the examples, they are built automatically when you run ninja. After building, you can
find the minimal example at build/examples/minimal/ecli-minimal.

To run the minimal example, you need to set LD_LIBRARY_PATH if libecli is not installed system-wide:

```
LD_LIBRARY_PATH=build ./build/examples/minimal/ecli-minimal
```

The minimal example provides several commands to demonstrate the framework. Type "help" to see
available commands, use Tab for completion, and try "set name yourname" followed by "hello" to see
argument handling. The "write terminal" command shows how configuration output works.

Example session:

```
$ LD_LIBRARY_PATH=build ./build/examples/minimal/ecli-minimal
ECLI Minimal Example
Type 'help' for available commands, 'quit' to exit.

minimal> help
Commands:
  hello - say hello
  quit - exit the application
  set name value - set the greeting name
  show name - display current name
  show version - display version
  write terminal - display config to terminal
  ...

minimal> set name Alice
Name set to 'Alice'

minimal> hello
Hello, Alice!

minimal> write terminal
! greeting
set name Alice

minimal> quit
Goodbye!
```


## Quick Start

Here is a minimal application that defines two commands:

```c
#include <stdio.h>
#include <ecoli.h>
#include <event2/event.h>

#include "ecli.h"
#include "ecli_cmd.h"
#include "ecli_types.h"

static volatile bool g_running = true;

ECLI_DEFUN(hello, "hello", "hello", "say hello")
{
    ecli_output(cli, "Hello, World!\n");
    return 0;
}

ECLI_DEFUN(greet, "greet", "greet name", "greet someone",
    ECLI_ARG_NAME("name", "person to greet"))
{
    const char *name = ecli_arg_str(parse, "name");
    ecli_output(cli, "Hello, %s!\n", name);
    return 0;
}

int main(void)
{
    ec_init();

    ecli_config_t config = { .prompt = "app> " };
    ecli_init(&config);
    ecli_run(&g_running);
    ecli_shutdown();

    return 0;
}
```

Build with pkg-config after installing libecli:

```
cc -o myapp myapp.c $(pkg-config --cflags --libs libecli)
```


## Command Definition Macros

ECLI_DEFUN defines a top-level command. The parameters are the function name, a callback identifier
for YAML export, the command syntax string, the help text, and optional argument definitions.

ECLI_DEFUN_ALIAS creates an alias for an existing command, such as "?" for "help" or "exit" for
"quit". Aliases share the same callback function as their target.

ECLI_DEFUN_GROUP defines a command group like "show" or "set". Groups organize related commands
under a common prefix and appear as a single entry in the top-level help.

ECLI_DEFUN_SUB defines a subcommand within a group. For example, "show version" or "set hostname"
are subcommands of their respective groups.

ECLI_DEFUN_SET defines a configuration command that supports "write terminal" output. In addition
to the standard parameters, it takes a format string with placeholders, a group name for output
organization, and a priority for ordering.

ECLI_DEFUN_OUT defines the output function for a DEFUN_SET command. This function is called by
"write terminal" and "write file" to emit the CLI commands that would recreate the current state.


## Argument Types

The library provides macros for common argument types with built-in validation.

ECLI_ARG_NAME matches generic identifiers like "server1" or "my-config". ECLI_ARG_HOSTNAME matches
RFC 1123 hostnames. ECLI_ARG_IFNAME matches interface names like "eth0" or "bond0.100".
ECLI_ARG_FILENAME and ECLI_ARG_PATH match file names and paths respectively.

For network addresses, ECLI_ARG_IPV4 matches IPv4 addresses, ECLI_ARG_IPV4_PREFIX matches CIDR
notation like "192.168.1.0/24", ECLI_ARG_IPV6 matches IPv6 addresses, and ECLI_ARG_MAC matches
MAC addresses in colon-separated format.

Integer arguments include ECLI_ARG_UINT for unsigned integers with a maximum value, ECLI_ARG_INT
for signed integers with min and max bounds, ECLI_ARG_PORT for TCP/UDP ports 1-65535,
ECLI_ARG_VLAN for VLAN IDs 1-4094, and ECLI_ARG_TIMEOUT for timeout values in seconds.

Boolean arguments can be defined with ECLI_ARG_ONOFF for "on"/"off", ECLI_ARG_ENABLE for
"enable"/"disable", or ECLI_ARG_BOOL for "true"/"false".


## Configuration Output

Commands defined with ECLI_DEFUN_SET automatically support configuration output. The format string
uses named placeholders like {value} that are substituted at runtime. This allows translations to
reorder parameters while keeping the same output function.

```c
static int g_max_connections = 1000;

ECLI_DEFUN_SET(set, max_conn, "set_max_connections",
    "max-connections value",
    "set maximum connections",
    "set max-connections {value}\n",
    "server", 10,
    ECLI_ARG_UINT("value", 10000, "max connections"))
{
    g_max_connections = (int)ecli_arg_int(parse, "value", 1000);
    ecli_output(cli, "Max connections set to %d\n", g_max_connections);
    return 0;
}

ECLI_DEFUN_OUT(set, max_conn)
{
    if (g_max_connections != 1000) {
        ECLI_OUT_FMT(cli, fp, fmt, "value", FMT_INT, g_max_connections, NULL);
    }
}
```

The output function checks if the value differs from the default before emitting output. This keeps
the "write terminal" output minimal and clean.


## API Reference

The ecli_init function initializes the CLI in foreground mode with the given configuration. The
ecli_init_tcp function initializes TCP daemon mode on the specified port. Both return 0 on success
or -1 on error. The ecli_shutdown function cleans up resources.

The ecli_run function runs the CLI event loop until the running flag becomes false. The
ecli_request_exit function sets an internal flag to request shutdown.

Output functions ecli_output and ecli_err write to the current CLI client. The ecli_err function
prefixes messages with "Error: " for user-facing error messages.

The ecli_load_config function loads and executes commands from a configuration file, returning the
number of failed commands or -1 if the file cannot be opened.

The ecli_get_mode function returns the current mode (ECLI_MODE_STDIN or ECLI_MODE_TCP) and
ecli_uses_editline returns true if readline-like editing is available.


## Directory Structure

The lib directory contains the core library implementation. The ecli.c and ecli.h files provide
the main CLI infrastructure including initialization, event loop, and TCP server. The ecli_cmd.h
header defines all the command macros. The ecli_builtin.c file implements built-in commands like
help, quit, and write. The ecli_types files provide argument type macros and parsing helpers. The
ecli_yaml files handle YAML grammar import and export. The ecli_root.c file manages the root
grammar node. The queue-extension.h header provides safe iteration macros for queue.h.

The examples directory contains sample applications. Currently it includes a minimal example that
demonstrates basic usage of the framework.


## License

This library is licensed under the GNU Affero General Public License v3.0 or later (AGPL-3.0-or-later).
