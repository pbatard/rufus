/*
 * win32_vss.c - Windows-specific code for creating VSS (Volume Shadow Copy
 * Service) snapshots.
 */

/*
 * Copyright (C) 2015-2023 Eric Biggers
 *
 * This file is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option) any
 * later version.
 *
 * This file is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this file; if not, see https://www.gnu.org/licenses/.
 */

#ifdef _WIN32

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "wimlib/win32_common.h"

#include <cguid.h>

#include "wimlib/error.h"
#include "wimlib/threads.h"
#include "wimlib/util.h"
#include "wimlib/win32_vss.h"

/*----------------------------------------------------------------------------*
 *                             VSS API declarations                           *
 *----------------------------------------------------------------------------*/

typedef GUID VSS_ID;
typedef LONGLONG VSS_TIMESTAMP;
typedef WCHAR *VSS_PWSZ;

typedef enum {
	VSS_BT_UNDEFINED      = 0,
	VSS_BT_FULL           = 1,
	VSS_BT_INCREMENTAL    = 2,
	VSS_BT_DIFFERENTIAL   = 3,
	VSS_BT_LOG            = 4,
	VSS_BT_COPY           = 5,
	VSS_BT_OTHER          = 6,
} VSS_BACKUP_TYPE;

typedef enum {
	VSS_SS_UNKNOWN                      = 0x00,
	VSS_SS_PREPARING                    = 0x01,
	VSS_SS_PROCESSING_PREPARE           = 0x02,
	VSS_SS_PREPARED                     = 0x03,
	VSS_SS_PROCESSING_PRECOMMIT         = 0x04,
	VSS_SS_PRECOMMITTED                 = 0x05,
	VSS_SS_PROCESSING_COMMIT            = 0x06,
	VSS_SS_COMMITTED                    = 0x07,
	VSS_SS_PROCESSING_POSTCOMMIT        = 0x08,
	VSS_SS_PROCESSING_PREFINALCOMMIT    = 0x09,
	VSS_SS_PREFINALCOMMITTED            = 0x0a,
	VSS_SS_PROCESSING_POSTFINALCOMMIT   = 0x0b,
	VSS_SS_CREATED                      = 0x0c,
	VSS_SS_ABORTED                      = 0x0d,
	VSS_SS_DELETED                      = 0x0e,
	VSS_SS_POSTCOMMITTED                = 0x0f,
	VSS_SS_COUNT                        = 0x10,
} VSS_SNAPSHOT_STATE;

typedef enum {
	VSS_VOLSNAP_ATTR_PERSISTENT             = 0x00000001,
	VSS_VOLSNAP_ATTR_NO_AUTORECOVERY        = 0x00000002,
	VSS_VOLSNAP_ATTR_CLIENT_ACCESSIBLE      = 0x00000004,
	VSS_VOLSNAP_ATTR_NO_AUTO_RELEASE        = 0x00000008,
	VSS_VOLSNAP_ATTR_NO_WRITERS             = 0x00000010,
	VSS_VOLSNAP_ATTR_TRANSPORTABLE          = 0x00000020,
	VSS_VOLSNAP_ATTR_NOT_SURFACED           = 0x00000040,
	VSS_VOLSNAP_ATTR_NOT_TRANSACTED         = 0x00000080,
	VSS_VOLSNAP_ATTR_HARDWARE_ASSISTED      = 0x00010000,
	VSS_VOLSNAP_ATTR_DIFFERENTIAL           = 0x00020000,
	VSS_VOLSNAP_ATTR_PLEX                   = 0x00040000,
	VSS_VOLSNAP_ATTR_IMPORTED               = 0x00080000,
	VSS_VOLSNAP_ATTR_EXPOSED_LOCALLY        = 0x00100000,
	VSS_VOLSNAP_ATTR_EXPOSED_REMOTELY       = 0x00200000,
	VSS_VOLSNAP_ATTR_AUTORECOVER            = 0x00400000,
	VSS_VOLSNAP_ATTR_ROLLBACK_RECOVERY      = 0x00800000,
	VSS_VOLSNAP_ATTR_DELAYED_POSTSNAPSHOT   = 0x01000000,
	VSS_VOLSNAP_ATTR_TXF_RECOVERY           = 0x02000000,
} VSS_VOLUME_SNAPSHOT_ATTRIBUTES;

