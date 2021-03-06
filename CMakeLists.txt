cmake_minimum_required(VERSION 3.0)
project(msgflo_cpp CXX)

set(MSGFLO_VERSION 0.0.6)

# Specify C++11
add_compile_options(-std=c++11)

# AMQP-CPP configuration
set(amqp_install ${CMAKE_CURRENT_BINARY_DIR}/thirdparty-install/amqpcpp)

include(ExternalProject)
ExternalProject_Add(amqpcpp_project
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/amqpcpp
    CONFIGURE_COMMAND ""
    BUILD_COMMAND make CPP=${CMAKE_CXX_COMPILER} LD=${CMAKE_CXX_COMPILER}
    BUILD_IN_SOURCE TRUE
    INSTALL_COMMAND make install PREFIX=${amqp_install}
    )

# Json11 configuration
set(JSON11 thirdparty/json11/json11.cpp thirdparty/json11/json11.hpp)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/json11/"
    DESTINATION "include"
    FILES_MATCHING PATTERN "*.hpp")

# libev, used by AMQP
find_path(libev_INCLUDE_DIRECTORY ev.h)
find_library(libev_LIB ev)

if (NOT libev_INCLUDE_DIRECTORY OR NOT libev_LIB)
    message(FATAL_ERROR "Could not find header and/or library for libev")
endif ()

# Mosquitto
find_path(mosquitto_INCLUDE_DIRECTORY mosquitto.h)
find_library(mosquitto_LIB mosquitto)

if (NOT mosquitto_INCLUDE_DIRECTORY OR NOT mosquitto_LIB)
  message(FATAL_ERROR "Could not find header and/or library for Mosquitto")
endif ()

# MsgFlo library
add_library(msgflo src/msgflo.cpp src/mqtt_support.cpp src/mqtt_support.h ${JSON11})
target_include_directories(msgflo
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/json11>
    PRIVATE ${libev_INCLUDE_DIRECTORY}
    PRIVATE ${amqp_install}/include
    PRIVATE ${mosquitto_INCLUDE_DIRECTORY}
    PRIVATE src)

# amqp just as too many unused parameters
target_compile_options(msgflo PRIVATE -Wall -Wextra -Wno-unused-parameter)

target_link_libraries(msgflo
    PRIVATE ${amqp_install}/lib/libamqpcpp.a
    PRIVATE ${mosquitto_LIB}
    PRIVATE ${libev_LIB})
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/"
    DESTINATION "include")

add_subdirectory(examples)

add_dependencies(msgflo amqpcpp_project)

# Installation

include(GenerateExportHeader)
include(CMakePackageConfigHelpers)
install(TARGETS msgflo
    EXPORT msgflo_export
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    INCLUDES include)

install(EXPORT msgflo_export
    FILE MsgFloTargets.cmake
    NAMESPACE MsgFlo::
    DESTINATION lib/cmake/MsgFlo)

configure_file(cmake/MsgFloConfig.cmake
    COPYONLY
    )

write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/MsgFlo/MsgFloConfigVersion.cmake"
    VERSION ${MSGFLO_VERSION}
    COMPATIBILITY AnyNewerVersion)

install(
    FILES cmake/MsgFloConfig.cmake "${CMAKE_CURRENT_BINARY_DIR}/MsgFlo/MsgFloConfigVersion.cmake"
    DESTINATION lib/cmake/MsgFlo)
