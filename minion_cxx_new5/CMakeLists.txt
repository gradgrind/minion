cmake_minimum_required(VERSION 3.16)

project(minion
    VERSION 5.0.0
    LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS "-Wall -Wextra")
set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-O2")

add_executable(minion
    minion.cpp
    minion.h
    main.cpp
    iofile.cpp iofile.h
    )

set_property(TARGET minion PROPERTY CXX_STANDARD 20)
