cmake_minimum_required(VERSION 3.16)

project(kaon VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 23)

find_package(Qt6 REQUIRED COMPONENTS Core Quick Widgets)
find_package(Qt6 REQUIRED COMPONENTS Core)

qt_standard_project_setup(REQUIRES 6.8)

add_subdirectory(3rdparty/ValveFileVDF)

add_subdirectory(src)
