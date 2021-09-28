install_man('fsearch.1')

desktop_file_in_config = configuration_data()
desktop_file_in_config.set('app_id', app_id)
desktop_file_in = configure_file(
          input: 'fsearch.desktop.in.in',
         output: '@0@.desktop.in'.format(app_id),
  configuration: desktop_file_in_config,
)


desktop_file = i18n.merge_file(
        input: desktop_file_in,
       output: '@0@.desktop'.format(app_id),
         type: 'desktop',
       po_dir: '../po',
      install: true,
  install_dir: join_paths(get_option('datadir'), 'applications')
)

desktop_utils = find_program('desktop-file-validate', required: false)
if desktop_utils.found()
  test('Validate desktop file', desktop_utils,
    args: [desktop_file]
  )
endif

appdata_file = i18n.merge_file(
  input: 'io.github.cboxdoerfer.FSearch.appdata.xml.in',
  output: 'io.github.cboxdoerfer.FSearch.appdata.xml',
  type: 'xml',
  po_dir: join_paths(meson.source_root(), 'po'),
  install: true,
  install_dir: join_paths(get_option('datadir'), 'metainfo')
)

appstream_util = find_program('appstream-util', required: false)
if appstream_util.found()
  test(
    'validate-appdata', appstream_util,
    args: [
      'validate-relax', '--nonet', appdata_file.full_path()
    ]
  )
endif