typedef enum {
	VSS_CTX_BACKUP                      = 0,
	VSS_CTX_FILE_SHARE_BACKUP           = VSS_VOLSNAP_ATTR_NO_WRITERS,
	VSS_CTX_NAS_ROLLBACK                = ( ( VSS_VOLSNAP_ATTR_PERSISTENT | VSS_VOLSNAP_ATTR_NO_AUTO_RELEASE ) | VSS_VOLSNAP_ATTR_NO_WRITERS ),
	VSS_CTX_APP_ROLLBACK                = ( VSS_VOLSNAP_ATTR_PERSISTENT | VSS_VOLSNAP_ATTR_NO_AUTO_RELEASE ),
	VSS_CTX_CLIENT_ACCESSIBLE           = ( ( ( VSS_VOLSNAP_ATTR_PERSISTENT | VSS_VOLSNAP_ATTR_CLIENT_ACCESSIBLE ) | VSS_VOLSNAP_ATTR_NO_AUTO_RELEASE ) | VSS_VOLSNAP_ATTR_NO_WRITERS ),
	VSS_CTX_CLIENT_ACCESSIBLE_WRITERS   = ( ( VSS_VOLSNAP_ATTR_PERSISTENT | VSS_VOLSNAP_ATTR_CLIENT_ACCESSIBLE ) | VSS_VOLSNAP_ATTR_NO_AUTO_RELEASE ),
	VSS_CTX_ALL                         = 0xffffffff,
} VSS_SNAPSHOT_CONTEXT;

typedef struct {
	VSS_ID             m_SnapshotId;
	VSS_ID             m_SnapshotSetId;
	LONG               m_lSnapshotsCount;
	VSS_PWSZ           m_pwszSnapshotDeviceObject;
	VSS_PWSZ           m_pwszOriginalVolumeName;
	VSS_PWSZ           m_pwszOriginatingMachine;
	VSS_PWSZ           m_pwszServiceMachine;
	VSS_PWSZ           m_pwszExposedName;
	VSS_PWSZ           m_pwszExposedPath;
	VSS_ID             m_ProviderId;
	LONG               m_lSnapshotAttributes;
	VSS_TIMESTAMP      m_tsCreationTimestamp;
	VSS_SNAPSHOT_STATE m_eStatus;
} VSS_SNAPSHOT_PROP;

typedef struct IVssAsyncVTable IVssAsyncVTable;

typedef struct {
	IVssAsyncVTable *vtable;
} IVssAsync;

struct IVssAsyncVTable {
	void *QueryInterface;
	void *AddRef;
	ULONG (WINAPI *Release)(IVssAsync *this);
	void *Cancel;
	HRESULT (WINAPI *Wait)(IVssAsync *this, DWORD dwMilliseconds);
	void *QueryStatus;
};

typedef struct IVssBackupComponentsVTable IVssBackupComponentsVTable;

typedef struct {
	IVssBackupComponentsVTable *vtable;
} IVssBackupComponents;

