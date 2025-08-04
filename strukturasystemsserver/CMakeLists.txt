cmake_minimum_required(VERSION 3.15)
find_package(Qt6 REQUIRED COMPONENTS Core HttpServer)
find_package(sysmllib REQUIRED)

qt_standard_project_setup()

set(CMAKE_AUTOMOC ON)

set(SOURCES
        src/main.cpp)

add_executable(StructuraSystemsServer ${SOURCES})



target_include_directories(StructuraSystemsServer PUBLIC
        "${CMAKE_CURRENT_BINARY_DIR}/")

target_link_libraries(StructuraSystemsServer PUBLIC
        Qt6::Core
    	Qt6::HttpServer
        sysmllib::sysmllib)

if (!UNIX)
set_target_properties(StructuraSystemsServer PROPERTIES
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
    target_compile_options(StructuraSystemsServer PRIVATE /W4 /WX- /bigobj)
elseif (UNIX AND NOT APPLE)
    target_compile_options(StructuraSystemsServer PRIVATE -Wall -Wextra -Wpedantic -Werror)
else ()
    target_compile_options(StructuraSystemsServer PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()