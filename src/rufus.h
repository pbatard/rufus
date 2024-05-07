/*
 * Rufus: The Reliable USB Formatting Utility
 * Copyright © 2011-2024 Pete Batard <pete@akeo.ie>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <windows.h>
#include <malloc.h>
#include <inttypes.h>

#if defined(_MSC_VER)
// Disable some VS Code Analysis warnings
#pragma warning(disable: 4996)		// Ignore deprecated
#pragma warning(disable: 6258)		// I know what I'm using TerminateThread for
#pragma warning(disable: 26451)		// Stop bugging me with casts already!
#pragma warning(disable: 28159)		// I'll keep using GetVersionEx(), thank you very much...
// Enable C11's _Static_assert (requires VS2015 or later)
#define _Static_assert static_assert
#endif

#pragma once

/* Convenient to have around */
#define KB                          1024LL
#define MB                          1048576LL
#define GB                          1073741824LL
#define TB                          1099511627776LL

/*
 * Features not ready for prime time and that may *DESTROY* your data - USE AT YOUR OWN RISKS!
 */
//#define RUFUS_TEST

#define APPLICATION_NAME            "Rufus"
#if defined(_M_AMD64)
#define APPLICATION_ARCH            "x64"
#elif defined(_M_IX86)
#define APPLICATION_ARCH            "x86"
#elif defined(_M_ARM64)
#define APPLICATION_ARCH            "Arm64"
#elif defined(_M_ARM)
#define APPLICATION_ARCH            "Arm"
#else
#define APPLICATION_ARCH            "(Unknown Arch)"
#endif
#define COMPANY_NAME                "Akeo Consulting"
#define STR_NO_LABEL                "NO_LABEL"
// Yes, there exist characters between these seemingly empty quotes!
#define LEFT_TO_RIGHT_MARK          "‎"
#define RIGHT_TO_LEFT_MARK          "‏"
#define LEFT_TO_RIGHT_EMBEDDING     "‪"
#define RIGHT_TO_LEFT_EMBEDDING     "‫"
#define POP_DIRECTIONAL_FORMATTING  "‬"
#define LEFT_TO_RIGHT_OVERRIDE      "‭"
#define RIGHT_TO_LEFT_OVERRIDE      "‮"
#define DRIVE_ACCESS_TIMEOUT        15000		// How long we should retry drive access (in ms)
#define DRIVE_ACCESS_RETRIES        150			// How many times we should retry
#define DRIVE_INDEX_MIN             0x00000080
#define DRIVE_INDEX_MAX             0x000000C0
#define MIN_DRIVE_SIZE              (8 * MB)	// Minimum size a drive must have, to be formattable
#define MIN_EXTRA_PART_SIZE         (1 * MB)	// Minimum size of the extra partition, in bytes
#define MIN_EXT_SIZE                (256 * MB)	// Minimum size we allow for ext formatting
#define MAX_DRIVES                  (DRIVE_INDEX_MAX - DRIVE_INDEX_MIN)
#define MAX_TOOLTIPS                128
#define MAX_SIZE_SUFFIXES           6			// bytes, KB, MB, GB, TB, PB
#define MAX_CLUSTER_SIZES           18
#define MAX_PROGRESS                0xFFFF
#define PATCH_PROGRESS_TOTAL        207
#define MAX_LOG_SIZE                0x7FFFFFFE
#define MAX_REFRESH                 25			// How long we should wait to refresh UI elements (in ms)
#define MARQUEE_TIMER_REFRESH       10			// Time between progress bar marquee refreshes, in ms
#define MAX_GUID_STRING_LENGTH      40
#define MAX_PARTITIONS              16			// Maximum number of partitions we handle
#define MAX_ESP_TOGGLE              8			// Maximum number of entries we record to toggle GPT ESP back and forth
#define MAX_IGNORE_USB              8			// Maximum number of USB drives we want to ignore
#define MAX_ISO_TO_ESP_SIZE         (1 * GB)	// Maximum size we allow for the ISO → ESP option
#define MAX_DEFAULT_LIST_CARD_SIZE  (500 * GB)	// Size above which we don't list a card without enable HDD or Alt-F
#define MAX_SECTORS_TO_CLEAR        128			// nb sectors to zap when clearing the MBR/GPT (must be >34)
#define MAX_USERNAME_LENGTH         128			// Maximum size we'll accept for a WUE specified username
#define MAX_WININST                 4			// Max number of install[.wim|.esd] we can handle on an image
#define MBR_UEFI_MARKER             0x49464555	// 'U', 'E', 'F', 'I', as a 32 bit little endian longword
#define MORE_INFO_URL               0xFFFF
#define PROJECTED_SIZE_RATIO        110			// Percentage by which we inflate projected_size to prevent persistence overflow
#define STATUS_MSG_TIMEOUT          3500		// How long should cheat mode messages appear for on the status bar
#define WRITE_RETRIES               4
#define WRITE_TIMEOUT               5000		// How long we should wait between write retries (in ms)
#define SEARCH_PROCESS_TIMEOUT      5000		// How long we should wait to get the conflicting process data (in ms)
#define NET_SESSION_TIMEOUT         3500		// How long we should wait to connect, send or receive internet data (in ms)
#define FS_DEFAULT                  FS_FAT32
#define SINGLE_CLUSTERSIZE_DEFAULT  0x00000100
#define BADLOCKS_PATTERN_TYPES      5
#define BADBLOCK_PATTERN_COUNT      4
#define BADBLOCK_PATTERN_ONE_PASS   {0x55, 0x00, 0x00, 0x00}
#define BADBLOCK_PATTERN_TWO_PASSES {0x55, 0xaa, 0x00, 0x00}
#define BADBLOCK_PATTERN_SLC        {0x00, 0xff, 0x55, 0xaa}
#define BADCLOCK_PATTERN_MLC        {0x00, 0xff, 0x33, 0xcc}
#define BADBLOCK_PATTERN_TLC        {0x00, 0xff, 0x1c71c7, 0xe38e38}
#define BADBLOCK_BLOCK_SIZE         (512 * KB)
#define LARGE_FAT32_SIZE            (32 * GB)	// Size at which we need to use fat32format
#define UDF_FORMAT_SPEED            3.1f		// Speed estimate at which we expect UDF drives to be formatted (GB/s)
#define UDF_FORMAT_WARN             20			// Duration (in seconds) above which we warn about long UDF formatting times
#define MAX_FAT32_SIZE              (2 * TB)	// Threshold above which we disable FAT32 formatting
#define FAT32_CLUSTER_THRESHOLD     1.011f		// For FAT32, cluster size changes don't occur at power of 2 boundaries but slightly above
#define DD_BUFFER_SIZE              (32 * MB)	// Minimum size of buffer to use for DD operations
#define UBUFFER_SIZE                4096
#define ISO_BUFFER_SIZE             (64 * KB)	// Buffer size used for ISO data extraction
#define RSA_SIGNATURE_SIZE          256
#define CBN_SELCHANGE_INTERNAL      (CBN_SELCHANGE + 256)
#if defined(RUFUS_TEST)
#define RUFUS_URL                   "http://nas/~rufus"
#else
#define RUFUS_URL                   "https://rufus.ie"
#endif
#define DOWNLOAD_URL                RUFUS_URL "/downloads"
#define FILES_URL                   RUFUS_URL "/files"
#define FILES_DIR                   APPLICATION_NAME
#define FIDO_VERSION                "z1"
#define WPPRECORDER_MORE_INFO_URL   "https://github.com/pbatard/rufus/wiki/FAQ#bsods-with-windows-to-go-drives-created-from-windows-10-1809-isos"
#define SEVENZIP_URL                "https://7-zip.org/"
// Generated by following https://randomascii.wordpress.com/2013/03/09/symbols-the-microsoft-way/
#define DISKCOPY_URL                "https://msdl.microsoft.com/download/symbols/diskcopy.dll/54505118173000/diskcopy.dll"
#define DISKCOPY_SIZE               0x16ee00
#define DISKCOPY_IMAGE_OFFSET       0x66d8
#define DISKCOPY_IMAGE_SIZE         0x168000
#define SYMBOL_SERVER_USER_AGENT    "Microsoft-Symbol-Server/10.0.22621.755"
#define DEFAULT_ESP_MOUNT_POINT     "S:\\"
#define IS_POWER_OF_2(x)            ((x != 0) && (((x) & ((x) - 1)) == 0))
#define IGNORE_RETVAL(expr)         do { (void)(expr); } while(0)
#ifndef ARRAYSIZE
#define ARRAYSIZE(A)                (sizeof(A)/sizeof((A)[0]))
#endif
#ifndef STRINGIFY
#define STRINGIFY(x)                #x
#endif
#define PERCENTAGE(percent, value)  ((1ULL * (percent) * (value)) / 100ULL)
#define IsChecked(CheckBox_ID)      (IsDlgButtonChecked(hMainDialog, CheckBox_ID) == BST_CHECKED)
#define MB_IS_RTL                   (right_to_left_mode?MB_RTLREADING|MB_RIGHT:0)
#define CHECK_FOR_USER_CANCEL       if (IS_ERROR(ErrorStatus) && (SCODE_CODE(ErrorStatus) == ERROR_CANCELLED)) goto out
// Bit masks used for the display of additional image options in the UI
#define IMOP_WINTOGO                0x01
#define IMOP_PERSISTENCE            0x02

