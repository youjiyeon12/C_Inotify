cmake_minimum_required(VERSION 3.16)
project(inotify_tracker LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 핵심 기능 (inotify 감시 + 변경사항 추적): Qt 없이도 빌드된다
add_library(tracker_core STATIC src/Watcher.cpp src/Tracker.cpp)
target_include_directories(tracker_core PUBLIC src)

add_executable(core_test tests/core_test.cpp)
target_link_libraries(core_test PRIVATE tracker_core)
enable_testing()
add_test(NAME core_test COMMAND core_test)

# GUI: Qt6가 있으면 Qt6, 없으면 Qt5
find_package(QT NAMES Qt6 Qt5 COMPONENTS Widgets)
if(QT_FOUND)
  set(CMAKE_AUTOMOC ON)
  find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)
  add_executable(inotify_tracker src/main.cpp src/MainWindow.cpp src/CharacterStage.cpp)
  target_link_libraries(inotify_tracker PRIVATE tracker_core Qt${QT_VERSION_MAJOR}::Widgets)
else()
  message(WARNING "Qt를 못 찾아서 GUI는 빼고 core_test만 빌드합니다 (sudo apt install qt6-base-dev)")
endif()
