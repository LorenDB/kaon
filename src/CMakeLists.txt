set(CMAKE_AUTOMOC ON)

qt_add_executable(kaon
    main.cpp
)

qt_add_qml_module(kaon
    URI dev.lorendb.kaon
    VERSION 1.0
    QML_FILES
        DotnetDownloadDialog.qml
        GameDetails.qml
        Main.qml
        SteamGrid.qml
    SOURCES
        Dotnet.cpp
        Dotnet.h
        DownloadManager.cpp
        DownloadManager.h
        Game.cpp
        Game.h
        Steam.cpp
        Steam.h
        UEVR.cpp
        UEVR.h
)

target_link_libraries(kaon
    PRIVATE
        Qt6::Core
        Qt6::Quick
        Qt6::Widgets
        sourcepp::kvpp
)

include(GNUInstallDirs)
install(TARGETS kaon
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
