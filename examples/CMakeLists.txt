cmake_minimum_required(VERSION 3.5)
project(msgflo_cpp_example)

find_package(MsgFlo)

add_executable(repeat repeat.cpp)
target_compile_features(repeat PUBLIC cxx_range_for)
target_link_libraries(repeat PUBLIC MsgFlo::msgflo pthread)
