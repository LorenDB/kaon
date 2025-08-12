cmake_minimum_required(VERSION 3.16)

project(kaon VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 23)

find_package(Qt6 REQUIRED COMPONENTS Core Quick Widgets)

qt_standard_project_setup(REQUIRES 6.8)

include(FetchContent)
FetchContent_Declare(ValveFileVDF URL https://github.com/TinyTinni/ValveFileVDF/archive/refs/tags/v1.1.1.tar.gz)
FetchContent_MakeAvailable(ValveFileVDF)

add_subdirectory(src)

install(FILES "dev.lorendb.kaon.desktop" DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications" COMPONENT kaon)
install(FILES "kaon.svg" DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps" COMPONENT kaon)