#define ComboBox_GetCurItemData(hCtrl) ComboBox_GetItemData(hCtrl, ComboBox_GetCurSel(hCtrl))

#define safe_free(p) do { free((void*)p); p = NULL; } while(0)
#define safe_mm_free(p) do { _mm_free((void*)p); p = NULL; } while(0)
static __inline void safe_strcp(char* dst, const size_t dst_max, const char* src, const size_t count) {
	memmove(dst, src, min(count, dst_max));
	dst[min(count, dst_max) - 1] = 0;
}
#define safe_strcpy(dst, dst_max, src) safe_strcp(dst, dst_max, src, safe_strlen(src) + 1)
#define static_strcpy(dst, src) safe_strcpy(dst, sizeof(dst), src)
#define safe_strcat(dst, dst_max, src) strncat_s(dst, dst_max, src, _TRUNCATE)
#define static_strcat(dst, src) safe_strcat(dst, sizeof(dst), src)
#define safe_strcmp(str1, str2) strcmp(((str1 == NULL) ? "<NULL>" : str1), ((str2 == NULL) ? "<NULL>" : str2))
#define safe_strstr(str1, str2) strstr(((str1 == NULL) ? "<NULL>" : str1), ((str2 == NULL) ? "<NULL>" : str2))
#define safe_stricmp(str1, str2) _stricmp(((str1 == NULL) ? "<NULL>" : str1), ((str2 == NULL) ? "<NULL>" : str2))
#define safe_strncmp(str1, str2, count) strncmp(((str1 == NULL) ? "<NULL>" : str1), ((str2 == NULL) ? "<NULL>" : str2), count)
#define safe_strnicmp(str1, str2, count) _strnicmp(((str1 == NULL) ? "<NULL>" : str1), ((str2 == NULL) ? "<NULL>" : str2), count)
#define safe_closehandle(h) do { if ((h != INVALID_HANDLE_VALUE) && (h != NULL)) { CloseHandle(h); h = INVALID_HANDLE_VALUE; } } while(0)
#define safe_release_dc(hDlg, hDC) do { if ((hDC != INVALID_HANDLE_VALUE) && (hDC != NULL)) { ReleaseDC(hDlg, hDC); hDC = NULL; } } while(0)
#define safe_sprintf(dst, count, ...) do { size_t _count = count; char* _dst = dst; _snprintf_s(_dst, _count, _TRUNCATE, __VA_ARGS__); \
	_dst[(_count) - 1] = 0; } while(0)
#define static_sprintf(dst, ...) safe_sprintf(dst, sizeof(dst), __VA_ARGS__)
#define safe_atoi(str) ((((char*)(str))==NULL) ? 0 : atoi(str))
#define safe_strlen(str) ((((char*)(str))==NULL) ? 0 : strlen(str))
#define safe_strdup(str) ((((char*)(str))==NULL) ? NULL : _strdup(str))
#if defined(_MSC_VER)
#define safe_vsnprintf(buf, size, format, arg) _vsnprintf_s(buf, size, _TRUNCATE, format, arg)
#else
#define safe_vsnprintf vsnprintf
#endif
static __inline void static_repchr(char* p, char s, char r) {
	if (p != NULL) while (*p != 0) { if (*p == s) *p = r; p++; }
}
#define to_unix_path(str) static_repchr(str, '\\', '/')
#define to_windows_path(str) static_repchr(str, '/', '\\')

