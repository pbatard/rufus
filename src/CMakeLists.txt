add_all_subdirs()
set(target_name rufus)
file(GLOB source_files *.c *.h rufus.rc)
add_executable(${target_name} WIN32 ${source_files})

target_include_directories(${target_name} PRIVATE
                           .
                           msvc-missing
                           ms-sys/inc
                           syslinux/libinstaller
                           syslinux/libfat
                           syslinux/win
                           libcdio
                           getopt)

target_compile_definitions(${target_name} PRIVATE
                           _OFF_T_DEFINED
                           _off_t=__int64
                           off_t=_off_t
                           COBJMACROS
                           _CRTDBG_MAP_ALLOC)

target_link_libraries(${target_name} PRIVATE
                      bled
                      ext2fs
                      getopt
                      libcdio-driver
                      libcdio-iso9660
                      libcdio-udf
                      ms-sys
                      syslinux-libfat
                      syslinux-libinstaller
                      syslinux-win
                      comctl32
                      crypt32
                      dwmapi
                      setupapi
                      shlwapi
                      version
                      wintrust)
