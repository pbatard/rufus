set(target_name syslinux-libfat)
file(GLOB source_files *.c *.h)
add_library(${target_name} STATIC ${source_files})

target_include_directories(${target_name} PRIVATE
                           ${CMAKE_SOURCE_DIR}/src
                           ${CMAKE_SOURCE_DIR}/src/msvc-missing)

target_compile_definitions(${target_name} PRIVATE
                           inline=__inline
                           _CRTDBG_MAP_ALLOC)
