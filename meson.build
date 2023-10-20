project('gst-ki-plugins', 'c', 'cpp', version : '1.18.0', license : 'LGPL')

plugins_install_dir = join_paths(get_option('libdir'), 'gstreamer-1.0')

cc = meson.get_compiler('c')
cxx = meson.get_compiler('cpp')

gst_version = meson.project_version()

api_version = '1.0'

gst_dep = dependency('gstreamer-1.0', version : '>=1.18',
    required : true, fallback : ['gstreamer', 'gst_dep'])
gstbase_dep = dependency('gstreamer-base-1.0', version : '>=1.18',
  fallback : ['gstreamer', 'gst_base_dep'])

libopencv4_headers = [
  'opencv4/opencv2/bgsegm.hpp',
  'opencv4/opencv2/calib3d.hpp',
  'opencv4/opencv2/core.hpp',
  'opencv4/opencv2/imgproc.hpp',
  'opencv4/opencv2/objdetect.hpp',
  'opencv4/opencv2/opencv.hpp',
  'opencv4/opencv2/video.hpp',
]

opencv_dep = dependency('opencv4', version : ['>= 4.0.0', '< 4.9.0'], required : false)
opencv_found = opencv_dep.found()
if opencv_found
  foreach h : libopencv4_headers
    if not cxx.has_header(h)
      message('Needed header "' + h + '" not found')
      opencv_found = false
    endif
  endforeach
endif

plugin_c_args = ['-DHAVE_CONFIG_H']

cdata = configuration_data()
cdata.set_quoted('PACKAGE_VERSION', gst_version)
cdata.set_quoted('PACKAGE', 'gstkiplugin')
cdata.set_quoted('GST_LICENSE', 'LGPL')
cdata.set_quoted('GST_API_VERSION', api_version)
cdata.set_quoted('GST_PACKAGE_NAME', 'GStreamer KnotInspector plugin collection')
cdata.set_quoted('GST_PACKAGE_ORIGIN', 'https://knotinspector.ru')
cfg_file = configure_file(output : 'config.h', configuration : cdata)

gstvideo_dep = dependency('gstreamer-video-1.0',
    fallback: ['gst-plugins-base', 'video_dep'])

compositor_sources = [
  'src/remap.cpp',
]

gstkiplugins = library('gstkiplugins',
  compositor_sources,
  c_args: plugin_c_args + cxx.get_supported_arguments(['-Wno-missing-include-dirs', '-fpermissive', '-Wno-format-nonliteral']),
  cpp_args: plugin_c_args + cxx.get_supported_arguments(['-Wno-missing-include-dirs', '-fpermissive', '-Wno-format-nonliteral']),
  dependencies : [gst_dep, gstvideo_dep, opencv_dep],
  install : true,
  install_dir : plugins_install_dir,
)
