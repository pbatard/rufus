#ifndef _WIMLIB_APPLY_H
#define _WIMLIB_APPLY_H

#include "wimlib/compiler.h"
#include "wimlib/file_io.h"
#include "wimlib/list.h"
#include "wimlib/progress.h"
#include "wimlib/types.h"
#include "wimlib.h"

/* These can be treated as counts (for required_features) or booleans (for
 * supported_features).  */
struct wim_features {
	unsigned long readonly_files;
	unsigned long hidden_files;
	unsigned long system_files;
	unsigned long archive_files;
	unsigned long compressed_files;
	unsigned long encrypted_files;
	unsigned long encrypted_directories;
	unsigned long not_context_indexed_files;
	unsigned long sparse_files;
	unsigned long named_data_streams;
	unsigned long hard_links;
	unsigned long reparse_points;
	unsigned long symlink_reparse_points;
	unsigned long other_reparse_points;
	unsigned long security_descriptors;
	unsigned long short_names;
	unsigned long unix_data;
	unsigned long object_ids;
	unsigned long timestamps;
	unsigned long case_sensitive_filenames;
	unsigned long xattrs;
};

struct blob_descriptor;
struct read_blob_callbacks;
struct apply_operations;
struct wim_dentry;

struct apply_ctx {
	/* The WIMStruct from which files are being extracted from the currently
	 * selected image.  */
	WIMStruct *wim;

	/* The target of the extraction, usually the path to a directory.  */
	const tchar *target;

	/* Length of @target in tchars.  */
	size_t target_nchars;

	/* Extraction flags (WIMLIB_EXTRACT_FLAG_*)  */
	int extract_flags;

	/* User-provided progress function, or NULL if not specified.  */
	wimlib_progress_func_t progfunc;
	void *progctx;

	/* Progress data buffer, with progress.extract initialized.  */
	union wimlib_progress_info progress;

	/* Features required to extract the files (with counts)  */
	struct wim_features required_features;

	/* Features supported by the extraction mode (with booleans)  */
	struct wim_features supported_features;

	/* The members below should not be used outside of extract.c  */
	const struct apply_operations *apply_ops;
	u64 next_progress;
	unsigned long invalid_sequence;
	unsigned long num_blobs_remaining;
	struct list_head blob_list;
	const struct read_blob_callbacks *saved_cbs;
	struct filedes tmpfile_fd;
	tchar *tmpfile_name;
	unsigned int count_until_file_progress;
};

/* Maximum number of UNIX file descriptors, NTFS attributes, or Windows file
 * handles that can be opened simultaneously to extract a blob to multiple
 * destinations.  */
#ifndef __APPLE__
#define MAX_OPEN_FILES 512
#else /* !__APPLE__ */
/* With macOS, reduce to 128 because the default value for ulimit -n is 256 */
#define MAX_OPEN_FILES 128
#endif /* __APPLE__ */

static inline int
extract_progress(struct apply_ctx *ctx, enum wimlib_progress_msg msg)
{
	return call_progress(ctx->progfunc, msg, &ctx->progress, ctx->progctx);
}

int
do_file_extract_progress(struct apply_ctx *ctx, enum wimlib_progress_msg msg);

#define COUNT_PER_FILE_PROGRESS 256

static inline int
maybe_do_file_progress(struct apply_ctx *ctx, enum wimlib_progress_msg msg)
{
	ctx->progress.extract.current_file_count++;
	if (unlikely(!--ctx->count_until_file_progress))
		return do_file_extract_progress(ctx, msg);
	return 0;
}

int
start_file_structure_phase(struct apply_ctx *ctx, u64 end_file_count);

int
start_file_metadata_phase(struct apply_ctx *ctx, u64 end_file_count);

/* Report that a file was created, prior to blob extraction.  */
static inline int
report_file_created(struct apply_ctx *ctx)
{
	return maybe_do_file_progress(ctx, WIMLIB_PROGRESS_MSG_EXTRACT_FILE_STRUCTURE);
}

/* Report that file metadata was applied, after blob extraction.  */
static inline int
report_file_metadata_applied(struct apply_ctx *ctx)
{
	return maybe_do_file_progress(ctx, WIMLIB_PROGRESS_MSG_EXTRACT_METADATA);
}

int
end_file_structure_phase(struct apply_ctx *ctx);

int
end_file_metadata_phase(struct apply_ctx *ctx);

static inline int
report_apply_error(struct apply_ctx *ctx, int error_code, const tchar *path)
{
	return report_error(ctx->progfunc, ctx->progctx, error_code, path);
}

bool
detect_sparse_region(const void *data, size_t size, size_t *len_ret);

