# -*- mode: meson -*-

# Style objective: be consistent with what mesonbuild.com documents/uses, and/or
# the meson book: https://meson-manual.com/

project(
  'doom-lock',
  'c',
  version: '2.16',
  default_options: [
    'c_std=c11',
    'warning_level=1',  # enable all warnings (-Wall)
  ],
  meson_version: '>=0.55.0',
)

if host_machine.system() != 'linux'
  error('doom-lock requires Linux PAM and X11')
endif

cc = meson.get_compiler('c')
add_project_arguments(cc.get_supported_arguments(['-Wunused-value']), language: 'c')

meson.add_dist_script('meson/meson-dist-script')

################################################################################
# Version handling
################################################################################

cdata = configuration_data()

cdata.set_quoted('I3LOCK_VERSION', '@VCS_TAG@')
cdata.set_quoted('SYSCONFDIR', join_paths(get_option('prefix'), get_option('sysconfdir')))

if get_option('b_sanitize').split(',').contains('address')
  cdata.set('I3LOCK_ASAN_ENABLED', 1)
endif

cdata.set('HAVE_STRNDUP', cc.has_function('strndup'))
cdata.set('HAVE_MKDIRP', cc.has_function('mkdirp'))
cdata.set('HAVE_EXPLICIT_BZERO', cc.has_function('explicit_bzero'))
cdata.set10('DOOM_WAD_ASSETS', get_option('wad_assets'))

# Instead of generating config.h directly, make vcs_tag generate it so that
# @VCS_TAG@ is replaced.
config_h_in = configure_file(
  output: 'config.h.in',
  configuration: cdata,
)
config_h = declare_dependency(
  sources: vcs_tag(
    input: config_h_in,
    output: 'config.h',
    fallback: meson.project_version() + '-non-git',
  )
)

################################################################################
# manpages
################################################################################

# Keep the upstream manual as a reference; do not replace system i3lock files.

# Required for e.g. struct ucred to be defined as per unix(7).
add_project_arguments('-D_GNU_SOURCE', language: 'c')

# https://mesonbuild.com/howtox.html#add-math-library-lm-portably
m_dep = cc.find_library('m', required: false)
rt_dep = cc.find_library('rt', required: false)

xcb_dep = dependency('xcb', method: 'pkg-config')
xcb_xkb_dep = dependency('xcb-xkb', method: 'pkg-config')
xcb_xinerama_dep = dependency('xcb-xinerama', method: 'pkg-config')
xcb_randr_dep = dependency('xcb-randr', method: 'pkg-config')
xcb_shape_dep = dependency('xcb-shape', method: 'pkg-config')
xcb_image_dep = dependency('xcb-image', method: 'pkg-config')
xcb_util_dep = dependency('xcb-util', method: 'pkg-config')
xcb_util_xrm_dep = dependency('xcb-xrm', method: 'pkg-config')
xkbcommon_dep = dependency('xkbcommon', method: 'pkg-config')
xkbcommon_x11_dep = dependency('xkbcommon-x11', method: 'pkg-config')
cairo_dep = dependency('cairo', version: '>=1.14.4', method: 'pkg-config')

i3lock_srcs = [
  'auth.c',
  'hud.c',
  'assets.c',
  'dpi.c',
  'doom.c',
  'melt.c',
  'level.c',
  'i3lock.c',
  'randr.c',
  'unlock_indicator.c',
  'xcb.c',
]

ev_dep = cc.find_library('ev')

thread_dep = dependency('threads')

i3lock_deps = [
  thread_dep,
  m_dep,
  rt_dep,
  ev_dep,
  config_h,
  cairo_dep,
  xcb_dep,
  xcb_xkb_dep,
  xcb_xinerama_dep,
  xcb_randr_dep,
  xcb_shape_dep,
  xcb_image_dep,
  xcb_util_dep,
  xcb_util_xrm_dep,
  xkbcommon_dep,
  xkbcommon_x11_dep,
]

pam_dep = cc.find_library('pam', required: true)
i3lock_deps += [pam_dep]

inc = include_directories('include')

executable(
  'i3lock-doom',
  i3lock_srcs,
  install: true,
  include_directories: inc,
  dependencies: i3lock_deps,
)

# System i3lock supplies the fingerprint PAM service. The launcher prepares the
# separate password policy locally; this fork never installs a system policy.
