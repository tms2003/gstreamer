ocio_sources = [
	'gstocio.cpp'
]

if get_option('ocio').disabled()
	subdir_done()
endif

ocio_dep = cc.find_library('OpenColorIO', required: false)
have_ocio = ocio_dep.found()

if not have_ocio
	ocio_dep = dependency('OpenColorIO', required: get_option('ocio'))
	have_ocio = ocio_dep.found()
endif

if have_ocio
	gstocio = library('gstocio',
		ocio_sources,
		c_args: gst_plugins_bad_args,
		include_directories: [configinc],
		dependencies: [gstbase_dep, gstvideo_dep],
		install: true,
		install_dir: plugins_install_dir
	)
	pkgconfig.generate(gstocio, install_dir: plugins_pkgconfig_install_dir)
	plugins += [gstocio]
elif get_option('ocio').enabled()
	error('ocio plugin enabled but OpenColorIO library or headers not found')
endif