project('fsearch', 'c',
    version: '0.1.3',
    meson_version: '>= 0.45.0',
    default_options: ['c_std=c11',
                      'channel=other',
                      ],
    )
app_id = 'io.github.cboxdoerfer.FSearch'

cc = meson.get_compiler('c')

gnome = import('gnome')
i18n = import('i18n')

config_h = configuration_data()
config_h.set_quoted('APP_ID', app_id)
config_h.set_quoted('PACKAGE_VERSION', meson.project_version())
config_h.set_quoted('VERSION', meson.project_version())
config_h.set_quoted('GETTEXT_PACKAGE', 'fsearch')
config_h.set_quoted('LOCALEDIR', join_paths(get_option('prefix'), get_option('localedir')))
config_h.set_quoted('PACKAGE_WEBSITE', 'https://github.com/cboxdoerfer/fsearch')
config_h.set_quoted('PACKAGE_ICON_NAME', app_id)
config_h.set_quoted('PACKAGE_NAME', 'FSearch')
config_h.set_quoted('BUILD_CHANNEL', get_option('channel'))

add_project_arguments('-DHAVE_CONFIG_H', language : 'c')
# ensure off_t is 64bit
add_project_arguments('-D_FILE_OFFSET_BITS=64', language : 'c')

fsearch_include_dirs = include_directories(
  '.',
)

subdir('data')
subdir('src')
subdir('po')

configure_file(
    output: 'config.h',
    configuration: config_h,
    )

