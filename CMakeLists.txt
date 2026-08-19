cmake_minimum_required(VERSION 3.21)
project(CrossTerm VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets)

qt_add_executable(CrossTerm
    src/main.cpp
    resources.qrc
    src/MainWindow.h
    src/MainWindow.cpp
    src/connections/IConnection.h
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
    src/profiles/ConnectionProfile.h
    src/profiles/ConnectionProfile.cpp
    src/profiles/ProfileManager.h
    src/profiles/ProfileManager.cpp
    src/dialogs/NewSessionDialog.h
    src/dialogs/NewSessionDialog.cpp
)

target_link_libraries(CrossTerm PRIVATE
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
endif()

install(TARGETS CrossTerm
    RUNTIME DESTINATION bin
    BUNDLE DESTINATION .
)
