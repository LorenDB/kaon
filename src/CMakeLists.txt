set(CMAKE_AUTOMOC ON)

qt_add_executable(kaon
    main.cpp
)

# Theme and Nav are QML singletons
set_source_files_properties(qml/Theme.qml qml/Nav.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(kaon
    URI dev.lorendb.kaon
    VERSION 1.0

    QML_FILES
        qml/Dialogs.qml
        qml/Main.qml
        qml/Nav.qml
        qml/Theme.qml

        qml/components/Chip.qml
        qml/components/GlassDialog.qml
        qml/components/GlassScrollBar.qml
        qml/components/Icon.qml
        qml/components/LaunchOptions.qml
        qml/components/Led.qml
        qml/components/NotchButton.qml
        qml/components/RoundedImage.qml
        qml/components/Segmented.qml
        qml/components/TextBox.qml
        qml/components/ToggleSwitch.qml
        qml/components/VButton.qml
        qml/components/VisorFrame.qml
        qml/components/VText.qml

        qml/game/GameView.qml
        qml/game/LensView.qml
        qml/game/VersionMenu.qml

        qml/library/FilterPopup.qml
        qml/library/GameCard.qml
        qml/library/LibraryView.qml

        qml/mods/ModsView.qml

        qml/settings/AddGameView.qml
        qml/settings/SettingsView.qml

    SOURCES
        Aptabase.cpp
        Aptabase.h
        DownloadManager.cpp
        DownloadManager.h
        Game.cpp
        Game.h
        GameExecutablePickerModel.cpp
        GameExecutablePickerModel.h
        GamesFilterModel.cpp
        GamesFilterModel.h
        GameStatus.cpp
        GameStatus.h
        Launcher.cpp
        Launcher.h
        UpdateChecker.cpp
        UpdateChecker.h
        VDF.cpp
        VDF.h
        Wine.cpp
        Wine.h
        Flatpak.cpp
        Flatpak.h

        mods/Dotnet.cpp
        mods/Dotnet.h
        mods/GitHubMod.cpp
        mods/GitHubMod.h
        mods/Mod.cpp
        mods/Mod.h
        mods/ModsFilterModel.cpp
        mods/ModsFilterModel.h
        mods/Portal1VR.cpp
        mods/Portal1VR.h
        mods/Portal1VRInstall.cpp
        mods/Portal1VRInstall.h
        mods/Portal2VR.cpp
        mods/Portal2VR.h
        mods/UEVR.cpp
        mods/UEVR.h
        mods/UEVRAFW.cpp
        mods/UEVRAFW.h

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
        qml/fonts/MPLUSRounded1c-Bold.ttf
        qml/fonts/MPLUSRounded1c-ExtraBold.ttf
        qml/fonts/MPLUSRounded1c-Medium.ttf
        qml/fonts/MPLUSRounded1c-Regular.ttf
        qml/fonts/OFL.txt
        qml/icons/kaon.svg
)

if (EXPERIMENTAL_UUVR_SUPPORT)
    add_compile_definitions(EXPERIMENTAL_UUVR_SUPPORT)
    target_sources(kaon PRIVATE
        mods/Bepinex.cpp
        mods/Bepinex.h
        mods/BepInExConfigManager.cpp
        mods/BepInExConfigManager.h
        mods/UUVR.cpp
        mods/UUVR.h
    )
endif()

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
