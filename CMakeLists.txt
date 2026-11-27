cmake_minimum_required(VERSION 3.20)
project(finily VERSION 0.2.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

option(FINILY_BUILD_TESTS "Build automated unit tests" ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Network Widgets Pdf Svg)

add_library(finily_engine STATIC
    src/core/Types.h
    src/core/Utils.h
    src/core/ShapeProtocols.h
    src/engine/DocumentSerializer.h
    src/engine/DocumentSerializer.cpp
    src/engine/LibraryStore.h
    src/engine/LibraryStore.cpp
    src/engine/LibraryFolders.cpp
    src/engine/LibraryTrash.cpp
    src/engine/LibraryMetadata.cpp
    src/engine/NotebookDocument.h
    src/engine/NotebookDocument.cpp
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
    src/engine/StickerCatalogInternal.h
    src/engine/StickerCatalogGeometry.cpp
    src/engine/StickerCatalogScience.cpp
    src/engine/StickerLibrary.cpp
    src/engine/UpdateManager.h
    src/engine/UpdateManager.cpp
    src/engine/ImageVectorizer.h
    src/engine/ImageVectorizer.cpp
    src/engine/VisionExtractor.h
    src/engine/VisionExtractor.cpp
)

target_include_directories(finily_engine PUBLIC src)
target_link_libraries(finily_engine PUBLIC Qt6::Core Qt6::Gui Qt6::Network Qt6::Pdf Qt6::Svg)

add_executable(finily
    src/main.cpp
    src/ui/CanvasWidget.h
    src/ui/CanvasWidgetInternal.h
    src/ui/CanvasWidget.cpp
    src/ui/CanvasDocument.cpp
    src/ui/CanvasInput.cpp
    src/ui/CanvasRender.cpp
    src/ui/CanvasSelection.cpp
    src/ui/IconLibrary.h
    src/ui/IconLibrary.cpp
    src/ui/FloatingToolbar.h
    src/ui/FloatingToolbarInternal.h
    src/ui/ToolbarIcons.h
    src/ui/ToolbarIcons.cpp
    src/ui/FloatingToolbar.cpp
    src/ui/ToolbarPopovers.cpp
    src/ui/popovers/FilePopover.h
    src/ui/popovers/FilePopover.cpp
    src/ui/popovers/PaperPopover.h
    src/ui/popovers/PaperPopover.cpp
    src/ui/popovers/ShapesPopover.h
    src/ui/popovers/ShapesPopover.cpp
    src/ui/popovers/StickersPopover.h
    src/ui/popovers/StickersPopover.cpp
    src/ui/popovers/StrokePopover.h
    src/ui/popovers/StrokePopover.cpp
    src/ui/popovers/ThumbnailPanel.h
    src/ui/popovers/ThumbnailPanel.cpp
    src/ui/DesignTokens.h
    src/ui/UiIcons.h
    src/ui/UiIcons.cpp
    src/ui/home/HomeTheme.h
    src/ui/home/HomeTheme.cpp
    src/ui/home/HomeMotion.h
    src/ui/home/HomeMotion.cpp
    src/ui/home/HomeIcons.cpp
    src/ui/home/HomeSidebar.h
    src/ui/home/HomeSidebar.cpp
    src/ui/home/HomeHeader.h
    src/ui/home/HomeHeader.cpp
    src/ui/home/LibraryObjects.h
    src/ui/home/LibraryObjects.cpp
    src/ui/home/ObjectCard.h
    src/ui/home/ObjectCard.cpp
    src/ui/home/LibraryViews.h
    src/ui/home/LibraryViews.cpp
    src/ui/home/SettingsView.h
    src/ui/home/SettingsView.cpp
    src/ui/home/FolderCards.h
    src/ui/home/FolderCards.cpp
    src/ui/home/NotebookCards.h
    src/ui/home/NotebookCards.cpp
    src/ui/home/CreateMenu.h
    src/ui/home/CreateMenu.cpp
    src/ui/home/HomeMenu.h
    src/ui/home/HomeMenu.cpp
    src/ui/home/TrashCards.h
    src/ui/home/TrashCards.cpp
    src/ui/home/SheetWidgets.h
    src/ui/home/SheetWidgets.cpp
    src/ui/home/GlassSheet.h
    src/ui/home/GlassSheet.cpp
    src/ui/home/NewFolderSheet.h
    src/ui/home/NewFolderSheet.cpp
    src/ui/home/BoardColorPopover.h
    src/ui/home/BoardColorPopover.cpp
    src/ui/home/NewBoardSheet.h
    src/ui/home/NewBoardSheet.cpp
    src/ui/home/NewNotebookSheet.h
    src/ui/home/NewNotebookSheet.cpp
    src/ui/home/MoveToSheet.h
    src/ui/home/MoveToSheet.cpp
    src/ui/home/UpdateBanner.h
    src/ui/home/UpdateBanner.cpp
    src/ui/HomeScreen.h
    src/ui/HomeScreen.cpp
    src/ui/HomeScreenActions.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
)

qt_add_resources(finily "finily_fonts"
    PREFIX "/"
    FILES resources/fonts/InterVariable.ttf
)
file(GLOB FINILY_ICON_FILES RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/*/*.svg")
qt_add_resources(finily "finily_icons"
    PREFIX "/"
    FILES ${FINILY_ICON_FILES}
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
    macro(finily_add_test name source)
        add_executable(${name} ${source})
        target_link_libraries(${name} PRIVATE finily_engine Qt6::Test)
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(${name} PRIVATE -Wall -Wextra)
        elseif(MSVC)
            target_compile_options(${name} PRIVATE /utf-8)
        endif()
        add_test(NAME ${name} COMMAND ${name})
        set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endmacro()
    finily_add_test(test_ink_and_live tests/test_ink_and_live.cpp)
    finily_add_test(test_geometry_and_regions tests/test_geometry_and_regions.cpp)
    finily_add_test(test_lasso tests/test_lasso.cpp)
    finily_add_test(test_handwriting tests/test_handwriting.cpp)
    finily_add_test(test_document_and_pdf tests/test_document_and_pdf.cpp)
    finily_add_test(test_library_store tests/test_library_store.cpp)
    finily_add_test(test_vision_and_updates tests/test_vision_and_updates.cpp)
endif()
