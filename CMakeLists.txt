cmake_minimum_required(VERSION 3.16)

project(kaon VERSION 0.3.0 LANGUAGES CXX)

add_compile_definitions(KAON_APP_VERSION="${PROJECT_VERSION}"_L1)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 23)

find_package(Qt6 REQUIRED COMPONENTS Concurrent Core Quick Widgets Sql)

qt_standard_project_setup(REQUIRES 6.10)

include(FetchContent)
FetchContent_Declare(ValveFileVDF URL https://github.com/TinyTinni/ValveFileVDF/archive/refs/tags/v1.1.1.tar.gz)
FetchContent_MakeAvailable(ValveFileVDF)

# Headset view. Both are on unless a build wants to skip the dependency.
# OpenVR is the SteamVR dashboard tab. OpenXR is the WiVRn and Monado window.
option(KAON_OPENVR "SteamVR dashboard overlay" ON)
option(KAON_OPENXR "OpenXR headset view for WiVRn and Monado" ON)

# OpenVR dashboard overlay (SteamVR tab). Only the small API stub is built: it
# loads vrclient from the SteamVR install at runtime, so Kaon still starts when
# SteamVR is not installed. The stub is fetched as individual files to avoid
# cloning the whole SDK.
if(KAON_OPENVR)
set(OPENVR_VERSION v2.15.6)
set(OPENVR_SRC_DIR ${CMAKE_BINARY_DIR}/_deps/openvr-stub-src)
set(OPENVR_FILES
    headers/openvr.h
    src/openvr_api_public.cpp
    src/ivrclientcore.h
    src/jsoncpp.cpp
    src/json/json.h
    src/json/json-forwards.h
    src/vrcore/dirtools_public.cpp
    src/vrcore/dirtools_public.h
    src/vrcore/envvartools_public.cpp
    src/vrcore/envvartools_public.h
    src/vrcore/hmderrors_public.cpp
    src/vrcore/hmderrors_public.h
    src/vrcore/pathtools_public.cpp
    src/vrcore/pathtools_public.h
    src/vrcore/sharedlibtools_public.cpp
    src/vrcore/sharedlibtools_public.h
    src/vrcore/strtools_public.cpp
    src/vrcore/strtools_public.h
    src/vrcore/vrpathregistry_public.cpp
    src/vrcore/vrpathregistry_public.h)
foreach(OPENVR_FILE IN LISTS OPENVR_FILES)
    if(NOT EXISTS ${OPENVR_SRC_DIR}/${OPENVR_FILE})
        message(STATUS "Downloading openvr ${OPENVR_FILE}")
        file(DOWNLOAD
            https://raw.githubusercontent.com/ValveSoftware/openvr/${OPENVR_VERSION}/${OPENVR_FILE}
            ${OPENVR_SRC_DIR}/${OPENVR_FILE}
            SHOW_PROGRESS)
    endif()
endforeach()
add_library(openvr_api STATIC
    ${OPENVR_SRC_DIR}/src/openvr_api_public.cpp
    ${OPENVR_SRC_DIR}/src/jsoncpp.cpp
    ${OPENVR_SRC_DIR}/src/vrcore/dirtools_public.cpp
    ${OPENVR_SRC_DIR}/src/vrcore/envvartools_public.cpp
    ${OPENVR_SRC_DIR}/src/vrcore/hmderrors_public.cpp
    ${OPENVR_SRC_DIR}/src/vrcore/pathtools_public.cpp
    ${OPENVR_SRC_DIR}/src/vrcore/sharedlibtools_public.cpp
    ${OPENVR_SRC_DIR}/src/vrcore/strtools_public.cpp
    ${OPENVR_SRC_DIR}/src/vrcore/vrpathregistry_public.cpp)
target_include_directories(openvr_api PUBLIC
    ${OPENVR_SRC_DIR}/headers
    ${OPENVR_SRC_DIR}/src
    ${OPENVR_SRC_DIR}/src/vrcore)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|ARM64")
    target_compile_definitions(openvr_api PRIVATE LINUX POSIX LINUXARM64 VR_API_PUBLIC VRCORE_NO_PLATFORM)
else()
    target_compile_definitions(openvr_api PRIVATE LINUX POSIX LINUX64 VR_API_PUBLIC VRCORE_NO_PLATFORM)
endif()
endif()

add_subdirectory(src)

install(FILES "dev.lorendb.kaon.desktop" DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications" COMPONENT kaon)
install(FILES "src/qml/icons/kaon.svg" DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps" COMPONENT kaon)
