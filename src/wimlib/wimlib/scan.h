#ifndef _WIMLIB_SCAN_H
#define _WIMLIB_SCAN_H

#include "wimlib.h"
#include "wimlib/inode_table.h"
#include "wimlib/list.h"
#include "wimlib/progress.h"
#include "wimlib/security.h"
#include "wimlib/textfile.h"
#include "wimlib/util.h"

struct blob_table;
struct wim_dentry;
struct wim_inode;

struct capture_config {

	/* List of path patterns to exclude  */
	struct string_list exclusion_pats;

	/* List of path patterns to include, overriding exclusion_pats  */
	struct string_list exclusion_exception_pats;

	void *buf;
};

/* Scan parameters: common parameters to implementations of building an
 * in-memory dentry tree from an external directory structure.  */
struct scan_params {

	/* The blob table within which any new blobs discovered during the scan
	 * will be deduplicated.  */
	struct blob_table *blob_table;

	/* List of new blobs that have been discovered without their SHA-1
	 * message digests having been calculated (as a shortcut).  */
	struct list_head *unhashed_blobs;

	/* Map from (inode number, device number) pair to inode for new inodes
	 * that have been discovered so far.  */
	struct wim_inode_table *inode_table;

	/* The set of unique security descriptors to which each newly
	 * discovered, unique security descriptor will be added.  */
	struct wim_sd_set *sd_set;

	/* The capture configuration in effect, or NULL if none.  */
	struct capture_config *config;

	/* Flags that affect the scan operation (WIMLIB_ADD_FLAG_*) */
	int add_flags;

	/* If non-NULL, the user-supplied progress function. */
	wimlib_progress_func_t progfunc;
	void *progctx;

	/* Progress data.  */
	union wimlib_progress_info progress;

	/* Path to the file or directory currently being scanned */
	tchar *cur_path;
	size_t cur_path_nchars;
	size_t cur_path_alloc_nchars;

	/* Length of the prefix of 'cur_path' which names the root of the
	 * directory tree currently being scanned */
	size_t root_path_nchars;

	/* Can be used by the scan implementation.  */
	u64 capture_root_ino;
	u64 capture_root_dev;
};

/* scan.c */

int
do_scan_progress(struct scan_params *params, int status,
		 const struct wim_inode *inode);

int
mangle_pat(tchar *pat, const tchar *path, unsigned long line_no);

int
read_capture_config(const tchar *config_file, const void *buf,
		    size_t bufsize, struct capture_config *config);

void
destroy_capture_config(struct capture_config *config);

bool
match_pattern_list(const tchar *path, const struct string_list *list,
		   int match_flags);

int
try_exclude(const struct scan_params *params);

typedef int (*scan_tree_t)(struct wim_dentry **, const tchar *,
			   struct scan_params *);

#ifdef WITH_NTFS_3G
/* ntfs-3g_capture.c */
int
ntfs_3g_build_dentry_tree(struct wim_dentry **root_ret,
			  const tchar *device, struct scan_params *params);
#endif

#ifdef _WIN32
/* win32_capture.c */
int
win32_build_dentry_tree(struct wim_dentry **root_ret,
			const tchar *root_disk_path,
			struct scan_params *params);
#define platform_default_scan_tree win32_build_dentry_tree
#else
/* unix_capture.c */
int
unix_build_dentry_tree(struct wim_dentry **root_ret,
		       const tchar *root_disk_path, struct scan_params *params);
#define platform_default_scan_tree unix_build_dentry_tree
#endif

#ifdef ENABLE_TEST_SUPPORT
int
generate_dentry_tree(struct wim_dentry **root_ret,
		     const tchar *root_disk_path, struct scan_params *params);
#endif

#define WIMLIB_ADD_FLAG_ROOT	0x80000000

static inline int
report_scan_error(struct scan_params *params, int error_code)
{
	return report_error(params->progfunc, params->progctx, error_code,
			    params->cur_path);
}

bool
should_ignore_filename(const tchar *name, int name_nchars);

void
attach_scanned_tree(struct wim_dentry *parent, struct wim_dentry *child,
		    struct blob_table *blob_table);

int
pathbuf_init(struct scan_params *params, const tchar *root_path);

const tchar *
pathbuf_append_name(struct scan_params *params, const tchar *name,
		    size_t name_nchars, size_t *orig_path_nchars_ret);

void
pathbuf_truncate(struct scan_params *params, size_t nchars);

#endif /* _WIMLIB_SCAN_H */