static inline bool
maybe_detect_sparse_region(const void *data, size_t size, size_t *len_ret,
			   bool enabled)
{
	if (!enabled) {
		/* Force non-sparse without checking */
		*len_ret = size;
		return false;
	}
	return detect_sparse_region(data, size, len_ret);
}

#define inode_first_extraction_dentry(inode)				\
	((inode)->i_first_extraction_alias)

#define inode_for_each_extraction_alias(dentry, inode)			\
	for (dentry = inode_first_extraction_dentry(inode);		\
	     dentry != NULL;						\
	     dentry = dentry->d_next_extraction_alias)

int
extract_blob_list(struct apply_ctx *ctx, const struct read_blob_callbacks *cbs);

/*
 * Represents an extraction backend.
 */
struct apply_operations {

	/* Name of the extraction backend.  */
	const char *name;

	/*
	 * Query the features supported by the extraction backend.
	 *
	 * @target
	 *	The target string that was provided by the user.  (Often a
	 *	directory, but extraction backends are free to interpret this
	 *	differently.)
	 *
	 * @supported_features
	 *	A structure, each of whose members represents a feature that may
	 *	be supported by the extraction backend.  For each feature that
	 *	the extraction backend supports, this routine must set the
	 *	corresponding member to a nonzero value.
	 *
	 * Return 0 if successful; otherwise a positive wimlib error code.
	 */
	int (*get_supported_features)(const tchar *target,
				      struct wim_features *supported_features);

	/*
	 * Main extraction routine.
	 *
	 * The extraction backend is provided a list of dentries that have been
	 * prepared for extraction.  It is free to extract them in any way that
	 * it chooses.  Ideally, it should choose a method that maximizes
	 * performance.
	 *
	 * The target string will be provided in ctx->common.target.  This might
	 * be a directory, although extraction backends are free to interpret it
	 * as they wish.  TODO: in some cases, the common extraction code also
	 * interprets the target string.  This should be completely isolated to
	 * extraction backends.
	 *
	 * The extraction flags will be provided in ctx->common.extract_flags.
	 * Extraction backends should examine them and implement the behaviors
	 * for as many flags as possible.  Some flags are already handled by the
	 * common extraction code.  TODO: this needs to be better formalized.
	 *
	 * @dentry_list, the list of dentries, will be ordered such that the
	 * ancestor of any dentry always precedes any descendents.  Unless
	 * @single_tree_only is set, it's possible that the dentries consist of
	 * multiple disconnected trees.
	 *
	 * 'd_extraction_name' and 'd_extraction_name_nchars' of each dentry
	 * will be set to indicate the actual name with which the dentry should
	 * be extracted.  This may or may not be the same as 'd_name'.  TODO:
	 * really, the extraction backends should be responsible for generating
	 * 'd_extraction_name'.
	 *
	 * Each dentry will refer to a valid inode in 'd_inode'.  Each inode
	 * will contain a list of dentries of that inode being extracted; this
	 * list may be shorter than the inode's full dentry list.
	 *
	 * The blobs required to be extracted will already be prepared in
	 * 'apply_ctx'.  The extraction backend should call extract_blob_list()
	 * to extract them.
	 *
	 * The will_extract_dentry() utility function, given an arbitrary dentry
	 * in the WIM image (which may not be in the extraction list), can be
	 * used to determine if that dentry is in the extraction list.
	 *
	 * Return 0 if successful; otherwise a positive wimlib error code.
	 */
	int (*extract)(struct list_head *dentry_list, struct apply_ctx *ctx);

	/*
	 * Query whether the unnamed data stream of the specified file will be
	 * extracted as "externally backed" from the WIM archive itself.  If so,
	 * then the extraction backend is assumed to handle this separately, and
	 * the common extraction code will not register a usage of the unnamed
	 * data stream's blob.
	 *
	 * This routine is optional.
	 *
	 * Return:
	 *	< 0 if the file will *not* be externally backed.
	 *	= 0 if the file will be externally backed.
	 *	> 0 (wimlib error code) if another error occurred.
	 */
	int (*will_back_from_wim)(struct wim_dentry *dentry, struct apply_ctx *ctx);

	/*
	 * Size of the backend-specific extraction context.  It must contain
	 * 'struct apply_ctx' as its first member.
	 */
	size_t context_size;

	/*
	 * Set this if the extraction backend only supports extracting dentries
	 * that form a single tree, not multiple trees.
	 */
	bool single_tree_only;
};

#ifdef _WIN32
  extern const struct apply_operations win32_apply_ops;
#else
  extern const struct apply_operations unix_apply_ops;
#endif

#ifdef WITH_NTFS_3G
  extern const struct apply_operations ntfs_3g_apply_ops;
#endif

#endif /* _WIMLIB_APPLY_H */
