cmake_minimum_required(VERSION 3.16)

project(iot_operator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

add_executable(iot_operator
    src/main.cpp
    src/network_thread.cpp
    src/network_thread.h
    src/gui.cpp
    src/gui.h
)

target_link_libraries(iot_operator PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)

if (WIN32)
    target_link_libraries(iot_operator PRIVATE ws2_32)
endif()