extern void uprintf(const char *format, ...);
extern void uprintfs(const char *str);
#define vuprintf(...) do { if (verbose) uprintf(__VA_ARGS__); } while(0)
#define vvuprintf(...) do { if (verbose > 1) uprintf(__VA_ARGS__); } while(0)
#define suprintf(...) do { if (!bSilent) uprintf(__VA_ARGS__); } while(0)
#define uuprintf(...) do { if (usb_debug) uprintf(__VA_ARGS__); } while(0)
#define ubprintf(...) do { safe_sprintf(&ubuffer[ubuffer_pos], UBUFFER_SIZE - ubuffer_pos - 4, __VA_ARGS__); \
	ubuffer_pos = strlen(ubuffer); ubuffer[ubuffer_pos++] = '\r'; ubuffer[ubuffer_pos++] = '\n'; \
	ubuffer[ubuffer_pos] = 0; } while(0)
#define ubflush() do { if (ubuffer_pos) uprintf("%s", ubuffer); ubuffer_pos = 0; } while(0)
#ifdef _DEBUG
#define duprintf uprintf
#else
#define duprintf(...)
#endif

/* Custom Windows messages */
enum user_message_type {
	UM_FORMAT_COMPLETED = WM_APP,
	UM_MEDIA_CHANGE,
	UM_PROGRESS_INIT,
	UM_PROGRESS_EXIT,
	UM_NO_UPDATE,
	UM_UPDATE_CSM_TOOLTIP,
	UM_RESIZE_BUTTONS,
	UM_ENABLE_CONTROLS,
	UM_SELECT_ISO,
	UM_TIMER_START,
	UM_FORMAT_START,
	// Start of the WM IDs for the language menu items
	UM_LANGUAGE_MENU = WM_APP + 0x100
};

/* Custom notifications */
enum notification_type {
	MSG_INFO,
	MSG_WARNING,
	MSG_ERROR,
	MSG_QUESTION,
	MSG_WARNING_QUESTION
};
typedef INT_PTR (CALLBACK *Callback_t)(HWND, UINT, WPARAM, LPARAM);
typedef struct {
	WORD id;
	union {
		Callback_t callback;
		char* url;
	};
} notification_info;	// To provide a "More info..." on notifications

/* Status Bar sections */
#define SB_SECTION_LEFT         0
#define SB_SECTION_RIGHT        1
#define SB_TIMER_SECTION_SIZE   58.0f

/* Timers used throughout the program */
enum timer_type {
	TID_MESSAGE_INFO = 0x1000,
	TID_MESSAGE_STATUS,
	TID_OUTPUT_INFO,
	TID_OUTPUT_STATUS,
	TID_BADBLOCKS_UPDATE,
	TID_APP_TIMER,
	TID_BLOCKING_TIMER,
	TID_REFRESH_TIMER,
	TID_MARQUEE_TIMER
};

/* Action type, for progress bar breakdown */
enum action_type {
	OP_NOOP_WITH_TASKBAR = -3,
	OP_NOOP = -2,
	OP_INIT = -1,
	OP_ANALYZE_MBR = 0,
	OP_BADBLOCKS,
	OP_ZERO_MBR,
	OP_PARTITION,
	OP_FORMAT,
	OP_CREATE_FS,
	OP_FIX_MBR,
	OP_FILE_COPY,
	OP_PATCH,
	OP_FINALIZE,
	OP_EXTRACT_ZIP,
	OP_MAX
};

/* File system indexes in our FS combobox */
enum fs_type {
	FS_UNKNOWN = -1,
	FS_FAT16 = 0,
	FS_FAT32,
	FS_NTFS,
	FS_UDF,
	FS_EXFAT,
	FS_REFS,
	FS_EXT2,
	FS_EXT3,
	FS_EXT4,
	FS_MAX
};

enum boot_type {
	BT_NON_BOOTABLE = 0,
	BT_MSDOS,
	BT_FREEDOS,
	BT_IMAGE,
	BT_SYSLINUX_V4,		// Start of indexes that only display in advanced mode
	BT_SYSLINUX_V6,
	BT_REACTOS,
	BT_GRUB4DOS,
	BT_GRUB2,
	BT_UEFI_NTFS,
	BT_MAX
};

enum target_type {
	TT_BIOS = 0,
	TT_UEFI,
	TT_MAX
};
// For the partition types we'll use Microsoft's PARTITION_STYLE_### constants
#define PARTITION_STYLE_SFD PARTITION_STYLE_RAW

enum image_option_type {
	IMOP_WIN_STANDARD = 0,
	IMOP_WIN_EXTENDED,
	IMOP_WIN_TO_GO,
	IMOP_MAX
};

enum file_io_type {
	FILE_IO_READ = 0,
	FILE_IO_WRITE,
	FILE_IO_APPEND
};

/* Special handling for old .c32 files we need to replace */
#define NB_OLD_C32          2
#define OLD_C32_NAMES       { "menu.c32", "vesamenu.c32" }
#define OLD_C32_THRESHOLD   { 53500, 148000 }

