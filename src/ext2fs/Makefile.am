noinst_LIBRARIES = libext2fs.a

libext2fs_a_SOURCES = alloc.c alloc_sb.c alloc_stats.c alloc_tables.c badblocks.c bb_inode.c       \
	bitmaps.c bitops.c blkmap64_ba.c blkmap64_rb.c blknum.c block.c bmap.c closefs.c crc16.c         \
	crc32c.c csum.c dirblock.c dir_iterate.c extent.c ext_attr.c extent.c fallocate.c fileio.c       \
	freefs.c gen_bitmap.c gen_bitmap64.c get_num_dirs.c hashmap.c i_block.c ind_block.c initialize.c \
	inline.c inline_data.c inode.c io_manager.c link.c lookup.c mkdir.c mkjournal.c namei.c mmp.c    \
	newdir.c nt_io.c openfs.c punch.c rbtree.c read_bb.c rw_bitmaps.c sha512.c symlink.c valid_blk.c

libext2fs_a_CFLAGS = $(AM_CFLAGS) -DEXT2_FLAT_INCLUDES=0 -DHAVE_CONFIG_H -I$(srcdir) -I$(srcdir)/.. -Wno-undef -Wno-strict-aliasing -Wno-shadow
