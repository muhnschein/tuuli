# No-op libservo for compile/link checks of the servo_capi shim on hosts
# without an engine build.  Never installed.
add_library(servo_capi_stub SHARED servo_capi_stub.cpp)
target_include_directories(servo_capi_stub PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
set_target_properties(servo_capi_stub PROPERTIES OUTPUT_NAME servo_stub)
add_library(ServoCapi::servo ALIAS servo_capi_stub)
