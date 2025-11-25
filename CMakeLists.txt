cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

# ---------------------------------------------------------------------------
# Project metadata
# ---------------------------------------------------------------------------
if(NOT DEFINED _name)
  set(_name "smart-lower-thirds")
endif()

if(NOT DEFINED _version)
  set(_version "1.0.0")
endif()

project(${_name} VERSION ${_version} LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# Options (these are mainly here so your CI flags are "used")
# ---------------------------------------------------------------------------
option(ENABLE_FRONTEND_API "Use obs-frontend-api for dock, hotkeys, browser auto-setup" ON)
option(ENABLE_HTTP_SERVER  "Serve overlay on http://127.0.0.1:<port>"                   ON)
option(ENABLE_QT           "Use Qt for dock UI and dialogs"                             ON)

# This plugin *requires* Qt and frontend API; don't allow disabling them.
if(NOT ENABLE_QT)
  message(FATAL_ERROR "ENABLE_QT=OFF is not supported for smart-lower-thirds; Qt is required.")
endif()

if(NOT ENABLE_FRONTEND_API)
  message(FATAL_ERROR "ENABLE_FRONTEND_API=OFF is not supported for smart-lower-thirds; obs-frontend-api is required.")
endif()

include(compilerconfig)
include(defaults)
include(helpers)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_COMPILE_WARNING_AS_ERROR OFF)

if(MSVC)
  add_compile_options(/WX- /utf-8)
endif()

# OBS 32+: use dock-by-id API
add_compile_definitions(HAVE_OBS_DOCK_BY_ID=1)

if(APPLE)
  add_compile_options(
    $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-error=newline-eof>
    $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-error=deprecated-this-capture>
    $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-error=deprecated-declarations>
    $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-error=range-loop-construct>
  )
endif()

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
set(SLT_SRC_DIR     "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(SLT_CNF_DIR     "${SLT_SRC_DIR}/config")
set(SLT_HDR_DIR     "${SLT_SRC_DIR}/headers")
set(SLT_TRD_DIR     "${SLT_SRC_DIR}/thirdparty")
set(SLT_GEN_DIR     "${CMAKE_CURRENT_BINARY_DIR}/generated")

file(MAKE_DIRECTORY "${SLT_GEN_DIR}")

# config.hpp (PLUGIN_NAME / PLUGIN_VERSION)
configure_file(
  "${SLT_CNF_DIR}/config.hpp.in"
  "${SLT_CNF_DIR}/config.hpp"
  @ONLY
)

# ---------------------------------------------------------------------------
# Target
# ---------------------------------------------------------------------------
add_library(${CMAKE_PROJECT_NAME} MODULE)

# libobs
find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

# ---------------------------------------------------------------------------
# Frontend API (REQUIRED)
# ---------------------------------------------------------------------------
find_package(obs-frontend-api REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
# Define macro for your #ifdefs
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE ENABLE_FRONTEND_API=1)

# ---------------------------------------------------------------------------
# Qt (dock & dialogs) – REQUIRED
# ---------------------------------------------------------------------------
find_package(Qt6 COMPONENTS Core Widgets QUIET)
if(Qt6_FOUND)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
else()
  find_package(Qt5 COMPONENTS Core Widgets REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt5::Core Qt5::Widgets)
endif()

set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES
  AUTOMOC ON
  AUTOUIC ON
  AUTORCC ON
)

# Define macro for your #ifdefs (even if you don't currently use it)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE ENABLE_QT=1)

# ---------------------------------------------------------------------------
# Includes
# ---------------------------------------------------------------------------
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
  ${SLT_CNF_DIR}
  ${SLT_SRC_DIR}
  ${SLT_GEN_DIR}
  ${SLT_HDR_DIR}
  ${SLT_TRD_DIR}
)

# ---------------------------------------------------------------------------
# HTTP server deps
# ---------------------------------------------------------------------------
if(ENABLE_HTTP_SERVER)
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE ENABLE_HTTP_SERVER=1)
  if(WIN32)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ws2_32)
  endif()
endif()

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------
set(SLT_SRC
  ${SLT_SRC_DIR}/main.cpp
  ${SLT_SRC_DIR}/state.cpp
  ${SLT_SRC_DIR}/widget.cpp
  ${SLT_SRC_DIR}/slt_helpers.cpp
)

if(ENABLE_HTTP_SERVER)
  list(APPEND SLT_SRC
    ${SLT_SRC_DIR}/server.cpp
  )
endif()

list(APPEND SLT_SRC
  ${SLT_SRC_DIR}/dock.cpp
  ${SLT_HDR_DIR}/dock.hpp
  ${SLT_SRC_DIR}/settings.cpp
  ${SLT_HDR_DIR}/settings.hpp
)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${SLT_SRC})

target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
  PLUGIN_NAME_STR="${_name}"
  PLUGIN_VERSION_STR="${_version}"
)

set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED YES
)

# ---------------------------------------------------------------------------
# Let obs-plugintemplate handle bundles & install layout
# ---------------------------------------------------------------------------
set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES
  OUTPUT_NAME ${_name}
)
