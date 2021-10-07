ocio_sources = [
	'gstocio.cpp'
]

if get_option('ocio').disabled()
	subdir_done()
endif

ocio_dep = dependency('OpenColorIO', required: get_option('ocio'))

if ocio_dep.found()
	gstocio = library('gstocio',
		ocio_sources,
		cpp_args: gst_plugins_bad_args,
		include_directories: [configinc],
		dependencies: [gstbase_dep, gstvideo_dep, ocio_dep],
		install: true,
		install_dir: plugins_install_dir
	)
	pkgconfig.generate(gstocio, install_dir: plugins_pkgconfig_install_dir)
	plugins += [gstocio]
endif