cmake_minimum_required(VERSION 3.20)

if(DEBUG_FLAG)
    add_compile_options(-O0 -g)
endif()


file(GLOB SRC_FILES "*.c")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(app)
add_executable(app ${SRC_FILES})
target_include_directories(${PROJECT_NAME} PRIVATE ".")

