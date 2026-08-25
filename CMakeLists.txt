cmake_minimum_required(VERSION 3.21)
project(CrossTerm VERSION 0.5.3 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets)

if(WIN32)
    set(LRZSZ_SENDER_SOURCES
        third_party/lrzsz/src/lsz.c
        third_party/lrzsz/src/timing.c
        third_party/lrzsz/src/zperr.c
        third_party/lrzsz/src/zreadline.c
        third_party/lrzsz/src/crctab.c
        third_party/lrzsz/src/rbsb.c
        third_party/lrzsz/src/zm.c
        third_party/lrzsz/src/protname.c
        third_party/lrzsz/src/canit.c
        third_party/lrzsz/lib/error.c
        third_party/lrzsz/lib/long-options.c
        third_party/lrzsz/lib/xstrtol.c
        third_party/lrzsz/lib/xstrtoul.c
        third_party/lrzsz/lib/getopt.c
        third_party/lrzsz/lib/getopt1.c
        third_party/lrzsz/windows_stubs.c
    )
    add_library(CrossTermLrzszSender STATIC ${LRZSZ_SENDER_SOURCES})
    target_compile_definitions(CrossTermLrzszSender PRIVATE
        HAVE_CONFIG_H=1
        __STDC__=1
        main=crossterm_lrzsz_sender_main
    )
    target_include_directories(CrossTermLrzszSender PRIVATE
        third_party/lrzsz
        third_party/lrzsz/src
        third_party/lrzsz/lib
    )

    add_executable(CrossTermSz ${LRZSZ_SENDER_SOURCES})
    target_compile_definitions(CrossTermSz PRIVATE
        HAVE_CONFIG_H=1
        __STDC__=1
    )
    target_include_directories(CrossTermSz PRIVATE
        third_party/lrzsz
        third_party/lrzsz/src
        third_party/lrzsz/lib
    )
    set_target_properties(CrossTermSz PROPERTIES OUTPUT_NAME sz)

    set(LRZSZ_RECEIVER_SOURCES ${LRZSZ_SENDER_SOURCES})
    list(REMOVE_ITEM LRZSZ_RECEIVER_SOURCES third_party/lrzsz/src/lsz.c)
    list(APPEND LRZSZ_RECEIVER_SOURCES third_party/lrzsz/src/lrz.c)
    add_executable(CrossTermRz ${LRZSZ_RECEIVER_SOURCES})
    target_compile_definitions(CrossTermRz PRIVATE
        HAVE_CONFIG_H=1
        __STDC__=1
    )
    target_include_directories(CrossTermRz PRIVATE
        third_party/lrzsz
        third_party/lrzsz/src
        third_party/lrzsz/lib
    )
    set_target_properties(CrossTermRz PROPERTIES OUTPUT_NAME rz)
endif()

qt_add_executable(CrossTerm
    src/main.cpp
    resources.qrc
    src/MainWindow.h
    src/MainWindow.cpp
    src/connections/IConnection.h
    src/connections/WindowsPty.h
    src/connections/WindowsPty.cpp
    src/connections/LocalShellConnection.h
    src/connections/LocalShellConnection.cpp
    src/connections/SshConnection.h
    src/connections/SshConnection.cpp
    src/widgets/TerminalWidget.h
    src/widgets/TerminalWidget.cpp
    src/terminal/TerminalCell.h
    src/terminal/TerminalCell.cpp
    src/terminal/TerminalCursor.h
    src/terminal/TerminalScreen.h
    src/terminal/TerminalScreen.cpp
    src/terminal/TerminalParser.h
    src/terminal/TerminalParser.cpp
    src/transfer/ZModemProtocol.h
    src/transfer/ZModemProtocol.cpp
    src/transfer/ZModemCodec.h
    src/transfer/ZModemCodec.cpp
    src/profiles/ConnectionProfile.h
    src/profiles/ConnectionProfile.cpp
    src/profiles/ProfileManager.h
    src/profiles/ProfileManager.cpp
    src/dialogs/NewSessionDialog.h
    src/dialogs/NewSessionDialog.cpp
)

add_executable(TerminalParserTest
    tests/TerminalParserTest.cpp
    src/terminal/TerminalCell.cpp
    src/terminal/TerminalScreen.cpp
    src/terminal/TerminalParser.cpp
)

target_include_directories(TerminalParserTest PRIVATE src)
target_link_libraries(CrossTerm PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)
if(WIN32)
    target_link_libraries(CrossTerm PRIVATE CrossTermLrzszSender)
endif()
target_link_libraries(TerminalParserTest PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

# Link against util library for forkpty on Unix systems
if(NOT WIN32)
    target_link_libraries(CrossTerm PRIVATE util)
endif()

if(WIN32)
    set_target_properties(CrossTerm PROPERTIES WIN32_EXECUTABLE TRUE)
    target_sources(CrossTerm PRIVATE CrossTerm.rc)
endif()

install(TARGETS CrossTerm
    RUNTIME DESTINATION bin
    BUNDLE DESTINATION .
)
if(UNIX AND NOT APPLE)
    install(FILES assets/cross_term_logo.png
        DESTINATION share/icons/hicolor/512x512/apps
        RENAME crossterm.png
    )
    install(FILES packaging/linux/crossterm.desktop
        DESTINATION share/applications
    )
    install(PROGRAMS packaging/linux/install_linux.sh
        DESTINATION .
    )
endif()
install(FILES LICENSE DESTINATION .)
if(WIN32)
    install(TARGETS CrossTermSz CrossTermRz RUNTIME DESTINATION bin)
endif()
