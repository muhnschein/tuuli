# Locates libservo and servo_capi.h as shipped by the libservo-devel RPM
# (rpm/libservo.spec).  Defines the imported target ServoCapi::servo.
#
# Variables honoured: SERVO_CAPI_ROOT (prefix), PKG_CONFIG_PATH.

find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(PC_SERVO_CAPI QUIET servo_capi)
endif()

find_path(ServoCapi_INCLUDE_DIR
  NAMES servo_capi.h
  HINTS ${SERVO_CAPI_ROOT}/include ${PC_SERVO_CAPI_INCLUDEDIR} ${PC_SERVO_CAPI_INCLUDE_DIRS})
find_library(ServoCapi_LIBRARY
  NAMES servo servo_capi
  HINTS ${SERVO_CAPI_ROOT}/lib64 ${SERVO_CAPI_ROOT}/lib ${PC_SERVO_CAPI_LIBDIR} ${PC_SERVO_CAPI_LIBRARY_DIRS})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(ServoCapi
  REQUIRED_VARS ServoCapi_LIBRARY ServoCapi_INCLUDE_DIR
  VERSION_VAR PC_SERVO_CAPI_VERSION)

if(ServoCapi_FOUND AND NOT TARGET ServoCapi::servo)
  add_library(ServoCapi::servo UNKNOWN IMPORTED)
  set_target_properties(ServoCapi::servo PROPERTIES
    IMPORTED_LOCATION "${ServoCapi_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${ServoCapi_INCLUDE_DIR}")
endif()
mark_as_advanced(ServoCapi_INCLUDE_DIR ServoCapi_LIBRARY)
