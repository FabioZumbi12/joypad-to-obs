#  Joypad to OBS Plugin for OBS - CMake Build Configuration
#  This CMake configuration file sets up the build process.
#  It includes options for enabling the obs-frontend-api and Qt functionalities,
#  and specifies custom output directories for the built plugin and its associated data.
#
#  If the build fail (the folder build_x64 don't exists), run on terminal: cmake --preset windows-x64
#
#  For buils on github, push tags:
#  - git tag 1.0.0
#  - git push origin --tags
#  For github/scripts folders:
#  - git add --chmod=+x *
#  Build:
#  - cmake --build build_x64 --config RelWithDebInfo --target joypad-to-obs
#  Check formats:
#  - pre-commit run --all-files --show-diff-on-failure
cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)
option(ENABLE_INNO_SETUP "Build installer using Inno Setup" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

# CI fallback: resolve OBS package config locations from local dependency tree.
set(_deps_root "${CMAKE_CURRENT_SOURCE_DIR}/.deps")
if(EXISTS "${_deps_root}")
  list(APPEND CMAKE_PREFIX_PATH "${_deps_root}" "${_deps_root}/cmake")
endif()

function(_resolve_deps_package_dir package_name package_config_filename)
  if(NOT EXISTS "${_deps_root}")
    return()
  endif()

  file(GLOB_RECURSE _cfg_candidates "${_deps_root}/*${package_config_filename}")
  list(LENGTH _cfg_candidates _cfg_count)
  if(_cfg_count EQUAL 0)
    return()
  endif()

  list(GET _cfg_candidates 0 _cfg)
  get_filename_component(_cfg_dir "${_cfg}" DIRECTORY)
  get_filename_component(_cfg_dir_parent "${_cfg_dir}" DIRECTORY)

  # Help both direct *_DIR lookups and dependent find_package() calls.
  list(APPEND CMAKE_PREFIX_PATH "${_cfg_dir}" "${_cfg_dir_parent}")
  set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH}" PARENT_SCOPE)
  set("${package_name}_DIR" "${_cfg_dir}" CACHE PATH "Path to ${package_name} CMake package" FORCE)
endfunction()

if(NOT DEFINED libobs_DIR)
  _resolve_deps_package_dir("libobs" "libobsConfig.cmake")
endif()

if(ENABLE_FRONTEND_API AND NOT DEFINED obs-frontend-api_DIR)
  _resolve_deps_package_dir("obs-frontend-api" "obs-frontend-apiConfig.cmake")
endif()

if(WIN32 AND NOT DEFINED w32-pthreads_DIR)
  _resolve_deps_package_dir("w32-pthreads" "w32-pthreadsConfig.cmake")
endif()

list(REMOVE_DUPLICATES CMAKE_PREFIX_PATH)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/joypad-plugin.cpp
    src/joypad-config.cpp
    src/joypad-input.cpp
    src/joypad-actions.cpp
    src/joypad-ui.cpp
    src/joypad-config.h
    src/joypad-input.h
    src/joypad-actions.h
    src/joypad-ui.h
)

if(WIN32)
  target_link_libraries(
    ${CMAKE_PROJECT_NAME}
    PRIVATE dinput8 dxguid wbemuuid ole32 oleaut32
  )
endif()

if(APPLE)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE "-framework IOKit" "-framework CoreFoundation")
endif()

if(WIN32)
  # https://files.jrsoftware.org/is/6/innosetup-6.6.0.exe
  set(INNO_SETUP_COMPILER "C:/Program Files (x86)/Inno Setup 6/ISCC.exe")
  set(INNO_ARGS "")
  if(PLUGIN_VERSION)
    set(INNO_ARGS "/DAppVersion=${PLUGIN_VERSION}")
  endif()

  if(ENABLE_INNO_SETUP AND EXISTS "${INNO_SETUP_COMPILER}")
    add_custom_command(
      TARGET ${_name}
      POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E echo "Executando Inno Setup como ${_name}..."
      COMMAND "${INNO_SETUP_COMPILER}" "${CMAKE_SOURCE_DIR}/setup.iss" "/DBuildConfig=$<CONFIG>" ${INNO_ARGS}
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
      COMMENT "Criando instalador ${_name}-Setup.exe (opcional)..."
    )
  else()
    message(STATUS "Inno Setup desabilitado ou nao encontrado. Ignorando criacao do instalador.")
  endif()
endif()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME
                                                              ${_name}
)
