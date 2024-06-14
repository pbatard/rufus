cmake_minimum_required(VERSION 3.16)

project(rufus C)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(CMAKE_BINARY_DIR MATCHES "^${CMAKE_SOURCE_DIR}")
    message(WARNING "In-source build detected")
endif()

if(MSVC)
    add_compile_options(/utf-8)
endif()

function(add_all_subdirs)
    file(GLOB children RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} *)

    foreach(child ${children})
        if(NOT IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${child})
            continue()
        endif()

        if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${child}/CMakeLists.txt)
            continue()
        endif()

        add_subdirectory(${child} ${ARGN})
    endforeach()
endfunction()

add_all_subdirs()
