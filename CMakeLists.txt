cmake_minimum_required(VERSION 3.24)
project(NicholasStash LANGUAGES C CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Guild Wars is 32-bit: configure with -A Win32 (or an x86 toolchain).")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
include(FetchContent)
FetchContent_Declare(minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG c3fcafdc10146beb5919319d0683e44e3c30d537) # v1.3.4
FetchContent_MakeAvailable(minhook)

add_library(NicholasStash SHARED src/NicholasStash.cpp)
target_link_libraries(NicholasStash PRIVATE minhook)
target_compile_definitions(NicholasStash PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
if(MSVC)
    target_compile_options(NicholasStash PRIVATE /W4 /EHa /utf-8)
endif()
