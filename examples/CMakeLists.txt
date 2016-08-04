# If we're not included by the msgflo project, find the sources the normal way. This is what you should do in your
# application
if (CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.5)
    project(msgflo_cpp_examples CXX)

    find_package(MsgFlo)
    set(msgflo_lib MsgFlo::msgflo)
else ()
    set(msgflo_lib msgflo)
endif ()

add_executable(repeat repeat.cpp)
target_compile_features(repeat PUBLIC cxx_range_for)
target_link_libraries(repeat PUBLIC ${msgflo_lib} ev pthread)
