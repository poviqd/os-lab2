cmake_minimum_required(VERSION 3.16)
project(ProcLaunch LANGUAGES C)
 
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
 
add_library(proclaunch STATIC src/proclaunch.c)
target_include_directories(proclaunch PUBLIC include)
 
add_executable(proctest test/proctest.c)
target_link_libraries(proctest PRIVATE proclaunch)
 
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(proclaunch PRIVATE -Wall -Wextra)
    target_compile_options(proctest PRIVATE -Wall -Wextra)
endif()
