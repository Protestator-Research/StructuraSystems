cmake_minimum_required(VERSION 3.15)

project(StructuraSystems CXX C)

include(CMakeDependentOption)

option(RUN_TESTS "Build the tests" ON)

if(APPLE)
    list(APPEND CMAKE_PREFIX_PATH "~/Qt/6.11.1/macos/")
endif(APPLE)
if(WIN32)
    list(APPEND CMAKE_PREFIX_PATH "C:/Qt/6.11.1/msvc2022_64")
endif(WIN32)

# With Conan the Qt tools (rcc, qmlimportscanner, qmlcachegen, qtpaths, ...) of the target Qt cannot start, because
# their ICU libraries are not on the library path. The host Qt (QT_HOST_PATH, set by the Conan build environment)
# ships working tools, so the Qt6:: tool targets are defined to point there before the Qt6 config defines them.
# The path is cached so that a later re-configure without the Conan environment keeps using the host tools.
if(DEFINED ENV{QT_HOST_PATH})
    set(STRUCTURA_QT_HOST_PATH "$ENV{QT_HOST_PATH}" CACHE PATH "Qt installation whose tools are used for the build" FORCE)
endif()
if(STRUCTURA_QT_HOST_PATH)
    file(GLOB QT_HOST_TOOLS "${STRUCTURA_QT_HOST_PATH}/bin/*" "${STRUCTURA_QT_HOST_PATH}/libexec/*")
    foreach(tool_path IN LISTS QT_HOST_TOOLS)
        get_filename_component(tool_name "${tool_path}" NAME)
        if(NOT IS_DIRECTORY "${tool_path}" AND NOT tool_name MATCHES "\\." AND NOT TARGET Qt6::${tool_name})
            add_executable(Qt6::${tool_name} IMPORTED)
            set_target_properties(Qt6::${tool_name} PROPERTIES IMPORTED_LOCATION "${tool_path}")
        endif()
    endforeach()
    # QtKeychain caches the qtpaths location, so an older cache entry pointing to the target Qt is replaced.
    if(TARGET Qt6::qtpaths)
        get_target_property(QT_HOST_QTPATHS Qt6::qtpaths IMPORTED_LOCATION)
        set(QUERY_EXECUTABLE "${QT_HOST_QTPATHS}" CACHE FILEPATH "Location of the Qt6 qtpaths executable" FORCE)
    endif()
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Concurrent)

# QtKeychain stores the password in the OS keychain (Credential Manager, macOS Keychain, libsecret on Linux).
# It is always built as a static library and linked into the application, so no extra DLL has to be deployed.
include(FetchContent)
set(BUILD_WITH_QT6 ON)
set(BUILD_TRANSLATIONS OFF)
set(BUILD_TEST_APPLICATION OFF)
set(QTKEYCHAIN_BUILD_SHARED_BACKUP ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF)
# QtKeychain links Qt via the legacy Qt6Core_LIBRARIES / Qt6DBus_LIBRARIES variables, which the Conan
# generated Qt6 config does not define. Map them to the imported targets so includes and libraries are found.
if(NOT Qt6Core_LIBRARIES)
    set(Qt6Core_LIBRARIES Qt6::Core)
endif()
if(UNIX AND NOT APPLE AND NOT Qt6DBus_LIBRARIES)
    set(Qt6DBus_LIBRARIES Qt6::DBus)
endif()
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
    # EXCLUDE_FROM_ALL also keeps the QtKeychain install rules out of our install step.
    # SYSTEM (3.25+) silences warnings from its headers.
    FetchContent_Declare(qtkeychain
            GIT_REPOSITORY https://github.com/frankosterfeld/qtkeychain.git
            GIT_TAG 0.15.0
            GIT_SHALLOW TRUE
            SYSTEM
            EXCLUDE_FROM_ALL)
else()
    FetchContent_Declare(qtkeychain
            GIT_REPOSITORY https://github.com/frankosterfeld/qtkeychain.git
            GIT_TAG 0.15.0
            GIT_SHALLOW TRUE)
endif()
FetchContent_MakeAvailable(qtkeychain)
set(BUILD_SHARED_LIBS ${QTKEYCHAIN_BUILD_SHARED_BACKUP})
unset(QTKEYCHAIN_BUILD_SHARED_BACKUP)
# QtKeychain sets CMAKE_CXX_STANDARD to 11 and relies on Qt6::Core to raise it to C++17,
# which the Conan generated Qt6::Core target does not do.
set_target_properties(qt6keychain PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
if(NOT TARGET Qt6Keychain::Qt6Keychain)
    add_library(Qt6Keychain::Qt6Keychain ALIAS qt6keychain)
endif()

find_package(md4c REQUIRED)
find_package(sysmllib REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(Threads REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/out)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/out)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/out)

add_subdirectory(./stucturasystems/)