project(
  'fsearch',
  'c',
  version: '0.3.alpha0',
  meson_version: '>= 0.45.0',
  default_options: [
    'c_std=gnu11',
    'channel=other',
  ],
)
app_id = 'io.github.cboxdoerfer.FSearch'

gnome = import('gnome')
i18n = import('i18n')

cc = meson.get_compiler('c')

# Check if the build platform supports using malloc_trim
malloc_trim_support_result = cc.run(
  '''
    #include <malloc.h>

    int main (int argc, char *argv[]) {
      malloc_trim(0);
      return 0;
    }
  ''',
  name: 'malloc_trim is supported on the build platform',
)

if malloc_trim_support_result.compiled() and malloc_trim_support_result.returncode() == 0
  have_malloc_trim = true
else
  have_malloc_trim = false
endif

# Check if the build platform supports using fanotify without the CAP_SYS_ADMIN capability
fanotify_support_result = cc.run(
  '''
    #include <sys/fanotify.h>
    #include <fcntl.h>

    int main (int argc, char *argv[]) {
      int fd = fanotify_init (FAN_CLOEXEC|FAN_CLASS_NOTIF|FAN_REPORT_DFID_NAME, O_RDONLY);
      return fd < 0 ? 1 : 0;
    }
  ''',
  name: 'fanotify is supported by the build platform and works without CAP_SYS_ADMIN capabilities',
)

if fanotify_support_result.compiled() and fanotify_support_result.returncode() == 0
  have_fanotify = true
else
  have_fanotify = false
endif

# Check if the build platform supports inotify
inotify_support_result = cc.run(
  '''
    #include <sys/inotify.h>
    #include <fcntl.h>

    int main (int argc, char *argv[]) {
      int fd = inotify_init1(IN_NONBLOCK);
      return fd < 0 ? 1 : 0;
    }
  ''',
  name: 'inotify is supported by the build platform',
)

if inotify_support_result.compiled() and inotify_support_result.returncode() == 0
  have_inotify = true
else
  have_inotify = false
endif

config_h = configuration_data()
config_h.set('HAVE_MALLOC_TRIM', have_malloc_trim)
config_h.set('HAVE_FANOTIFY', have_fanotify)
config_h.set('HAVE_INOTIFY', have_inotify)
config_h.set_quoted('APP_ID', app_id)
config_h.set_quoted('PACKAGE_VERSION', meson.project_version())
config_h.set_quoted('VERSION', meson.project_version())
config_h.set_quoted('GETTEXT_PACKAGE', 'fsearch')
config_h.set_quoted('LOCALEDIR', join_paths(get_option('prefix'), get_option('localedir')))
config_h.set_quoted('PACKAGE_WEBSITE', 'https://github.com/cboxdoerfer/fsearch')
config_h.set_quoted('PACKAGE_ICON_NAME', app_id)
config_h.set_quoted('PACKAGE_NAME', 'FSearch')
config_h.set_quoted('BUILD_CHANNEL', get_option('channel'))

# ensure off_t is 64bit
add_project_arguments('-DHAVE_CONFIG_H', '-D_FILE_OFFSET_BITS=64', '-D_GNU_SOURCE', language: 'c')

fsearch_include_dirs = include_directories(
  '.',
)

subdir('data')
subdir('help')
subdir('src')
subdir('po')

configure_file(
  output: 'config.h',
  configuration: config_h,
)