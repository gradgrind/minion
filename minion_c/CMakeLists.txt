cmake_minimum_required(VERSION 3.16)

project(minion
    VERSION 5.0.0
    LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS "-Wall -Wextra")
set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-O2")
set(CMAKE_C_FLAGS "-Wall -Wextra")
set(CMAKE_C_FLAGS_DEBUG "-g")
set(CMAKE_C_FLAGS_RELEASE "-O2")

add_executable(minion
    minion.c
    minion.h
    main.c
    iofile.cpp iofile.h
    )

set_property(TARGET minion PROPERTY C_STANDARD 99)

include(GNUInstallDirs)
install(TARGETS minion
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
