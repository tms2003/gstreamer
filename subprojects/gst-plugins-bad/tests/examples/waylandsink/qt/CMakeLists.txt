cmake_minimum_required(VERSION 3.14)

project(waylandsink LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PROJECT_SOURCES
        main.cpp
        videowidget.cpp videowidget.h
        )

add_executable(waylandsink ${PROJECT_SOURCES})

find_package(PkgConfig REQUIRED)

# GStreamer dependencies
pkg_search_module(GSTREAMER1.0 gstreamer-1.0)
pkg_search_module(GSTREAMER1.0_VIDEO gstreamer-video-1.0)
pkg_search_module(GSTREAMER1.0_WAYLAND gstreamer-wayland-1.0)

include_directories(
        ${GSTREAMER1.0_INCLUDE_DIRS}
)

target_link_libraries(waylandsink
        ${GSTREAMER1.0_LIBRARIES}
        ${GSTREAMER1.0_VIDEO_LIBRARIES}
        ${GSTREAMER1.0_WAYLAND_LIBRARIES}
        )

# Qt dependencies
find_package(QT NAMES Qt5 COMPONENTS Core Widgets REQUIRED)
find_package(Qt5 COMPONENTS Core Widgets REQUIRED)
find_package(Qt5Gui REQUIRED)

include_directories(
        ${Qt5Core_INCLUDE_DIRS}
        ${Qt5Gui_PRIVATE_INCLUDE_DIRS}
        ${Qt5Widgets_INCLUDE_DIRS}
)

target_link_libraries(waylandsink Qt5::Core Qt5::Widgets)
