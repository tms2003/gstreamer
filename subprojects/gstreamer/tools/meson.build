# If tools are disabled, we still allow building them against gst-full
# later, so populate the gst_tools dictionary in any case.
gst_tools = {}

tools = ['gst-inspect', 'gst-stats', 'gst-typefind']

extra_launch_dep = []
extra_launch_arg = []

if gst_parse
  if host_system == 'windows' and not building_for_uwp
    winmm_lib = cc.find_library('winmm', required: false)
    if winmm_lib.found() and cc.has_header('mmsystem.h')
      extra_launch_dep += [winmm_lib]
      extra_launch_arg += ['-DHAVE_WINMM']
    endif
  endif

  tools += ['gst-launch']
endif

foreach tool : tools
  exe_name = '@0@-@1@'.format(tool, apiversion)
  src_file = '@0@.c'.format(tool)
  extra_deps = []
  extra_c_args = []

  if tool == 'gst-launch'
    extra_deps += extra_launch_dep
    extra_c_args += extra_launch_arg
  endif

  man_page = files('@0@-1.0.1'.format(tool))
  if not get_option('tools').disabled()
    tool_srcfile = [src_file]
    if host_system == 'windows'
      tool_rc_cdata = gst_version_cdata
      tool_rc_cdata.set('GST_FILE_TYPE', 'VFT_APP')
      tool_rc_cdata.set('GST_BIN_DESC', 'GStreamer core tool')
      tool_rc_cdata.set('GST_BIN_NAME', exe_name)
      tool_rc = configure_file(
        input: join_paths(meson.project_source_root(), 'gstreamer.rc.in'),
        output: '@0@.rc'.format(exe_name),
        configuration: tool_rc_cdata,
      )
      tool_res = windows.compile_resources(tool_rc)
      tool_srcfile += [tool_res]
    endif

    executable(exe_name,
      tool_srcfile,
      install: true,
      install_tag: 'bin',
      include_directories : [configinc],
      dependencies : [glib_dep, gobject_dep, gmodule_dep, mathlib, gst_dep] + extra_deps,
      c_args: gst_c_args + extra_c_args + ['-DG_LOG_DOMAIN="@0@"'.format(exe_name)],
    )

    install_man(man_page)
  endif

  gst_tools += {tool:
    {
      'files': files(src_file),
      'deps': [glib_dep, gobject_dep, gmodule_dep, mathlib, gst_dep] + extra_deps,
      'extra_c_args': extra_c_args,
      'man_page': man_page,
    }
  }
endforeach