struct IVssBackupComponentsVTable {
	void *QueryInterface;
	void *AddRef;
	ULONG (WINAPI *Release)(IVssBackupComponents *this);
	void *GetWriterComponentsCount;
	void *GetWriterComponents;
	HRESULT (WINAPI *InitializeForBackup)(IVssBackupComponents *this,
					      BSTR bstrXML);
	HRESULT (WINAPI *SetBackupState)(IVssBackupComponents *this,
					 BOOLEAN bSelectComponents,
					 BOOLEAN bBackupBootableSystemState,
					 VSS_BACKUP_TYPE backupType,
					 BOOLEAN bPartialFileSupport);
	void *InitializeForRestore;
	void *SetRestoreState;
	HRESULT (WINAPI *GatherWriterMetadata)(IVssBackupComponents *this,
					       IVssAsync **ppAsync);
	void *GetWriterMetadataCount;
	void *GetWriterMetadata;
	void *FreeWriterMetadata;
	void *AddComponent;
	HRESULT (WINAPI *PrepareForBackup)(IVssBackupComponents *this,
					   IVssAsync **ppAsync);
	void *AbortBackup;
	void *GatherWriterStatus;
	void *GetWriterStatusCount;
	void *FreeWriterStatus;
	void *GetWriterStatus;
	void *SetBackupSucceeded;
	void *SetBackupOptions;
	void *SetSelectedForRestore;
	void *SetRestoreOptions;
	void *SetAdditionalRestores;
	void *SetPreviousBackupStamp;
	void *SaveAsXML;
	void *BackupComplete;
	void *AddAlternativeLocationMapping;
	void *AddRestoreSubcomponent;
	void *SetFileRestoreStatus;
	void *AddNewTarget;
	void *SetRangesFilePath;
	void *PreRestore;
	void *PostRestore;
	HRESULT (WINAPI *SetContext)(IVssBackupComponents *this,
				     LONG lContext);
	HRESULT (WINAPI *StartSnapshotSet)(IVssBackupComponents *this,
					   VSS_ID *pSnapshotSetId);
	HRESULT (WINAPI *AddToSnapshotSet)(IVssBackupComponents *this,
					   VSS_PWSZ pwszVolumeName,
					   VSS_ID ProviderId,
					   VSS_ID *pidSnapshot);
	HRESULT (WINAPI *DoSnapshotSet)(IVssBackupComponents *this,
					IVssAsync **ppAsync);
	void *DeleteSnapshots;
	void *ImportSnapshots;
	void *BreakSnapshotSet;
	HRESULT (WINAPI *GetSnapshotProperties)(IVssBackupComponents *this,
						VSS_ID SnapshotId,
						VSS_SNAPSHOT_PROP *pprop);
	void *Query;
	void *IsVolumeSupported;
	void *DisableWriterClasses;
	void *EnableWriterClasses;
	void *DisableWriterInstances;
	void *ExposeSnapshot;
	void *RevertToSnapshot;
	void *QueryRevertStatus;
};

/*----------------------------------------------------------------------------*
 *                             VSS API initialization                         *
 *----------------------------------------------------------------------------*/

static bool vss_initialized;
static volatile uint16_t vss_initialization_mutex = 0;

/* vssapi.dll  */
static HANDLE hVssapi;
static HRESULT (WINAPI *func_CreateVssBackupComponentsInternal)(IVssBackupComponents **ppBackup);
static void (WINAPI *func_VssFreeSnapshotPropertiesInternal)(VSS_SNAPSHOT_PROP *pProp);

/* ole32.dll  */
static HANDLE hOle32;
static void (WINAPI *func_CoInitialize)(LPVOID *pvReserved);
static void (WINAPI *func_CoUninitialize)(void);

static bool
vss_global_init_impl(void)
{
	hVssapi = LoadLibrary(L"vssapi.dll");
	if (!hVssapi) {
		ERROR("vssapi.dll not found");
		goto err;
	}

	func_CreateVssBackupComponentsInternal =
		(void *)GetProcAddress(hVssapi, "CreateVssBackupComponentsInternal");
	if (!func_CreateVssBackupComponentsInternal) {
		ERROR("CreateVssBackupComponentsInternal() not found in vssapi.dll");
		goto err_vssapi;
	}

	func_VssFreeSnapshotPropertiesInternal =
		(void *)GetProcAddress(hVssapi, "VssFreeSnapshotPropertiesInternal");
	if (!func_VssFreeSnapshotPropertiesInternal) {
		ERROR("VssFreeSnapshotPropertiesInternal() not found in vssapi.dll");
		goto err_vssapi;
	}

	hOle32 = LoadLibrary(L"ole32.dll");
	if (!hOle32) {
		ERROR("ole32.dll not found");
		goto err_vssapi;
	}

	func_CoInitialize = (void *)GetProcAddress(hOle32, "CoInitialize");
	if (!func_CoInitialize) {
		ERROR("CoInitialize() not found in ole32.dll");
		goto err_ole32;
	}

	func_CoUninitialize = (void *)GetProcAddress(hOle32, "CoUninitialize");
	if (!func_CoUninitialize) {
		ERROR("CoUninitialize() not found in ole32.dll");
		goto err_ole32;
	}

	(*func_CoInitialize)(NULL);
	return true;

err_ole32:
	FreeLibrary(hOle32);
err_vssapi:
	FreeLibrary(hVssapi);
err:
	return false;
}

