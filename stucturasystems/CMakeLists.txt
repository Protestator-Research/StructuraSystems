cmake_minimum_required(VERSION 3.15)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Gui Charts)
find_package(md4c REQUIRED)
#find_package(sysmlv2lib REQUIRED)

qt_standard_project_setup()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC ON)

set(SOURCES src/main.cpp
            src/Widgets/StructuraMainWindow.cpp
            src/Widgets/StructuraMainWindow.h
        src/Models/MainWindowModel.cpp
        src/Models/MainWindowModel.h)

qt_wrap_ui(SOURCES ui/MainWindow.ui)

add_executable(StructuraSystems ${SOURCES})



target_include_directories(StructuraSystems PUBLIC
        "${CMAKE_CURRENT_BINARY_DIR}/")

target_link_libraries(StructuraSystems PUBLIC
        Qt6::Widgets
        Qt6::Gui
        Qt6::Charts
        Qt6::Core
        md4c::md4c
        #sysmlv2lib::sysmlv2lib
)

if (!UNIX)
set_target_properties(StructuraSystems PROPERTIES
        WIN32_EXECUTABLE ON
        MACOSX_BUNDLE ON
)
endif()

file(GLOB_RECURSE _source_list *.cpp* *.h* *.hpp*)
foreach(_source IN ITEMS ${_source_list})
    get_filename_component(_source_path "${_source}" PATH)
    string(REPLACE "${CMAKE_SOURCE_DIR}" "" _group_path "${_source_path}")
    string(REPLACE "/" "\\" _group_path "${_group_path}")
    source_group("${_group_path}" FILES "${_source}")
endforeach()


if(MSVC)
    target_compile_options(StructuraSystems PRIVATE /W4 /WX- /bigobj)
elseif (UNIX AND NOT APPLE)
    target_compile_options(StructuraSystems PRIVATE -Wall -Wextra -Wpedantic -Werror)
else ()
    target_compile_options(StructuraSystems PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()