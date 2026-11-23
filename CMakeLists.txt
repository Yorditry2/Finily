cmake_minimum_required(VERSION 3.20)
project(finily VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Pdf Svg)

add_executable(finily
    src/main.cpp
    src/core/Types.h
    src/engine/InkEngine.h
    src/engine/InkEngine.cpp
    src/engine/LiveStroke.h
    src/engine/LiveStroke.cpp
    src/engine/GeometryEngine.h
    src/engine/GeometryEngine.cpp
    src/engine/RegionGeometry.h
    src/engine/RegionGeometry.cpp
    src/engine/HandwritingEngine.h
    src/engine/HandwritingEngine.cpp
    src/engine/LassoEngine.h
    src/engine/LassoEngine.cpp
    src/engine/PdfEngine.h
    src/engine/PdfEngine.cpp
    src/ui/CanvasWidget.h
    src/ui/CanvasWidget.cpp
    src/ui/IconLibrary.h
    src/ui/IconLibrary.cpp
    src/ui/FloatingToolbar.h
    src/ui/FloatingToolbar.cpp
    src/ui/HomeScreen.h
    src/ui/HomeScreen.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
)

target_include_directories(finily PRIVATE src)
target_link_libraries(finily PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Pdf Qt6::Svg)