/* ISO details that the application may want */
#define WINPE_I386          0x0007
#define WINPE_AMD64         0x0023
#define WINPE_MININT        0x01C0
#define SPECIAL_WIM_VERSION 0x000E0000
#define HAS_KOLIBRIOS(r)    (r.has_kolibrios)
#define HAS_REACTOS(r)      (r.reactos_path[0] != 0)
#define HAS_GRUB(r)         ((r.has_grub2) || (r.has_grub4dos))
#define HAS_SYSLINUX(r)     (r.sl_version != 0)
#define HAS_BOOTMGR_BIOS(r) (r.has_bootmgr)
#define HAS_BOOTMGR_EFI(r)  (r.has_bootmgr_efi)
#define HAS_BOOTMGR(r)      (HAS_BOOTMGR_BIOS(r) || HAS_BOOTMGR_EFI(r))
#define HAS_REGULAR_EFI(r)  (r.has_efi & 0x7FFE)
#define HAS_WININST(r)      (r.wininst_index != 0)
#define HAS_WINPE(r)        (((r.winpe & WINPE_I386) == WINPE_I386)||((r.winpe & WINPE_AMD64) == WINPE_AMD64)||((r.winpe & WINPE_MININT) == WINPE_MININT))
#define HAS_WINDOWS(r)      (HAS_BOOTMGR(r) || (r.uses_minint) || HAS_WINPE(r))
#define HAS_WIN7_EFI(r)     ((r.has_efi == 1) && HAS_WININST(r))
#define IS_WINDOWS_1X(r)    (r.has_bootmgr_efi && (r.win_version.major >= 10))
#define IS_WINDOWS_11(r)    (r.has_bootmgr_efi && (r.win_version.major >= 11))
#define HAS_EFI_IMG(r)      (r.efi_img_path[0] != 0)
#define IS_DD_BOOTABLE(r)   (r.is_bootable_img > 0)
#define IS_DD_ONLY(r)       ((r.is_bootable_img > 0) && (!r.is_iso || r.disable_iso))
#define IS_EFI_BOOTABLE(r)  (r.has_efi != 0)
#define IS_BIOS_BOOTABLE(r) (HAS_BOOTMGR(r) || HAS_SYSLINUX(r) || HAS_WINPE(r) || HAS_GRUB(r) || HAS_REACTOS(r) || HAS_KOLIBRIOS(r))
#define HAS_WINTOGO(r)      (HAS_BOOTMGR(r) && IS_EFI_BOOTABLE(r) && HAS_WININST(r))
#define HAS_PERSISTENCE(r)  ((HAS_SYSLINUX(r) || HAS_GRUB(r)) && !(HAS_WINDOWS(r) || HAS_REACTOS(r) || HAS_KOLIBRIOS(r)))
#define IS_FAT(fs)          ((fs == FS_FAT16) || (fs == FS_FAT32))
#define IS_EXT(fs)          ((fs >= FS_EXT2) && (fs <= FS_EXT4))
#define SYMLINKS_RR         0x01
#define SYMLINKS_UDF        0x02

typedef struct {
	uint16_t major;
	uint16_t minor;
	uint16_t build;
	uint16_t revision;
} winver_t;

/* We can't use the Microsoft enums as we want to have RISC-V */
enum ArchType {
	ARCH_UNKNOWN = 0,
	ARCH_X86_32,
	ARCH_X86_64,
	ARCH_ARM_32,
	ARCH_ARM_64,
	ARCH_IA_64,
	ARCH_RISCV_32,
	ARCH_RISCV_64,
	ARCH_RISCV_128,
	ARCH_EBC,
	ARCH_MAX
};

typedef struct {
	char label[192];					// 3*64 to account for UTF-8
	char usb_label[192];				// converted USB label for workaround
	char cfg_path[128];					// path to the ISO's isolinux.cfg
	char reactos_path[128];				// path to the ISO's freeldr.sys or setupldr.sys
	char wininst_path[MAX_WININST][64];	// path to the Windows install image(s)
	char efi_boot_path[ARCH_MAX][30];	// paths of detected UEFI bootloaders
	char efi_img_path[128];				// path to an efi.img file
	uint64_t image_size;
	uint64_t projected_size;
	int64_t mismatch_size;
	uint32_t wininst_version;
	BOOLEAN is_iso;
	int8_t is_bootable_img;
	BOOLEAN is_vhd;
	BOOLEAN is_windows_img;
	BOOLEAN disable_iso;
	BOOLEAN rh8_derivative;
	uint16_t winpe;
	uint16_t has_efi;
	uint8_t has_md5sum;
	uint8_t wininst_index;
	uint8_t has_symlinks;
	BOOLEAN has_4GB_file;
	BOOLEAN has_long_filename;
	BOOLEAN has_deep_directories;
	BOOLEAN has_bootmgr;
	BOOLEAN has_bootmgr_efi;
	BOOLEAN has_autorun;
	BOOLEAN has_old_c32[NB_OLD_C32];
	BOOLEAN has_old_vesamenu;
	BOOLEAN has_efi_syslinux;
	BOOLEAN has_grub4dos;
	uint8_t has_grub2;
	BOOLEAN has_compatresources_dll;
	BOOLEAN has_panther_unattend;
	BOOLEAN has_kolibrios;
	BOOLEAN needs_syslinux_overwrite;
	BOOLEAN needs_ntfs;
	BOOLEAN uses_casper;
	BOOLEAN uses_minint;
	uint8_t compression_type;
	winver_t win_version;	// Windows ISO version
	uint16_t sl_version;	// Syslinux/Isolinux version
	char sl_version_str[12];
	char sl_version_ext[32];
	char grub2_version[192];
} RUFUS_IMG_REPORT;

/* Isolate the Syslinux version numbers */
#define SL_MAJOR(x) ((uint8_t)((x)>>8))
#define SL_MINOR(x) ((uint8_t)(x))

typedef struct {
	char* id;
	char* name;
	char* display_name;
	char* label;
	char* hub;
	DWORD index;
	uint32_t port;
	uint64_t size;
} RUFUS_DRIVE;

typedef struct {
	uint16_t version[3];
	uint32_t platform_min[2];		// minimum platform version required
	char* download_url;
	char* release_notes;
} RUFUS_UPDATE;

typedef struct {
	DWORD Type;
	DWORD DeviceNum;
	DWORD BufSize;
	LONGLONG DeviceSize;
	char* DevicePath;
	char* ImagePath;
	char* Label;
} IMG_SAVE;

/*
 * Structure and macros used for the extensions specification of FileDialog()
 * You can use:
 *   EXT_DECL(my_extensions, "default.std", __VA_GROUP__("*.std", "*.other"), __VA_GROUP__("Standard type", "Other Type"));
 * to define an 'ext_t my_extensions' variable initialized with the relevant attributes.
 */
typedef struct ext_t {
	size_t count;
	const char* filename;
	const char** extension;
	const char** description;
} ext_t;

/* DLL address resolver */
typedef struct {
	char* path;
	uint32_t    count;
	char** name;
	uint32_t* address;	// 32-bit will do, as we're not dealing with >4GB DLLs...
} dll_resolver_t;

/* Alignment macro */
#if defined(__GNUC__)
#define ALIGNED(m) __attribute__ ((__aligned__(m)))
#elif defined(_MSC_VER)
#define ALIGNED(m) __declspec(align(m))
#endif

