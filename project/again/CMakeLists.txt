cmake_minimum_required(VERSION 3.16)
project(project LANGUAGES CXX)

find_package(Qt5 REQUIRED COMPONENTS Core Widgets)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)   # 👈 UIC 자동 실행
set(CMAKE_AUTORCC ON)

add_executable(project
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    weapon.h
    weapon.cpp   # 👈 반드시 추가
)

target_link_libraries(project
    PRIVATE Qt5::Core Qt5::Widgets
)
