/*
 * win32_vss.h - Declarations for managing VSS snapshots.  This header should
 * only be included by Windows-specific files.
 */

#ifndef _WIMLIB_WIN32_VSS_H
#define _WIMLIB_WIN32_VSS_H

#include "wimlib/win32_common.h"

/* A reference counter for a VSS snapshot.  This is embedded in another data
 * structure only visible to win32_vss.c.  */
struct vss_snapshot {
	size_t refcnt;
};

void
vss_delete_snapshot(struct vss_snapshot *snapshot);

/* Acquire a reference to the specified VSS snapshot.  */
static inline struct vss_snapshot *
vss_get_snapshot(struct vss_snapshot *snapshot)
{
	if (snapshot)
		snapshot->refcnt++;
	return snapshot;
}

/* Release a reference to the specified VSS snapshot.  When the last reference
 * is released, the snapshot is deleted.  */
static inline void
vss_put_snapshot(struct vss_snapshot *snapshot)
{
	if (snapshot && --snapshot->refcnt == 0)
		vss_delete_snapshot(snapshot);
}

int
vss_create_snapshot(const wchar_t *source, UNICODE_STRING *vss_path_ret,
		    struct vss_snapshot **snapshot_ret);

void
vss_global_cleanup(void);

#endif /* _WIMLIB_WIN32_VSS_H */
