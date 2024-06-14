set(target_name getopt)
file(GLOB source_files *.c *.h)
add_library(${target_name} STATIC ${source_files})
target_compile_definitions(${target_name} PRIVATE HAVE_STRING_H)
