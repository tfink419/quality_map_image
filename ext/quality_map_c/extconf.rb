require 'mkmf'

abort 'Missing PNG library.' unless have_library('png')
abort 'Missing Z library.' unless have_library('z')
abort 'Missing M library.' unless have_library('m')

LIBDIR      = RbConfig::CONFIG['libdir']
INCLUDEDIR  = RbConfig::CONFIG['includedir']

HEADER_DIRS = [
  # First search /opt/local for macports
  '/opt/local/include',

  # Then search /usr/local for people that installed from source
  '/usr/local/include',

  # Check the ruby install locations
  INCLUDEDIR,

  # Finally fall back to /usr
  '/usr/include',
]

LIB_DIRS = [
  # Then search /usr/local for people that installed from source
  '/usr/local/lib',

  # Check the ruby install locations
  LIBDIR,

  # Finally fall back to /usr
  '/usr/lib',
]

dir_config('quality_map_c', HEADER_DIRS, LIB_DIRS) # Tried with the line commented out, doesn't make any difference

create_makefile 'quality_map_c/quality_map_c'