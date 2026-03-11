cmake_minimum_required(VERSION 3.4.1)
project(Pitchy)

set (CMAKE_VERBOSE_MAKEFILE ON)
set (CMAKE_CXX_STANDARD 20)

add_library(react-native-pitchy            SHARED
            ../cpp/react-native-pitchy.cpp
            cpp-adapter.cpp
)

# Specifies a path to native header files.
include_directories(
            ../cpp
)
