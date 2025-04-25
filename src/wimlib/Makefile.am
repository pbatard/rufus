noinst_LIBRARIES = libwim.a
libwim_a_SOURCES = avl_tree.c blob_table.c compress.c compress_common.c compress_parallel.c \
	compress_serial.c cpu_features.c decompress.c decompress_common.c dentry.c divsufsort.c \
	encoding.c error.c export_image.c extract.c file_io.c header.c inode.c inode_fixup.c \
	inode_table.c integrity.c iterate_dir.c lcpit_matchfinder.c lzms_common.c lzms_compress.c \
	lzms_decompress.c lzx_common.c lzx_compress.c lzx_decompress.c metadata_resource.c \
	pathlist.c paths.c pattern.c progress.c registry.c reparse.c resource.c scan.c security.c \
	sha1.c solid.c split.c tagged_items.c textfile.c threads.c timestamp.c update_image.c \
	util.c wim.c wimboot.c win32_apply.c win32_capture.c win32_common.c win32_replacements.c \
	win32_vss.c write.c xml.c xmlproc.c xml_windows.c xpress_compress.c xpress_decompress.c
libwim_a_CFLAGS = $(AM_CFLAGS) -I$(srcdir)/.. -I$(srcdir)/../libcdio -DHAVE_CONFIG_H -D_RUFUS -D__SSE2__ -D_POSIX -D_POSIX_THREAD_SAFE_FUNCTIONS -DUNICODE -D_UNICODE -D__MINGW_USE_VC2005_COMPAT -Wno-undef -Wno-strict-aliasing -Wno-shadow -Wno-incompatible-pointer-types -Wno-sequence-point
