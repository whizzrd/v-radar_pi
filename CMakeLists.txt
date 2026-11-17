cmake_minimum_required(VERSION 3.16)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

include(${CMAKE_SOURCE_DIR}/Plugin.cmake)
project(${PKG_NAME} VERSION ${PKG_VERSION})

if(PROJECT_VERSION_TWEAK STREQUAL "")
  set(PROJECT_VERSION_TWEAK 0)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  string(APPEND CMAKE_SHARED_LINKER_FLAGS " -Wl -undefined dynamic_lookup")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  string(APPEND CMAKE_SHARED_LINKER_FLAGS " -Wl,-Bsymbolic")
endif()

find_package(wxWidgets REQUIRED COMPONENTS base core xml adv)
include(${wxWidgets_USE_FILE})

set(DEFAULT_OPENCPN_SOURCE_DIR "$ENV{HOME}/src/github.com/OpenCPN/OpenCPN")
set(OPENCPN_SOURCE_DIR
    "${DEFAULT_OPENCPN_SOURCE_DIR}"
    CACHE PATH "Path to the OpenCPN source tree")

set(OPENCPN_PLUGIN_HEADER "")
set(OPENCPN_RUNTIME_CONFIG_HEADER "")
set(OPENCPN_CONFIG_INI "$ENV{HOME}/Library/Preferences/opencpn/opencpn.ini"
    CACHE FILEPATH "Path to the running OpenCPN config file")
if(EXISTS "${OPENCPN_SOURCE_DIR}/include/ocpn_plugin.h")
  set(OPENCPN_PLUGIN_HEADER "${OPENCPN_SOURCE_DIR}/include/ocpn_plugin.h")
  set(OPENCPN_INCLUDE_DIR "${OPENCPN_SOURCE_DIR}/include")
  if(EXISTS "${OPENCPN_SOURCE_DIR}/build/include/config.h")
    set(OPENCPN_RUNTIME_CONFIG_HEADER "${OPENCPN_SOURCE_DIR}/build/include/config.h")
  endif()
