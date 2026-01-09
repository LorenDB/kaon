cmake_minimum_required(VERSION 3.16)

project(kaon VERSION 0.3.0 LANGUAGES CXX)

add_compile_definitions(KAON_APP_VERSION="${PROJECT_VERSION}"_L1)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 23)

option(EXPERIMENTAL_UUVR_SUPPORT "Enable experimental UUVR support. Expect it to not work." OFF)

find_package(Qt6 REQUIRED COMPONENTS Core Quick Widgets Sql)

qt_standard_project_setup(REQUIRES 6.10)

include(FetchContent)
FetchContent_Declare(ValveFileVDF URL https://github.com/TinyTinni/ValveFileVDF/archive/refs/tags/v1.1.1.tar.gz)
FetchContent_MakeAvailable(ValveFileVDF)

add_subdirectory(src)

install(FILES "dev.lorendb.kaon.desktop" DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications" COMPONENT kaon)
install(FILES "src/qml/icons/kaon.svg" DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps" COMPONENT kaon)
