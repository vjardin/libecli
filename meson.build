project('libecli', 'c',
    version : '1.0.0',
    license : 'AGPL-3.0-or-later',
    default_options : [
        'c_std=c11',
        'warning_level=2',
        'werror=true',
        'b_ndebug=if-release',
        'default_library=shared',
    ],
    meson_version : '>=0.56.0'
)

cc = meson.get_compiler('c')

# Required dependencies
dep_libevent = dependency('libevent', required : true)
dep_yaml = dependency('yaml-0.1', required : true)

# libecoli - CLI library
# Try pkg-config first, then fall back to local build
dep_ecoli = dependency('libecoli', required : false)
if not dep_ecoli.found()
    # Local libecoli build
    libecoli_dir = '/home/vjardin/dev/libecoli'
    dep_ecoli = declare_dependency(
        include_directories : include_directories(libecoli_dir / 'include'),
        dependencies : cc.find_library('ecoli',
            dirs : [libecoli_dir / 'build'],
            required : true)
    )
endif

# Include directories
lib_inc = include_directories('lib')

# Library sources
lib_sources = files(
    'lib/ecli.c',
    'lib/ecli_yaml.c',
    'lib/ecli_builtin.c',
    'lib/ecli_types.c',
    'lib/ecli_root.c',
)

# Build CLI library (shared by default, can be overridden with -Ddefault_library=static)
libecli = library('ecli',
    lib_sources,
    include_directories : lib_inc,
    dependencies : [dep_libevent, dep_ecoli, dep_yaml],
    c_args : ['-D_GNU_SOURCE', '-D_POSIX_C_SOURCE=200809L'],
    version : meson.project_version(),
    soversion : '1',
    install : true,
)

# Create dependency object for the library
# For shared library, normal link is fine
# For static library, use link_whole to ensure EC_INIT constructors are included
if get_option('default_library') == 'static'
    dep_libecli = declare_dependency(
        link_whole : libecli,
        include_directories : lib_inc,
        dependencies : [dep_libevent, dep_ecoli, dep_yaml],
    )
else
    dep_libecli = declare_dependency(
        link_with : libecli,
        include_directories : lib_inc,
        dependencies : [dep_libevent, dep_ecoli, dep_yaml],
    )
endif

# Install headers
install_headers(
    'lib/ecli.h',
    'lib/ecli_cmd.h',
    'lib/ecli_types.h',
    'lib/ecli_yaml.h',
    'lib/queue-extension.h',
    subdir : 'ecli',
)

# Generate pkg-config file
pkg = import('pkgconfig')
pkg.generate(libecli,
    name : 'libecli',
    description : 'CLI framework based on libecoli',
    url : 'https://github.com/vjardin/libecli',
)

# Build examples
subdir('examples')

summary({
    'Version' : meson.project_version(),
    'Prefix' : get_option('prefix'),
    'Library type' : get_option('default_library'),
}, section : 'Project')

summary({
    'libevent' : dep_libevent.found(),
    'libecoli' : dep_ecoli.found(),
    'libyaml' : dep_yaml.found(),
}, section : 'Dependencies')
