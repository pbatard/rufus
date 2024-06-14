set(target_name bled)
file(GLOB source_files *.c *.h)
add_library(${target_name} STATIC ${source_files})
target_include_directories(${target_name} PRIVATE ..)

target_compile_definitions(${target_name} PRIVATE
                           _OFF_T_DEFINED
                           _off_t=__int64
                           off_t=_off_t
                           _FILE_OFFSET_BITS=64
                           _CRTDBG_MAP_ALLOC)