elseif(EXISTS "${CMAKE_SOURCE_DIR}/opencpn-libs/api-18/ocpn_plugin.h")
  set(OPENCPN_PLUGIN_HEADER
      "${CMAKE_SOURCE_DIR}/opencpn-libs/api-18/ocpn_plugin.h")
  set(OPENCPN_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/opencpn-libs/api-18")
else()
  message(FATAL_ERROR
    "Cannot find ocpn_plugin.h. Set OPENCPN_SOURCE_DIR to your OpenCPN source "
    "tree or add opencpn-libs/api-18 to this repository.")
endif()

set(LOCAL_PLUGIN_TARGET "")
set(LOCAL_PLUGIN_TARGET_VERSION "")
if(APPLE AND CMAKE_OSX_ARCHITECTURES)
  set(LOCAL_TARGET_ARCH "${CMAKE_OSX_ARCHITECTURES}")
  if(LOCAL_TARGET_ARCH MATCHES "arm64" AND LOCAL_TARGET_ARCH MATCHES "x86_64")
    set(LOCAL_TARGET_ARCH "universal")
  endif()
else()
  set(LOCAL_TARGET_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
endif()
set(OPENCPN_VERSION_STRING "")
set(OPENCPN_VERSION "")
set(API_VERSION_MAJOR "1")
set(API_VERSION_MINOR "")

if(EXISTS "${OPENCPN_CONFIG_INI}")
  file(STRINGS "${OPENCPN_CONFIG_INI}" _compat_os_line
       REGEX "^CompatOS=.*")
  file(STRINGS "${OPENCPN_CONFIG_INI}" _compat_os_version_line
       REGEX "^CompatOsVersion=.*")
  file(STRINGS "${OPENCPN_CONFIG_INI}" _config_version_line
       REGEX "^ConfigVersionString=.*")
  if(_compat_os_line)
    string(REGEX REPLACE "^CompatOS=(.*)$" "\\1"
           LOCAL_PLUGIN_TARGET "${_compat_os_line}")
  endif()
  if(_compat_os_version_line)
    string(REGEX REPLACE "^CompatOsVersion=(.*)$" "\\1"
           LOCAL_PLUGIN_TARGET_VERSION "${_compat_os_version_line}")
  endif()
  if(_config_version_line)
    string(REGEX REPLACE "^ConfigVersionString=(.*)$" "\\1"
           OPENCPN_VERSION_STRING "${_config_version_line}")
    string(REGEX MATCH "([0-9]+\\.[0-9]+\\.[0-9]+)"
           OPENCPN_VERSION "${OPENCPN_VERSION_STRING}")
  endif()
endif()

if(OPENCPN_RUNTIME_CONFIG_HEADER)
  if(NOT LOCAL_PLUGIN_TARGET)
    file(STRINGS "${OPENCPN_RUNTIME_CONFIG_HEADER}" _target_line
         REGEX "^#define PKG_TARGET \".*\"")
    if(_target_line)
      string(REGEX REPLACE "^#define PKG_TARGET \"([^\"]+)\".*" "\\1"
             LOCAL_PLUGIN_TARGET "${_target_line}")
    endif()
  endif()
  if(NOT LOCAL_PLUGIN_TARGET_VERSION)
    file(STRINGS "${OPENCPN_RUNTIME_CONFIG_HEADER}" _target_version_line
         REGEX "^#define PKG_TARGET_VERSION \".*\"")
    if(_target_version_line)
      string(REGEX REPLACE "^#define PKG_TARGET_VERSION \"([^\"]+)\".*" "\\1"
             LOCAL_PLUGIN_TARGET_VERSION "${_target_version_line}")
    endif()
  endif()
endif()

if(LOCAL_PLUGIN_TARGET AND LOCAL_PLUGIN_TARGET_VERSION)
  set(OCPN_TARGET_TUPLE
      "${LOCAL_PLUGIN_TARGET};${LOCAL_PLUGIN_TARGET_VERSION};${LOCAL_TARGET_ARCH}")
endif()

if(OPENCPN_VERSION AND EXISTS "${OPENCPN_SOURCE_DIR}/.git")
  set(_release_header "${CMAKE_BINARY_DIR}/ocpn_plugin.release.h")
  execute_process(
    COMMAND git show "Release_${OPENCPN_VERSION}:include/ocpn_plugin.h"
    WORKING_DIRECTORY "${OPENCPN_SOURCE_DIR}"
    OUTPUT_FILE "${_release_header}"
    RESULT_VARIABLE _release_header_result
    ERROR_QUIET
  )
  if(_release_header_result EQUAL 0 AND EXISTS "${_release_header}")
    set(OPENCPN_PLUGIN_HEADER "${_release_header}")
  endif()
endif()

file(STRINGS "${OPENCPN_PLUGIN_HEADER}" _api_major_line
     REGEX "^#define API_VERSION_MAJOR [0-9]+")
file(STRINGS "${OPENCPN_PLUGIN_HEADER}" _api_minor_line
     REGEX "^#define API_VERSION_MINOR [0-9]+")
if(_api_major_line)
  string(REGEX REPLACE "^#define API_VERSION_MAJOR ([0-9]+)$" "\\1"
         API_VERSION_MAJOR "${_api_major_line}")
endif()
if(_api_minor_line)
  string(REGEX REPLACE "^#define API_VERSION_MINOR ([0-9]+)$" "\\1"
         API_VERSION_MINOR "${_api_minor_line}")
endif()
if(NOT API_VERSION_MINOR)
  message(FATAL_ERROR "Unable to determine OpenCPN plugin API version")
endif()
set(API_VERSION "${API_VERSION_MAJOR}.${API_VERSION_MINOR}")

set(VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(VERSION_PATCH ${PROJECT_VERSION_PATCH})
include(Metadata)

configure_file(
  ${CMAKE_SOURCE_DIR}/config.h.in
  ${CMAKE_BINARY_DIR}/include/config.h
  @ONLY
)

set(checksum "")
configure_file(
  ${CMAKE_SOURCE_DIR}/plugin.xml.in
  ${CMAKE_BINARY_DIR}/${pkg_displayname}.xml.in
  @ONLY
)

set(SRC
  src/ais_encoder.cpp
  src/ais_sim.cpp
  src/navico_sim.cpp
  src/scene.cpp
  src/s57_reader.cpp
  src/weather.cpp
  src/clutter.cpp
  src/grib/GribRecord.cpp
  src/prefs_dialog.cpp
  src/v_radar_pi.cpp
)

# Standalone harness: the same simulator without OpenCPN (protocol and scene debugging).
add_executable(navico_sim_cli EXCLUDE_FROM_ALL
  src/ais_encoder.cpp src/ais_sim.cpp src/navico_sim.cpp src/scene.cpp src/s57_reader.cpp src/weather.cpp src/clutter.cpp src/navico_sim_cli.cpp)
target_include_directories(navico_sim_cli PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(navico_sim_cli PRIVATE pthread)

# Dev tool: ENC extract to text, to validate the native S-57 reader against tools/enc_extract.py.
add_executable(s57_extract_cli EXCLUDE_FROM_ALL src/s57_extract_cli.cpp src/s57_reader.cpp)
target_include_directories(s57_extract_cli PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_library(${PKG_NAME} SHARED ${SRC})
target_include_directories(${PKG_NAME}
  PRIVATE
    ${CMAKE_BINARY_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${OPENCPN_INCLUDE_DIR}
)
target_link_libraries(${PKG_NAME} PRIVATE ${wxWidgets_LIBRARIES})

if(APPLE)
  set(_fixup_wx_script "${CMAKE_BINARY_DIR}/fixup_wx_rpaths.cmake")
  file(WRITE "${_fixup_wx_script}" [=[
set(target_file "$ENV{TARGET_FILE}")
if(NOT EXISTS "${target_file}")
  message(FATAL_ERROR "Missing target file: ${target_file}")
endif()

execute_process(
  COMMAND otool -L "${target_file}"
  OUTPUT_VARIABLE otool_output
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

foreach(name IN ITEMS baseu baseu_xml osx_cocoau_core)
  string(REGEX MATCH "(/[^\n\t ]*libwx_${name}-[0-9][^\n\t ]*\\.dylib)" old_path "${otool_output}")
  if(old_path)
    execute_process(
      COMMAND install_name_tool -change "${old_path}"
              "@executable_path/../Frameworks/libwx_${name}-3.2.dylib"
              "${target_file}"
      RESULT_VARIABLE change_result
    )
    if(NOT change_result EQUAL 0)
      message(FATAL_ERROR "install_name_tool failed for ${name}")
    endif()
  endif()
endforeach()
]=])

  add_custom_command(
    TARGET ${PKG_NAME}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E env TARGET_FILE=$<TARGET_FILE:${PKG_NAME}>
            ${CMAKE_COMMAND} -P "${_fixup_wx_script}"
    VERBATIM
  )
endif()

if(APPLE)
  install(
    TARGETS ${PKG_NAME}
    RUNTIME LIBRARY DESTINATION OpenCPN.app/Contents/PlugIns
  )
  install(
    DIRECTORY data
    DESTINATION OpenCPN.app/Contents/SharedSupport/plugins/${PKG_NAME}
  )
elseif(WIN32)
  install(TARGETS ${PKG_NAME} RUNTIME DESTINATION "plugins")
  if(EXISTS ${PROJECT_SOURCE_DIR}/data)
    install(DIRECTORY data DESTINATION "plugins/${PKG_NAME}")
  endif()
else()
  install(
    TARGETS ${PKG_NAME}
    RUNTIME LIBRARY DESTINATION lib/opencpn
  )
  if(EXISTS ${PROJECT_SOURCE_DIR}/data)
    install(DIRECTORY data DESTINATION share/opencpn/plugins/${PKG_NAME})
  endif()
endif()

set(_checksum_script "
execute_process(
  COMMAND cmake -E sha256sum ${CMAKE_BINARY_DIR}/${pkg_tarname}.tar.gz
  OUTPUT_FILE ${CMAKE_BINARY_DIR}/${pkg_tarname}.sha256
)
file(READ ${CMAKE_BINARY_DIR}/${pkg_tarname}.sha256 _SHA256)
string(REGEX MATCH \"^[^ ]*\" checksum \${_SHA256})
configure_file(
  ${CMAKE_BINARY_DIR}/${pkg_displayname}.xml.in
  ${CMAKE_BINARY_DIR}/${pkg_xmlname}.xml
  @ONLY
)
")
file(WRITE "${CMAKE_BINARY_DIR}/checksum.cmake" "${_checksum_script}")

set(_tarball_script "
set(root \"${CMAKE_BINARY_DIR}/app\")
set(files_dir \"\${root}/files\")
set(package_root \"\${root}/${pkg_displayname}\")

file(REMOVE_RECURSE \"\${root}\")
execute_process(
  COMMAND ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR} --prefix \${files_dir}
)
configure_file(
  ${CMAKE_BINARY_DIR}/${pkg_displayname}.xml.in
  ${CMAKE_BINARY_DIR}/app/files/metadata.xml
  @ONLY
)
file(RENAME \"\${files_dir}\" \"\${package_root}\")
execute_process(
  WORKING_DIRECTORY \"\${root}\"
  COMMAND cmake -E tar -czf ../${pkg_tarname}.tar.gz --format=gnutar ${pkg_displayname}
)
execute_process(
  COMMAND cmake -P ${CMAKE_BINARY_DIR}/checksum.cmake
)
")
file(WRITE "${CMAKE_BINARY_DIR}/make_tarball.cmake" "${_tarball_script}")

add_custom_target(tarball
  COMMAND cmake --build ${CMAKE_BINARY_DIR} --target ${PKG_NAME}
  COMMAND cmake -P ${CMAKE_BINARY_DIR}/make_tarball.cmake
  DEPENDS ${PKG_NAME}
  VERBATIM
)