static bool
vss_global_init(void)
{
	bool ret = true;

	while (InterlockedIncrement16(&vss_initialization_mutex) >= 2) {
		InterlockedDecrement16(&vss_initialization_mutex);
		Sleep(100);
	}

	if (vss_initialized)
		goto out_unlock;

	vss_initialized = vss_global_init_impl();

	if (!vss_initialized) {
		ERROR("The Volume Shadow Copy Service (VSS) API could not be "
		      "initialized.");
		ret = false;
	}

out_unlock:
	InterlockedDecrement16(&vss_initialization_mutex);
	return ret;
}

void
vss_global_cleanup(void)
{
	while (InterlockedIncrement16(&vss_initialization_mutex) >= 2) {
		InterlockedDecrement16(&vss_initialization_mutex);
		Sleep(100);
	}

	if (!vss_initialized)
		goto out_unlock;

	(*func_CoUninitialize)();
	FreeLibrary(hOle32);
	FreeLibrary(hVssapi);
	vss_initialized = false;

out_unlock:
	InterlockedDecrement16(&vss_initialization_mutex);
}

/*----------------------------------------------------------------------------*
 *                             VSS implementation                             *
 *----------------------------------------------------------------------------*/

struct vss_snapshot_internal {
	struct vss_snapshot base;
	IVssBackupComponents *vss;
	VSS_SNAPSHOT_PROP props;
};

/* Delete the specified VSS snapshot.  */
void
vss_delete_snapshot(struct vss_snapshot *snapshot)
{
	struct vss_snapshot_internal *internal;

	internal = container_of(snapshot, struct vss_snapshot_internal, base);

	if (internal->props.m_pwszSnapshotDeviceObject)
		(*func_VssFreeSnapshotPropertiesInternal)(&internal->props);
	if (internal->vss)
		internal->vss->vtable->Release(internal->vss);
	FREE(internal);
}

static HRESULT
wait_and_release(IVssAsync *async)
{
	HRESULT res = async->vtable->Wait(async, INFINITE);

	async->vtable->Release(async);
	return res;
}

static bool
request_vss_snapshot(IVssBackupComponents *vss, wchar_t *volume,
		     VSS_ID *snapshot_id)
{
	HRESULT res;
	IVssAsync *async;

	res = vss->vtable->InitializeForBackup(vss, NULL);
	if (FAILED(res)) {
		ERROR("IVssBackupComponents.InitializeForBackup() error: %x",
		      (u32)res);
		return false;
	}

	res = vss->vtable->SetBackupState(vss, FALSE, TRUE, VSS_BT_COPY, FALSE);
	if (FAILED(res)) {
		ERROR("IVssBackupComponents.SetBackupState() error: %x",
		      (u32)res);
		return false;
	}

	res = vss->vtable->StartSnapshotSet(vss, snapshot_id);
	if (FAILED(res)) {
		ERROR("IVssBackupComponents.StartSnapshotSet() error: %x",
		      (u32)res);
		return false;
	}

	res = vss->vtable->AddToSnapshotSet(vss, volume, (GUID){ 0 }, snapshot_id);
	if (FAILED(res)) {
		ERROR("IVssBackupComponents.AddToSnapshotSet() error: %x",
		      (u32)res);
		return false;
	}

	res = vss->vtable->PrepareForBackup(vss, &async);
	if (FAILED(res)) {
		ERROR("IVssBackupComponents.PrepareForBackup() error: %x",
		      (u32)res);
		return false;
	}
	res = wait_and_release(async);
	if (FAILED(res)) {
		ERROR("IVssAsync.Wait() error while preparing for backup: %x",
		      (u32)res);
		return false;
	}

	res = vss->vtable->DoSnapshotSet(vss, &async);
	if (FAILED(res)) {
		ERROR("IVssBackupComponents.DoSnapshotSet() error: %x",
		      (u32)res);
		return false;
	}
	res = wait_and_release(async);
	if (FAILED(res)) {
		ERROR("IVssAsync.Wait() error while doing snapshot set: %x",
		      (u32)res);
		return false;
	}

	return true;
}