/* Hash definitions */
enum hash_type {
	HASH_MD5 = 0,
	HASH_SHA1,
	HASH_SHA256,
	HASH_SHA512,
	HASH_MAX
};

/* Blocksize for each hash algorithm - Must be a power of 2 */
#define MD5_BLOCKSIZE       64
#define SHA1_BLOCKSIZE      64
#define SHA256_BLOCKSIZE    64
#define SHA512_BLOCKSIZE    128
#define MAX_BLOCKSIZE       SHA512_BLOCKSIZE

/* Hashsize for each hash algorithm */
#define MD5_HASHSIZE        16
#define SHA1_HASHSIZE       20
#define SHA256_HASHSIZE     32
#define SHA512_HASHSIZE     64
#define MAX_HASHSIZE        SHA512_HASHSIZE

/* Context for the hash algorithms */
typedef struct ALIGNED(64) {
	uint8_t buf[MAX_BLOCKSIZE];
	uint64_t state[8];
	uint64_t bytecount;
} HASH_CONTEXT;

/* Hash functions */
typedef void hash_init_t(HASH_CONTEXT* ctx);
typedef void hash_write_t(HASH_CONTEXT* ctx, const uint8_t* buf, size_t len);
typedef void hash_final_t(HASH_CONTEXT* ctx);
extern hash_init_t* hash_init[HASH_MAX];
extern hash_write_t* hash_write[HASH_MAX];
extern hash_final_t* hash_final[HASH_MAX];

