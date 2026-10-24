# Desktop build: CI compile-checks the shared code with it, runs the unit tests and
# drives the app offscreen to take screenshots (see tests/shots.txt).
cmake_minimum_required(VERSION 3.16)
project(browsenx C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(DEPS REQUIRED sdl2 freetype2 libcurl libwebpdemux libwebp zlib)
find_package(Threads REQUIRED)

set(LH third_party/litehtml)
file(GLOB GUMBO_SOURCES ${LH}/src/gumbo/*.c)
file(GLOB LITEHTML_SOURCES ${LH}/src/*.cpp)
add_library(litehtml STATIC ${LITEHTML_SOURCES} ${GUMBO_SOURCES})
target_include_directories(litehtml PUBLIC ${LH}/include ${LH}/include/litehtml ${LH}/src ${LH}/src/gumbo/include
                           ${LH}/src/gumbo/include/gumbo)

file(GLOB CORE_SOURCES
  source/util/*.cpp
  source/net/*.cpp
  source/ui/*.cpp
  source/ui/*.c
  source/web/*.cpp
  source/app/*.cpp)

add_executable(browsenx source/main.cpp ${CORE_SOURCES} source/platform/desktop/platform_desktop.cpp)
target_include_directories(browsenx PRIVATE source third_party ${DEPS_INCLUDE_DIRS})
target_compile_options(browsenx PRIVATE ${DEPS_CFLAGS_OTHER} -Wall -Wno-unused-function)
target_link_directories(browsenx PRIVATE ${DEPS_LIBRARY_DIRS})
target_link_libraries(browsenx PRIVATE litehtml ${DEPS_LIBRARIES} Threads::Threads)

add_executable(test_urlutil tests/test_urlutil.cpp source/web/urlutil.cpp)
target_include_directories(test_urlutil PRIVATE source)