static bool
is_wow64(void)
{
	BOOL wow64 = FALSE;
	if (sizeof(size_t) == 4)
		IsWow64Process(GetCurrentProcess(), &wow64);
	return wow64;
}

/*
 * Create a VSS snapshot of the specified @volume.  Return the NT namespace path
 * to the snapshot root directory in @vss_path_ret and a handle to the snapshot
 * in @snapshot_ret.
 */
int
vss_create_snapshot(const wchar_t *source, UNICODE_STRING *vss_path_ret,
		    struct vss_snapshot **snapshot_ret)
{
	wchar_t *source_abspath;
	wchar_t volume[4];
	VSS_ID snapshot_id;
	struct vss_snapshot_internal *snapshot = NULL;
	IVssBackupComponents *vss;
	HRESULT res;
	int ret;

	source_abspath = realpath(source, NULL);
	if (!source_abspath) {
		ret = WIMLIB_ERR_NOMEM;
		goto err;
	}

	if (source_abspath[0] == L'\0' || source_abspath[1] != L':' ||
	    source_abspath[2] != L'\\') {
		ERROR("\"%ls\" (full path \"%ls\"): Path format not recognized",
		      source, source_abspath);
		ret = WIMLIB_ERR_UNSUPPORTED;
		goto err;
	}

	wsprintf(volume, L"%lc:\\", source_abspath[0]);

	snapshot = CALLOC(1, sizeof(*snapshot));
	if (!snapshot) {
		ret = WIMLIB_ERR_NOMEM;
		goto err;
	}

	if (!vss_global_init())
		goto vss_err;

	res = (*func_CreateVssBackupComponentsInternal)(&vss);
	if (FAILED(res)) {
		ERROR("CreateVssBackupComponents error: %x", (u32)res);
		goto vss_err;
	}

	snapshot->vss = vss;

	if (!request_vss_snapshot(vss, volume, &snapshot_id))
		goto vss_err;

	res = vss->vtable->GetSnapshotProperties(vss, snapshot_id, &snapshot->props);
	if (FAILED(res)) {
		ERROR("IVssBackupComponents.GetSnapshotProperties() error: %x",
		      (u32)res);
		goto vss_err;
	}

	if (wcsncmp(snapshot->props.m_pwszSnapshotDeviceObject, L"\\\\?\\", 4)) {
		ERROR("Unexpected volume shadow device path: %ls",
		      snapshot->props.m_pwszSnapshotDeviceObject);
		goto vss_err;
	}

	vss_path_ret->MaximumLength = sizeof(wchar_t) *
		(wcslen(snapshot->props.m_pwszSnapshotDeviceObject) +
		 1 + wcslen(&source_abspath[3]) + 1);
	vss_path_ret->Length = vss_path_ret->MaximumLength - sizeof(wchar_t);
	vss_path_ret->Buffer = HeapAlloc(GetProcessHeap(), 0,
					 vss_path_ret->MaximumLength);
	if (!vss_path_ret->Buffer) {
		ret = WIMLIB_ERR_NOMEM;
		goto err;
	}
	swprintf(vss_path_ret->Buffer,
		 vss_path_ret->MaximumLength / sizeof(wchar_t),
		 L"\\??\\%ls\\%ls",
		 &snapshot->props.m_pwszSnapshotDeviceObject[4],
		 &source_abspath[3]);
	*snapshot_ret = &snapshot->base;
	snapshot->base.refcnt = 1;
	ret = 0;
	goto out;

vss_err:
	ret = WIMLIB_ERR_SNAPSHOT_FAILURE;
	if (is_wow64()) {
		ERROR("64-bit Windows doesn't allow 32-bit applications to "
		      "create VSS snapshots.\n"
		      "        Run the 64-bit version of this application "
		      "instead.");
	} else {
		ERROR("A problem occurred while creating a VSS snapshot of "
		      "\"%ls\".\n"
		      "        Aborting the operation.", volume);
	}
err:
	if (snapshot)
		vss_delete_snapshot(&snapshot->base);
out:
	FREE(source_abspath);
	return ret;
}

#endif /* _WIN32 */
