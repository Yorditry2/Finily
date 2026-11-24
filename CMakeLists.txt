cmake_minimum_required(VERSION 3.20)
project(finily VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

option(FINILY_BUILD_TESTS "Build automated unit tests" ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Pdf Svg)

add_library(finily_engine STATIC
    src/core/Types.h
    src/engine/DocumentSerializer.h
    src/engine/DocumentSerializer.cpp
    src/engine/InkEngine.h
    src/engine/InkEngine.cpp
    src/engine/LiveStroke.h
    src/engine/LiveStroke.cpp
    src/engine/GeometryEngine.h
    src/engine/GeometryInternal.h
    src/engine/GeometryEngine.cpp
    src/engine/GeometryEditing.cpp
    src/engine/RegionGeometry.h
    src/engine/RegionGeometry.cpp
    src/engine/HandwritingEngine.h
    src/engine/HandwritingEngine.cpp
    src/engine/LassoEngine.h
    src/engine/LassoEngine.cpp
    src/engine/PdfEngine.h
    src/engine/PdfEngine.cpp
    src/engine/StickerLibrary.h
    src/engine/StickerLibrary.cpp
)

target_include_directories(finily_engine PUBLIC src)
target_link_libraries(finily_engine PUBLIC Qt6::Core Qt6::Gui Qt6::Pdf Qt6::Svg)

add_executable(finily
    src/main.cpp
    src/ui/CanvasWidget.h
    src/ui/CanvasWidgetInternal.h
    src/ui/CanvasWidget.cpp
    src/ui/CanvasDocument.cpp
    src/ui/CanvasRender.cpp
    src/ui/IconLibrary.h
    src/ui/IconLibrary.cpp
    src/ui/FloatingToolbar.h
    src/ui/FloatingToolbarInternal.h
    src/ui/ToolbarIcons.h
    src/ui/ToolbarIcons.cpp
    src/ui/FloatingToolbar.cpp
    src/ui/ToolbarPopovers.cpp
    src/ui/ToolbarThumbnails.cpp
    src/ui/HomeScreen.h
    src/ui/HomeScreen.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
)

target_compile_definitions(finily PRIVATE FINILY_VERSION="${PROJECT_VERSION}")
target_link_libraries(finily PRIVATE finily_engine Qt6::Widgets)

if(WIN32)
    set_target_properties(finily PROPERTIES WIN32_EXECUTABLE TRUE)
    install(TARGETS finily RUNTIME DESTINATION .)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/finily-version.txt" "${PROJECT_VERSION}\n")
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(finily_engine PRIVATE -Wall -Wextra)
    target_compile_options(finily PRIVATE -Wall -Wextra)
elseif(MSVC)
    target_compile_options(finily_engine PRIVATE /utf-8)
    target_compile_options(finily PRIVATE /utf-8)
endif()

if(FINILY_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(finily_tests tests/test_engines.cpp)
    target_link_libraries(finily_tests PRIVATE finily_engine Qt6::Test)
    add_test(NAME finily_tests COMMAND finily_tests)
    set_tests_properties(finily_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
