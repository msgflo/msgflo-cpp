cmake_minimum_required(VERSION 3.5)
project(msgflo_cpp)

set(MSGFLO_VERSION 1.0)

set(amqp_install ${CMAKE_CURRENT_BINARY_DIR}/thirdparty-install/amqpcpp)

include(ExternalProject)
ExternalProject_Add(amqpcpp_project
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/amqpcpp
#    CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${amqp_install}
    CONFIGURE_COMMAND ""
    BUILD_COMMAND make -j4
    BUILD_IN_SOURCE TRUE
    INSTALL_COMMAND make install PREFIX=${amqp_install}
    )

set(JSON11 thirdparty/json11/json11.cpp thirdparty/json11/json11.hpp)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/json11/"
        DESTINATION "include"
        FILES_MATCHING PATTERN "*.hpp")

set(Boost_USE_STATIC_LIBS ON)
find_package(Boost COMPONENTS REQUIRED system)

add_library(msgflo src/msgflo.cpp ${JSON11})
target_include_directories(msgflo
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/json11>
    PRIVATE ${Boost_INCLUDE_DIR} ${amqp_install}/include)

target_link_libraries(msgflo
    PRIVATE ${Boost_LIBRARIES} ${amqp_install}/lib/libamqpcpp.a)
target_compile_features(msgflo PUBLIC cxx_range_for)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/"
    DESTINATION "include")

add_dependencies(msgflo amqpcpp_project)

include(GenerateExportHeader)
include(CMakePackageConfigHelpers)
install(TARGETS msgflo
    EXPORT msgflo_export
    LIBRARY DESTINATION  lib
    ARCHIVE DESTINATION  lib
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
    FILES         cmake/MsgFloConfig.cmake "${CMAKE_CURRENT_BINARY_DIR}/MsgFlo/MsgFloConfigVersion.cmake"
    DESTINATION   lib/cmake/MsgFlo)
