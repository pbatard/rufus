/*
 * This file contains the version string of the GRUB 2.x binary embedded in Rufus.
 * Should be the same as GRUB's PACKAGE_VERSION in config.h.
 */
#pragma once

#define GRUB2_PACKAGE_VERSION "2.06"

/*
 * Also include the 'core.img' patch data for distros using a 
 * NONSTANDARD '/boot/grub2/' prefix directory (openSUSE, Gecko, ...)
 * This is basically a diff of the 'core.img' generated with
 * 'grub-mkimage -p/boot/grub' vs 'grub-mkimage -p/boot/grub2'
 */
 
// For GRUB 2.04
static const chunk_t grub_204_chunk1_src = { 0x0208, 1, { 0xce } };
static const chunk_t grub_204_chunk1_rep = { 0x0208, 1, { 0xcf } };
static const chunk_t grub_204_chunk2_src = { 0x7cf8, 6, { 0x63, 0x25, 0x7e, 0x04, 0xf6, 0x14 } };
static const chunk_t grub_204_chunk2_rep = { 0x7cf8, 7, { 0x62, 0xdb, 0x77, 0x57, 0x0c, 0x4e, 0x00 } };

// For GRUB 2.06
static const chunk_t grub_206_chunk1_src = { 0x0208, 1, { 0xcf } };
static const chunk_t grub_206_chunk1_rep = { 0x0208, 1, { 0xd0 } };
static const chunk_t grub_206_chunk2_src = { 0x95f9, 6, { 0xac, 0x1a, 0xc6, 0x4f, 0x45, 0x2c } };
static const chunk_t grub_206_chunk2_rep = { 0x95f9, 7, { 0xab, 0xe7, 0xe4, 0x0a, 0x2e, 0x38, 0x00 } };

const grub_patch_t grub_patch[2] = {
	{	"2.04", {
			{ &grub_204_chunk1_src, &grub_204_chunk1_rep },
			{ &grub_204_chunk2_src, &grub_204_chunk2_rep },
		}
	},
	{	"2.06", {
			{ &grub_206_chunk1_src, &grub_206_chunk1_rep },
			{ &grub_206_chunk2_src, &grub_206_chunk2_rep },
		}
	},
};
