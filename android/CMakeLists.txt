cmake_minimum_required(VERSION 3.4.1)
project(Pitchy)

set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_CXX_STANDARD 17)

add_library(react-native-pitchy SHARED
    ../cpp/react-native-pitchy.cpp
    ../cpp/yin-fft.cpp
    ../cpp/mpm.cpp
    ../cpp/hps.cpp
    ../cpp/amdf.cpp
    ../cpp/rapt.cpp
    ../cpp/pitch-detector.cpp
    cpp-adapter.cpp
)

include_directories(../cpp)
