faad_opt = get_option('faad').require(gpl_allowed, error_message: '''
  Plugin faad explicitly required via options but GPL-licensed plugins disabled via options.
  Pass option -Dgpl=enabled to Meson to allow GPL-licensed plugins to be built.
  ''')

if faad_opt.disabled()
  faad_dep = dependency('', required: false)
  subdir_done()
endif

faad_args = [ ]

faad_dep = dependency('faad2', version : '>= 2.9.2', required: faad_opt)

if faad_dep.found()
  gstfaad = library('gstfaad',
    'gstfaad.c',
    c_args : gst_plugins_bad_args + [ '-DFAAD2_MINOR_VERSION=7', '-DFAAD_IS_NEAAC' ],
    link_args : noseh_link_args,
    include_directories : [configinc],
    dependencies : [gstaudio_dep, gstpbutils_dep, gsttag_dep, faad_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  plugins += [gstfaad]
endif
