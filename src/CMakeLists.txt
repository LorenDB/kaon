set(CMAKE_AUTOMOC ON)

qt_add_executable(kaon
    main.cpp
)

qt_add_qml_module(kaon
    URI dev.lorendb.kaon
    VERSION 1.0

    QML_FILES
        qml/AddCustomGame.qml
        qml/Dialogs.qml
        qml/GameDetails.qml
        qml/GamesView.qml
        qml/Main.qml
        qml/ModsList.qml
        qml/SettingsPage.qml

        qml/components/GameGridDelegate.qml
        qml/components/GameListDelegate.qml
        qml/components/Tag.qml

    SOURCES
        Aptabase.cpp
        Aptabase.h
        DownloadManager.cpp
        DownloadManager.h
        Game.cpp
        Game.h
        GamesFilterModel.cpp
        GamesFilterModel.h
        UpdateChecker.cpp
        UpdateChecker.h
        VDF.cpp
        VDF.h
        Wine.cpp
        Wine.h

        mods/Dotnet.cpp
        mods/Dotnet.h
        mods/Mod.cpp
        mods/Mod.h
        mods/ModsFilterModel.cpp
        mods/ModsFilterModel.h
        mods/UEVR.cpp
        mods/UEVR.h

        stores/CustomGames.cpp
        stores/CustomGames.h
        stores/Heroic.cpp
        stores/Heroic.h
        stores/Itch.cpp
        stores/Itch.h
        stores/Steam.cpp
        stores/Steam.h
        stores/Store.cpp
        stores/Store.h

    RESOURCES
        qml/icons/delete.svg
        qml/icons/download.svg
        qml/icons/draw-arrow-back.svg
        qml/icons/folder.svg
        qml/icons/kaon.svg
        qml/icons/list-add.svg
        qml/icons/media-playback-start.svg
        qml/icons/settings-configure.svg
        qml/icons/view-refresh.svg
)

# needed to make the QML module compilation find headers that aren't directly in src
target_include_directories(kaon PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/mods>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/stores>
)
target_precompile_headers(kaon PUBLIC pch.h)

target_link_libraries(kaon
    PRIVATE
        Qt6::Core
        Qt6::Quick
        Qt6::Widgets
        Qt6::Sql
        ValveFileVDF
)

include(GNUInstallDirs)
install(TARGETS kaon
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
