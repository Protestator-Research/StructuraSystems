cmake_minimum_required(VERSION 3.15)
find_package(Qt6 REQUIRED COMPONENTS Core HttpServer)
find_package(sysmllib REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(mongocxx REQUIRED)

qt_standard_project_setup()

set(CMAKE_AUTOMOC ON)

set(SOURCES
        src/main.cpp
        src/Controller/BaseController.hpp
        src/Controller/ProjectController.hpp
        src/Controller/DataBaseController.h
        src/Controller/DataBaseController.cpp
        src/Controller/AuthenticationController.hpp
        src/Controller/ElementsController.hpp
        src/Controller/CommitController.hpp
        src/Templates/CrudApi.hpp
        src/Templates/IdMap.hpp
        src/Templates/SessionAPI.hpp
        src/Json/Jsonable.hpp
        src/Json/Updatable.hpp
        src/Json/FromJsonFactory.hpp
        src/Json/entities/Project.h
        src/Json/entities/Project.cpp
        src/Json/entities/AuthenticationRequest.h
        src/Json/entities/AuthenticationRequest.cpp
        src/Json/entities/AuthenticationResponse.h
        src/Json/entities/AuthenticationResponse.cpp
        src/Services/ServerProjectService.h
        src/Services/ServerProjectService.cpp
        src/Services/ElementNavigationService.h
        src/Services/ElementNavigationService.cpp
        src/Services/ProjectVersioningService.h
        src/Services/ProjectVersioningService.cpp
        src/Services/AuthenticationService.h
        src/Services/AuthenticationService.cpp
        ../resources/sysml.qrc)

add_executable(StructuraSystemsServer ${SOURCES})



target_include_directories(StructuraSystemsServer PUBLIC
        "${CMAKE_CURRENT_BINARY_DIR}/")
if(TARGET mongo::mongocxx_shared)
    target_link_libraries(StructuraSystemsServer PUBLIC
            Qt6::Core
    	    Qt6::HttpServer
            sysmllib::sysmllib
            openssl::openssl
            mongo::mongocxx_shared)
else()
    target_link_libraries(StructuraSystemsServer PUBLIC
            Qt6::Core
            Qt6::HttpServer
            sysmllib::sysmllib
            openssl::openssl
            mongo::mongocxx_static)
endif()

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