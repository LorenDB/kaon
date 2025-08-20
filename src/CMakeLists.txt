set(CMAKE_AUTOMOC ON)

qt_add_executable(kaon
    main.cpp
)

qt_add_qml_module(kaon
    URI dev.lorendb.kaon
    VERSION 1.0

    QML_FILES
        qml/DotnetDownloadDialog.qml
        qml/GameDetails.qml
        qml/GamesView.qml
        qml/Main.qml
        qml/SettingsPage.qml

        qml/components/SteamGridDelegate.qml
        qml/components/SteamListDelegate.qml
        qml/components/Tag.qml

    SOURCES
        Aptabase.cpp
        Aptabase.h
        Dotnet.cpp
        Dotnet.h
        DownloadManager.cpp
        DownloadManager.h
        Game.cpp
        Game.h
        GamesFilterModel.cpp
        GamesFilterModel.h
        UEVR.cpp
        UEVR.h
        UpdateChecker.cpp
        UpdateChecker.h
        VDF.cpp
        VDF.h

        stores/Itch.cpp
        stores/Itch.h
        stores/Steam.cpp
        stores/Steam.h

    RESOURCES
        icons/delete.svg
        icons/download.svg
        icons/draw-arrow-back.svg
        icons/folder.svg
        icons/kaon.svg
        icons/media-playback-start.svg
        icons/settings-configure.svg
)

# needed to make the QML module compilation find headers that aren't directly in src
target_include_directories(kaon PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/stores>)

target_link_libraries(kaon
    PRIVATE
        Qt6::Core
        Qt6::Quick
        Qt6::Widgets
        ValveFileVDF
)

include(GNUInstallDirs)
install(TARGETS kaon
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