#ifndef __VA_GROUP__
#define __VA_GROUP__(...)  __VA_ARGS__
#endif
#define EXT_X(prefix, ...) const char* _##prefix##_x[] = { __VA_ARGS__ }
#define EXT_D(prefix, ...) const char* _##prefix##_d[] = { __VA_ARGS__ }
#define EXT_DECL(var, filename, extensions, descriptions)                   \
	EXT_X(var, extensions);                                                 \
	EXT_D(var, descriptions);                                               \
	ext_t var = { ARRAYSIZE(_##var##_x), filename, _##var##_x, _##var##_d }

/* Duplication of the TBPFLAG enum for Windows taskbar progress */
typedef enum TASKBAR_PROGRESS_FLAGS
{
	TASKBAR_NOPROGRESS = 0,
	TASKBAR_INDETERMINATE = 0x1,
	TASKBAR_NORMAL = 0x2,
	TASKBAR_ERROR = 0x4,
	TASKBAR_PAUSED = 0x8
} TASKBAR_PROGRESS_FLAGS;

static __inline USHORT GetApplicationArch(void)
{
#if defined(_M_AMD64)
	return IMAGE_FILE_MACHINE_AMD64;
#elif defined(_M_IX86)
	return IMAGE_FILE_MACHINE_I386;
#elif defined(_M_ARM64)
	return IMAGE_FILE_MACHINE_ARM64;
#elif defined(_M_ARM)
	return IMAGE_FILE_MACHINE_ARM;
#else
	return IMAGE_FILE_MACHINE_UNKNOWN;
#endif
}

static __inline const char* GetArchName(USHORT uArch)
{
	switch (uArch) {
	case IMAGE_FILE_MACHINE_AMD64:
		return "x64";
	case IMAGE_FILE_MACHINE_I386:
		return "x86";
	case IMAGE_FILE_MACHINE_ARM64:
		return "arm64";
	case IMAGE_FILE_MACHINE_ARM:
		return "arm";
	default:
		return "unknown";
	}
}

/* Windows versions */
enum WindowsVersion {
	WINDOWS_UNDEFINED = 0,
	WINDOWS_XP = 0x51,
	WINDOWS_2003 = 0x52,	// Also XP_64
	WINDOWS_VISTA = 0x60,	// Also Server 2008
	WINDOWS_7 = 0x61,		// Also Server 2008_R2
	WINDOWS_8 = 0x62,		// Also Server 2012
	WINDOWS_8_1 = 0x63,		// Also Server 2012_R2
	WINDOWS_10_PREVIEW1 = 0x64,
	WINDOWS_10 = 0xA0,		// Also Server 2016, also Server 2019
	WINDOWS_11 = 0xB0,		// Also Server 2022
	WINDOWS_MAX = 0xFFFF,
};

typedef struct {
	DWORD Major;
	DWORD Minor;
	DWORD Micro;
	DWORD Nano;
} version_t;

typedef struct {
	DWORD Version;
	DWORD Major;
	DWORD Minor;
	DWORD BuildNumber;
	DWORD Ubr;
	DWORD Edition;
	USHORT Arch;
	char VersionStr[128];
} windows_version_t;

// Windows User Experience (unattend.xml) flags and masks
#define UNATTEND_SECUREBOOT_TPM_MINRAM      0x00001
#define UNATTEND_NO_ONLINE_ACCOUNT          0x00004
#define UNATTEND_NO_DATA_COLLECTION         0x00008
#define UNATTEND_OFFLINE_INTERNAL_DRIVES    0x00010
#define UNATTEND_DUPLICATE_LOCALE           0x00020
#define UNATTEND_SET_USER                   0x00040
#define UNATTEND_DISABLE_BITLOCKER          0x00080
#define UNATTEND_FORCE_S_MODE               0x00100
#define UNATTEND_DEFAULT_MASK               0x000FF
#define UNATTEND_WINDOWS_TO_GO              0x10000		// Special flag for Windows To Go

#define UNATTEND_WINPE_SETUP_MASK           (UNATTEND_SECUREBOOT_TPM_MINRAM)
#define UNATTEND_SPECIALIZE_DEPLOYMENT_MASK (UNATTEND_NO_ONLINE_ACCOUNT)
#define UNATTEND_OOBE_SHELL_SETUP_MASK      (UNATTEND_NO_DATA_COLLECTION | UNATTEND_SET_USER)
#define UNATTEND_OOBE_INTERNATIONAL_MASK    (UNATTEND_DUPLICATE_LOCALE)
#define UNATTEND_OOBE_MASK                  (UNATTEND_OOBE_SHELL_SETUP_MASK | UNATTEND_OOBE_INTERNATIONAL_MASK | UNATTEND_DISABLE_BITLOCKER)
#define UNATTEND_OFFLINE_SERVICING_MASK     (UNATTEND_OFFLINE_INTERNAL_DRIVES | UNATTEND_FORCE_S_MODE)
#define UNATTEND_DEFAULT_SELECTION_MASK     (UNATTEND_SECUREBOOT_TPM_MINRAM | UNATTEND_NO_ONLINE_ACCOUNT | UNATTEND_OFFLINE_INTERNAL_DRIVES)

/* Hash tables */
typedef struct htab_entry {
	uint32_t used;
	char* str;
	void* data;
} htab_entry;
typedef struct htab_table {
	htab_entry* table;
	uint32_t size;
	uint32_t filled;
} htab_table;
#define HTAB_EMPTY {NULL, 0, 0}
extern BOOL htab_create(uint32_t nel, htab_table* htab);
extern void htab_destroy(htab_table* htab);
extern uint32_t htab_hash(char* str, htab_table* htab);

/* Basic String Array */
typedef struct {
	char** String;
	uint32_t Index;		// Current array size
	uint32_t Max;		// Maximum array size
} StrArray;
extern void StrArrayCreate(StrArray* arr, uint32_t initial_size);
extern int32_t StrArrayAdd(StrArray* arr, const char* str, BOOL);
extern int32_t StrArrayFind(StrArray* arr, const char* str);
extern void StrArrayClear(StrArray* arr);
extern void StrArrayDestroy(StrArray* arr);
#define IsStrArrayEmpty(arr) (arr.Index == 0)

/*
 * Globals
 */
extern RUFUS_UPDATE update;
extern RUFUS_IMG_REPORT img_report;
extern HINSTANCE hMainInstance;
extern HWND hMainDialog, hLogDialog, hStatus, hDeviceList, hCapacity, hImageOption;
extern HWND hPartitionScheme, hTargetSystem, hFileSystem, hClusterSize, hLabel, hBootType;
extern HWND hNBPasses, hLog, hInfo, hProgress;
extern WORD selected_langid;
extern DWORD ErrorStatus, DownloadStatus, MainThreadId, LastWriteError;
extern BOOL use_own_c32[NB_OLD_C32], detect_fakes, op_in_progress, right_to_left_mode;
extern BOOL allow_dual_uefi_bios, large_drive, usb_debug;
extern uint8_t image_options, *pe256ssp;
extern uint16_t rufus_version[3], embedded_sl_version[2];
extern uint32_t pe256ssp_size;
extern uint64_t persistence_size;
extern int64_t iso_blocking_status;
extern size_t ubuffer_pos;
extern const int nb_steps[FS_MAX];
extern float fScale;
extern windows_version_t WindowsVersion;
extern int dialog_showing, force_update, fs_type, boot_type, partition_type, target_type;
extern unsigned long syslinux_ldlinux_len[2];
extern char ubuffer[UBUFFER_SIZE], embedded_sl_version_str[2][12];
extern char szFolderPath[MAX_PATH], app_dir[MAX_PATH], temp_dir[MAX_PATH], system_dir[MAX_PATH];
extern char sysnative_dir[MAX_PATH], app_data_dir[MAX_PATH], *image_path, *fido_url;

/*
 * Shared prototypes
 */
extern void GetWindowsVersion(windows_version_t* WindowsVersion);
extern version_t* GetExecutableVersion(const char* path);
extern const char* WindowsErrorString(void);
extern void DumpBufferHex(void *buf, size_t size);
extern void PrintStatusInfo(BOOL info, BOOL debug, unsigned int duration, int msg_id, ...);
#define PrintStatus(...) PrintStatusInfo(FALSE, FALSE, __VA_ARGS__)
#define PrintStatusDebug(...) PrintStatusInfo(FALSE, TRUE, __VA_ARGS__)
#define PrintInfo(...) PrintStatusInfo(TRUE, FALSE, __VA_ARGS__)
#define PrintInfoDebug(...) PrintStatusInfo(TRUE, TRUE, __VA_ARGS__)
extern void UpdateProgress(int op, float percent);
extern void _UpdateProgressWithInfo(int op, int msg, uint64_t processed, uint64_t total, BOOL force);
#define UpdateProgressWithInfo(op, msg, processed, total) _UpdateProgressWithInfo(op, msg, processed, total, FALSE)
#define UpdateProgressWithInfoForce(op, msg, processed, total) _UpdateProgressWithInfo(op, msg, processed, total, TRUE)
#define UpdateProgressWithInfoInit(hProgressDialog, bNoAltMode) UpdateProgressWithInfo(OP_INIT, (int)bNoAltMode, (uint64_t)(uintptr_t)hProgressDialog, 0);
extern const char* StrError(DWORD error_code, BOOL use_default_locale);
extern char* GuidToString(const GUID* guid, BOOL bDecorated);
extern GUID* StringToGuid(const char* str);
extern char* SizeToHumanReadable(uint64_t size, BOOL copy_to_log, BOOL fake_units);
extern char* TimestampToHumanReadable(uint64_t ts);
extern HWND MyCreateDialog(HINSTANCE hInstance, int Dialog_ID, HWND hWndParent, DLGPROC lpDialogFunc);
extern INT_PTR MyDialogBox(HINSTANCE hInstance, int Dialog_ID, HWND hWndParent, DLGPROC lpDialogFunc);
extern void CenterDialog(HWND hDlg, HWND hParent);
extern void ResizeMoveCtrl(HWND hDlg, HWND hCtrl, int dx, int dy, int dw, int dh, float scale);
extern void ResizeButtonHeight(HWND hDlg, int id);
extern void CreateStatusBar(void);
extern void CreateStaticFont(HDC hDC, HFONT* hFont, BOOL underlined);
extern void SetTitleBarIcon(HWND hDlg);
extern BOOL CreateTaskbarList(void);
extern BOOL SetTaskbarProgressState(TASKBAR_PROGRESS_FLAGS tbpFlags);
extern BOOL SetTaskbarProgressValue(ULONGLONG ullCompleted, ULONGLONG ullTotal);
extern INT_PTR CreateAboutBox(void);
extern BOOL CreateTooltip(HWND hControl, const char* message, int duration);
extern void DestroyTooltip(HWND hWnd);
extern void DestroyAllTooltips(void);
extern BOOL Notification(int type, const char* dont_display_setting, const notification_info* more_info, char* title, char* format, ...);
extern int CustomSelectionDialog(int style, char* title, char* message, char** choices, int size, int mask, int username_index);
#define SelectionDialog(title, message, choices, size) CustomSelectionDialog(BS_AUTORADIOBUTTON, title, message, choices, size, 1, -1)
extern void ListDialog(char* title, char* message, char** items, int size);
extern SIZE GetTextSize(HWND hCtrl, char* txt);
extern BOOL ExtractAppIcon(const char* filename, BOOL bSilent);
extern BOOL ExtractDOS(const char* path);
extern BOOL ExtractISO(const char* src_iso, const char* dest_dir, BOOL scan);
extern BOOL ExtractZip(const char* src_zip, const char* dest_dir);
extern int64_t ExtractISOFile(const char* iso, const char* iso_file, const char* dest_file, DWORD attributes);
extern uint32_t ReadISOFileToBuffer(const char* iso, const char* iso_file, uint8_t** buf);
extern BOOL CopySKUSiPolicy(const char* drive_name);
extern BOOL HasEfiImgBootLoaders(void);
extern BOOL DumpFatDir(const char* path, int32_t cluster);
extern BOOL InstallSyslinux(DWORD drive_index, char drive_letter, int fs);
extern uint16_t GetSyslinuxVersion(char* buf, size_t buf_size, char** ext);
extern BOOL SetAutorun(const char* path);
extern char* FileDialog(BOOL save, char* path, const ext_t* ext, UINT* selected_ext);
extern BOOL FileIO(enum file_io_type io_type, char* path, char** buffer, DWORD* size);
extern uint8_t* GetResource(HMODULE module, char* name, char* type, const char* desc, DWORD* len, BOOL duplicate);
extern DWORD GetResourceSize(HMODULE module, char* name, char* type, const char* desc);
extern DWORD RunCommandWithProgress(const char* cmdline, const char* dir, BOOL log, int msg);
#define RunCommand(cmd, dir, log) RunCommandWithProgress(cmd, dir, log, 0)
extern BOOL CompareGUID(const GUID *guid1, const GUID *guid2);
extern BOOL MountRegistryHive(const HKEY key, const char* pszHiveName, const char* pszHivePath);
extern BOOL UnmountRegistryHive(const HKEY key, const char* pszHiveName);
extern BOOL SetLGP(BOOL bRestore, BOOL* bExistingKey, const char* szPath, const char* szPolicy, DWORD dwValue);
extern LONG GetEntryWidth(HWND hDropDown, const char* entry);
extern uint64_t DownloadToFileOrBufferEx(const char* url, const char* file, const char* user_agent,
	BYTE** buffer, HWND hProgressDialog, BOOL bTaskBarProgress);
#define DownloadToFileOrBuffer(url, file, buffer, hProgressDialog, bTaskBarProgress) \
	DownloadToFileOrBufferEx(url, file, NULL, buffer, hProgressDialog, bTaskBarProgress)
extern DWORD DownloadSignedFile(const char* url, const char* file, HWND hProgressDialog, BOOL PromptOnError);
extern HANDLE DownloadSignedFileThreaded(const char* url, const char* file, HWND hProgressDialog, BOOL bPromptOnError);
extern INT_PTR CALLBACK UpdateCallback(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
extern void SetFidoCheck(void);
extern BOOL SetUpdateCheck(void);
extern BOOL CheckForUpdates(BOOL force);
extern void DownloadNewVersion(void);
extern BOOL DownloadISO(void);
extern BOOL IsDownloadable(const char* url);
extern BOOL IsShown(HWND hDlg);
extern uint32_t read_file(const char* path, uint8_t** buf);
extern uint32_t write_file(const char* path, const uint8_t* buf, const uint32_t size);
extern char* get_token_data_file_indexed(const char* token, const char* filename, int index);
#define get_token_data_file(token, filename) get_token_data_file_indexed(token, filename, 1)
extern char* set_token_data_file(const char* token, const char* data, const char* filename);
extern char* get_token_data_buffer(const char* token, unsigned int n, const char* buffer, size_t buffer_size);
extern char* insert_section_data(const char* filename, const char* section, const char* data, BOOL dos2unix);
extern char* replace_in_token_data(const char* filename, const char* token, const char* src, const char* rep, BOOL dos2unix);
extern char* replace_char(const char* src, const char c, const char* rep);
extern void parse_update(char* buf, size_t len);
extern void* get_data_from_asn1(const uint8_t* buf, size_t buf_len, const char* oid_str, uint8_t asn1_type, size_t* data_len);
extern int sanitize_label(char* label);
extern int IsHDD(DWORD DriveIndex, uint16_t vid, uint16_t pid, const char* strid);
extern char* GetSignatureName(const char* path, const char* country_code, BOOL bSilent);
extern uint64_t GetSignatureTimeStamp(const char* path);
extern LONG ValidateSignature(HWND hDlg, const char* path);
extern BOOL ValidateOpensslSignature(BYTE* pbBuffer, DWORD dwBufferLen, BYTE* pbSignature, DWORD dwSigLen);
extern BOOL ParseSKUSiPolicy(void);
extern BOOL IsFontAvailable(const char* font_name);
extern BOOL WriteFileWithRetry(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite,
	LPDWORD lpNumberOfBytesWritten, DWORD nNumRetries);
extern HANDLE CreateFileWithTimeout(LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
	LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes,
	HANDLE hTemplateFile, DWORD dwTimeOut);
extern BOOL SetThreadAffinity(DWORD_PTR* thread_affinity, size_t num_threads);
extern BOOL HashFile(const unsigned type, const char* path, uint8_t* sum);
extern BOOL PE256File(const char* path, uint8_t* hash);
extern void UpdateMD5Sum(const char* dest_dir, const char* md5sum_name);
extern BOOL HashBuffer(const unsigned type, const uint8_t* buf, const size_t len, uint8_t* sum);
extern BOOL IsFileInDB(const char* path);
extern int IsBootloaderRevoked(const char* path);
extern void PrintRevokedBootloaderInfo(void);
extern BOOL IsBufferInDB(const unsigned char* buf, const size_t len);
#define printbits(x) _printbits(sizeof(x), &x, 0)
#define printbitslz(x) _printbits(sizeof(x), &x, 1)
extern char* _printbits(size_t const size, void const * const ptr, int leading_zeroes);
extern BOOL IsCurrentProcessElevated(void);
extern char* ToLocaleName(DWORD lang_id);
extern void SetAlertPromptMessages(void);
extern BOOL SetAlertPromptHook(void);
extern void ClrAlertPromptHook(void);
extern BOOL StartProcessSearch(void);
extern void StopProcessSearch(void);
extern BOOL SetProcessSearch(DWORD DeviceNum);
extern BYTE GetProcessSearch(uint32_t timeout, uint8_t access_mask, BOOL bIgnoreStaleProcesses);
extern BOOL EnablePrivileges(void);
extern void FlashTaskbar(HANDLE handle);
extern DWORD WaitForSingleObjectWithMessages(HANDLE hHandle, DWORD dwMilliseconds);
extern HICON CreateMirroredIcon(HICON hiconOrg);
extern HANDLE CreatePreallocatedFile(const char* lpFileName, DWORD dwDesiredAccess,
	DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition,
	DWORD dwFlagsAndAttributes, LONGLONG fileSize);
extern uint32_t ResolveDllAddress(dll_resolver_t* resolver);
#define GetTextWidth(hDlg, id) GetTextSize(GetDlgItem(hDlg, id), NULL).cx

DWORD WINAPI HashThread(void* param);

/*
 * typedefs for the function prototypes. Use the something like:
 *   PF_DECL(FormatEx);
 * which translates to:
 *   FormatEx_t pfFormatEx = NULL;
 * in your code, to declare the entrypoint and then use:
 *   PF_INIT(FormatEx, Fmifs);
 * which translates to:
 *   pfFormatEx = (FormatEx_t) GetProcAddress(GetDLLHandle("fmifs"), "FormatEx");
 * to make it accessible.
 */
#define         MAX_LIBRARY_HANDLES 64
extern HMODULE  OpenedLibrariesHandle[MAX_LIBRARY_HANDLES];
extern uint16_t OpenedLibrariesHandleSize;
#define         OPENED_LIBRARIES_VARS HMODULE OpenedLibrariesHandle[MAX_LIBRARY_HANDLES]; uint16_t OpenedLibrariesHandleSize = 0
#define         CLOSE_OPENED_LIBRARIES while(OpenedLibrariesHandleSize > 0) FreeLibrary(OpenedLibrariesHandle[--OpenedLibrariesHandleSize])
static __inline HMODULE GetLibraryHandle(char* szLibraryName) {
	HMODULE h = NULL;
	wchar_t* wszLibraryName = NULL;
	int size;
	if (szLibraryName == NULL || szLibraryName[0] == 0)
		goto out;
	size = MultiByteToWideChar(CP_UTF8, 0, szLibraryName, -1, NULL, 0);
	if ((size <= 1) || ((wszLibraryName = (wchar_t*)calloc(size, sizeof(wchar_t))) == NULL) ||
		(MultiByteToWideChar(CP_UTF8, 0, szLibraryName, -1, wszLibraryName, size) != size))
		goto out;
	// If the library is already opened, just return a handle (that doesn't need to be freed)
	if ((h = GetModuleHandleW(wszLibraryName)) != NULL)
		goto out;
	// Sanity check
	if (OpenedLibrariesHandleSize >= MAX_LIBRARY_HANDLES) {
		uprintf("Error: MAX_LIBRARY_HANDLES is too small\n");
		goto out;
	}
	h = LoadLibraryExW(wszLibraryName, NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
	if (h != NULL)
		OpenedLibrariesHandle[OpenedLibrariesHandleSize++] = h;
	else
		uprintf("Unable to load '%S.dll': %s", wszLibraryName, WindowsErrorString());
out:
	free(wszLibraryName);
	return h;
}
#define PF_TYPE(api, ret, proc, args)		typedef ret (api *proc##_t)args
#define PF_DECL(proc)						static proc##_t pf##proc = NULL
#define PF_TYPE_DECL(api, ret, proc, args)	PF_TYPE(api, ret, proc, args); PF_DECL(proc)
#define PF_INIT(proc, name)					if (pf##proc == NULL) pf##proc = \
	(proc##_t) GetProcAddress(GetLibraryHandle(#name), #proc)
#define PF_INIT_OR_OUT(proc, name)			do {PF_INIT(proc, name);         \
	if (pf##proc == NULL) {uprintf("Unable to locate %s() in '%s.dll': %s",  \
	#proc, #name, WindowsErrorString()); goto out;} } while(0)
#define PF_INIT_OR_SET_STATUS(proc, name)	do {PF_INIT(proc, name);         \
	if ((pf##proc == NULL) && (NT_SUCCESS(status))) status = STATUS_PROCEDURE_NOT_FOUND; } while(0)
#if defined(_MSC_VER)
#define TRY_AND_HANDLE(exception, TRY_CODE, EXCEPTION_CODE) __try TRY_CODE   \
	__except (GetExceptionCode() == exception ? EXCEPTION_EXECUTE_HANDLER :  \
			  EXCEPTION_CONTINUE_SEARCH) EXCEPTION_CODE
#else
// NB: Eventually we may try __try1 and __except1 from MinGW...
#define TRY_AND_HANDLE(exception, TRY_CODE, EXCEPTION_CODE) TRY_CODE
#endif

/* Custom application errors */
#define FAC(f)                         ((f)<<16)
#define APPERR(err)                    (APPLICATION_ERROR_MASK|(err))
#define ERROR_INCOMPATIBLE_FS          0x1201
#define ERROR_CANT_QUICK_FORMAT        0x1202
#define ERROR_INVALID_CLUSTER_SIZE     0x1203
#define ERROR_INVALID_VOLUME_SIZE      0x1204
#define ERROR_CANT_START_THREAD        0x1205
#define ERROR_BADBLOCKS_FAILURE        0x1206
#define ERROR_ISO_SCAN                 0x1207
#define ERROR_ISO_EXTRACT              0x1208
#define ERROR_CANT_REMOUNT_VOLUME      0x1209
#define ERROR_CANT_PATCH               0x120A
#define ERROR_CANT_ASSIGN_LETTER       0x120B
#define ERROR_CANT_MOUNT_VOLUME        0x120C
#define ERROR_BAD_SIGNATURE            0x120D
#define ERROR_CANT_DOWNLOAD            0x120E

#define RUFUS_ERROR(err)               (ERROR_SEVERITY_ERROR | FAC(FACILITY_STORAGE) | (err))
