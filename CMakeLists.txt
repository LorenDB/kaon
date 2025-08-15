cmake_minimum_required(VERSION 3.16)

project(kaon VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 23)

find_package(Qt6 REQUIRED COMPONENTS Core Quick Widgets)

qt_standard_project_setup(REQUIRES 6.8)

include(FetchContent)
FetchContent_Declare(sourcepp
    GIT_REPOSITORY https://github.com/craftablescience/sourcepp.git
    GIT_TAG 9516bd37c308f1e0388d6f548231de52f768df46
)
FetchContent_MakeAvailable(sourcepp)

add_subdirectory(src)
