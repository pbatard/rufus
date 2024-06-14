set(target_name libcdio-iso9660)
file(GLOB source_files *.c *.h)
add_library(${target_name} STATIC ${source_files})

target_include_directories(${target_name} PRIVATE
                           ${CMAKE_SOURCE_DIR}/src
                           ${CMAKE_SOURCE_DIR}/src/libcdio
                           ${CMAKE_SOURCE_DIR}/src/libcdio/driver
                           ${CMAKE_SOURCE_DIR}/src/msvc-missing)

target_compile_definitions(${target_name} PRIVATE
                           HAVE_CONFIG_H
                           _OFF_T_DEFINED
                           _off_t=__int64
                           off_t=_off_t
                           _FILE_OFFSET_BITS=64
                           _CRTDBG_MAP_ALLOC)
