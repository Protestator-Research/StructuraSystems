cmake_minimum_required(VERSION 3.15)

# Qt Test is used (not GTest): Qt is already a dependency of the project, and the code under test uses Qt types
# (QString, QDir, QFile). The tests only link the library resolution code, not the QML application.
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(SRC "${CMAKE_CURRENT_SOURCE_DIR}/../src")

# The Markdown parser (cmark) reads the code blocks of .md library files.
file(GLOB CMARK_SOURCES "${SRC}/Models/Parser/Markdown/CommonmarkParser/*.c")
list(FILTER CMARK_SOURCES EXCLUDE REGEX "main\\.c$")

qt_add_executable(LibraryWorkspaceTests
        tst_LibraryWorkspace.cpp
        ${SRC}/Models/Libraries/CodeScanner.cpp
        ${SRC}/Models/Libraries/CodeScanner.h
        ${SRC}/Models/Libraries/DocumentWorkspace.cpp
        ${SRC}/Models/Libraries/DocumentWorkspace.h
        ${SRC}/Models/Libraries/LibraryWorkspace.cpp
        ${SRC}/Models/Libraries/LibraryWorkspace.h
        ${SRC}/Models/Libraries/WorkspaceConfig.cpp
        ${SRC}/Models/Libraries/WorkspaceConfig.h
        ${SRC}/Models/Parser/Markdown/MarkdownParser.cpp
        ${CMARK_SOURCES}
        # The standard library that the workspaces load.
        ${CMAKE_CURRENT_SOURCE_DIR}/../../resources/sysml.qrc)

target_link_libraries(LibraryWorkspaceTests PRIVATE
        Qt6::Core
        Qt6::Test
        sysmllib::sysmllib)

add_test(NAME LibraryWorkspaceTests COMMAND LibraryWorkspaceTests)
# Parsing the standard library takes about a minute in a debug build.
set_tests_properties(LibraryWorkspaceTests PROPERTIES TIMEOUT 900)

# ctest starts the executable without the environment of the Conan build, so the shared libraries it needs (sysmllib and its
# dependencies, Qt) are put on the PATH of the test. The directories come from the variables the Conan CMake generator sets
# for every package (<package>_BIN_DIRS_<CONFIG>); only directories that exist are used.
if(WIN32 AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.22)
    # Only the directories of the configuration that runs the test (ctest -C <config>) are used.
    set(_modifications "")
    get_cmake_property(_all_variables VARIABLES)
    foreach(_variable IN LISTS _all_variables)
        if(_variable MATCHES "_BIN_DIRS_(DEBUG|RELEASE|RELWITHDEBINFO|MINSIZEREL)$")
            string(TOUPPER "${CMAKE_MATCH_1}" _config_key)
            set(_config_name "")
            if(_config_key STREQUAL "DEBUG")
                set(_config_name "Debug")
            elseif(_config_key STREQUAL "RELEASE")
                set(_config_name "Release")
            elseif(_config_key STREQUAL "RELWITHDEBINFO")
                set(_config_name "RelWithDebInfo")
            else()
                set(_config_name "MinSizeRel")
            endif()
            foreach(_dir IN LISTS ${_variable})
                if(IS_DIRECTORY "${_dir}")
                    list(APPEND _modifications "PATH=path_list_prepend:$<$<CONFIG:${_config_name}>:${_dir}>")
                endif()
            endforeach()
        endif()
    endforeach()
    # Qt: <prefix>/lib/cmake/Qt6 -> <prefix>/bin
    get_filename_component(_qt_bin "${Qt6_DIR}/../../../bin" ABSOLUTE)
    if(IS_DIRECTORY "${_qt_bin}")
        list(APPEND _modifications "PATH=path_list_prepend:${_qt_bin}")
    endif()
    list(REMOVE_DUPLICATES _modifications)
    set_tests_properties(LibraryWorkspaceTests PROPERTIES ENVIRONMENT_MODIFICATION "${_modifications}")
endif()
