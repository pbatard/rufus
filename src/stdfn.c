/*
 * Rufus: The Reliable USB Formatting Utility
 * Standard Windows function calls
 * Copyright © 2013-2018 Pete Batard <pete@akeo.ie>
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
#ifdef _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#endif

#include <windows.h>
#include <sddl.h>

#include "rufus.h"
#include "missing.h"
#include "resource.h"
#include "msapi_utf8.h"
#include "localization.h"

#include "settings.h"

int  nWindowsVersion = WINDOWS_UNDEFINED;
int  nWindowsBuildNumber = -1;
char WindowsVersionStr[128] = "Windows ";

// __popcnt16, __popcnt, __popcnt64 are not available for ARM :(
uint8_t popcnt8(uint8_t val)
{
	static const uint8_t nibble_lookup[16] = {
		0, 1, 1, 2, 1, 2, 2, 3,
		1, 2, 2, 3, 2, 3, 3, 4
	};
	return nibble_lookup[val & 0x0F] + nibble_lookup[val >> 4];
}

/*
 * Hash table functions - modified From glibc 2.3.2:
 * [Aho,Sethi,Ullman] Compilers: Principles, Techniques and Tools, 1986
 * [Knuth]            The Art of Computer Programming, part 3 (6.4)
 */

/*
 * For the used double hash method the table size has to be a prime. To
 * correct the user given table size we need a prime test.  This trivial
 * algorithm is adequate because the code is called only during init and
 * the number is likely to be small
 */
static uint32_t isprime(uint32_t number)
{
	// no even number will be passed
	uint32_t divider = 3;

	while((divider * divider < number) && (number % divider != 0))
		divider += 2;

	return (number % divider != 0);
}

/*
 * Before using the hash table we must allocate memory for it.
 * We allocate one element more as the found prime number says.
 * This is done for more effective indexing as explained in the
 * comment for the hash function.
 */
BOOL htab_create(uint32_t nel, htab_table* htab)
{
	if (htab == NULL) {
		return FALSE;
	}
	if (htab->table != NULL) {
		uprintf("warning: htab_create() was called with a non empty table");
		return FALSE;
	}

	// Change nel to the first prime number not smaller as nel.
	nel |= 1;
	while(!isprime(nel))
		nel += 2;

	htab->size = nel;
	htab->filled = 0;

	// allocate memory and zero out.
	htab->table = (htab_entry*)calloc(htab->size + 1, sizeof(htab_entry));
	if (htab->table == NULL) {
		uprintf("could not allocate space for hash table\n");
		return FALSE;
	}

	return TRUE;
}

/* After using the hash table it has to be destroyed.  */
void htab_destroy(htab_table* htab)
{
	size_t i;

	if ((htab == NULL) || (htab->table == NULL)) {
		return;
	}

	for (i=0; i<htab->size+1; i++) {
		if (htab->table[i].used) {
			safe_free(htab->table[i].str);
		}
	}
	htab->filled = 0; htab->size = 0;
	safe_free(htab->table);
	htab->table = NULL;
}

/*
 * This is the search function. It uses double hashing with open addressing.
 * We use a trick to speed up the lookup. The table is created with one
 * more element available. This enables us to use the index zero special.
 * This index will never be used because we store the first hash index in
 * the field used where zero means not used. Every other value means used.
 * The used field can be used as a first fast comparison for equality of
 * the stored and the parameter value. This helps to prevent unnecessary
 * expensive calls of strcmp.
 */
uint32_t htab_hash(char* str, htab_table* htab)
{
	uint32_t hval, hval2;
	uint32_t idx;
	uint32_t r = 0;
	int c;
	char* sz = str;

	if ((htab == NULL) || (htab->table == NULL) || (str == NULL)) {
		return 0;
	}

	// Compute main hash value using sdbm's algorithm (empirically
	// shown to produce half the collisions as djb2's).
	// See http://www.cse.yorku.ca/~oz/hash.html
	while ((c = *sz++) != 0)
		r = c + (r << 6) + (r << 16) - r;
	if (r == 0)
		++r;

	// compute table hash: simply take the modulus
	hval = r % htab->size;
	if (hval == 0)
		++hval;

	// Try the first index
	idx = hval;

	if (htab->table[idx].used) {
		if ( (htab->table[idx].used == hval)
		  && (safe_strcmp(str, htab->table[idx].str) == 0) ) {
			// existing hash
			return idx;
		}
		// uprintf("hash collision ('%s' vs '%s')\n", str, htab->table[idx].str);

		// Second hash function, as suggested in [Knuth]
		hval2 = 1 + hval % (htab->size - 2);

		do {
			// Because size is prime this guarantees to step through all available indexes
			if (idx <= hval2) {
				idx = ((uint32_t)htab->size) + idx - hval2;
			} else {
				idx -= hval2;
			}

			// If we visited all entries leave the loop unsuccessfully
			if (idx == hval) {
				break;
			}

			// If entry is found use it.
			if ( (htab->table[idx].used == hval)
			  && (safe_strcmp(str, htab->table[idx].str) == 0) ) {
				return idx;
			}
		}
		while (htab->table[idx].used);
	}

	// Not found => New entry

	// If the table is full return an error
	if (htab->filled >= htab->size) {
		uprintf("hash table is full (%d entries)", htab->size);
		return 0;
	}

	safe_free(htab->table[idx].str);
	htab->table[idx].used = hval;
	htab->table[idx].str = (char*) malloc(safe_strlen(str)+1);
	if (htab->table[idx].str == NULL) {
		uprintf("could not duplicate string for hash table\n");
		return 0;
	}
	memcpy(htab->table[idx].str, str, safe_strlen(str)+1);
	++htab->filled;

	return idx;
}

BOOL is_x64(void)
{
	BOOL ret = FALSE;
	PF_TYPE_DECL(WINAPI, BOOL, IsWow64Process, (HANDLE, PBOOL));
	// Detect if we're running a 32 or 64 bit system
	if (sizeof(uintptr_t) < 8) {
		PF_INIT(IsWow64Process, Kernel32);
		if (pfIsWow64Process != NULL) {
			(*pfIsWow64Process)(GetCurrentProcess(), &ret);
		}
	} else {
		ret = TRUE;
	}
	return ret;
}

int GetCpuArch(void)
{
	SYSTEM_INFO info = { 0 };
	GetNativeSystemInfo(&info);
	switch (info.wProcessorArchitecture) {
	case PROCESSOR_ARCHITECTURE_AMD64:
		return CPU_ARCH_X86_64;
	case PROCESSOR_ARCHITECTURE_INTEL:
		return CPU_ARCH_X86_64;
	// TODO: Set this back to PROCESSOR_ARCHITECTURE_ARM64 when the MinGW headers have it
	case 12:
		return CPU_ARCH_ARM_64;
	case PROCESSOR_ARCHITECTURE_ARM:
		return CPU_ARCH_ARM_32;
	default:
		return CPU_ARCH_UNDEFINED;
	}
}

// From smartmontools os_win32.cpp
void GetWindowsVersion(void)
{
	OSVERSIONINFOEXA vi, vi2;
	const char* w = 0;
	const char* w64 = "32 bit";
	char *vptr;
	size_t vlen;
	unsigned major, minor;
	ULONGLONG major_equal, minor_equal;
	BOOL ws;

	nWindowsVersion = WINDOWS_UNDEFINED;
	static_strcpy(WindowsVersionStr, "Windows Undefined");

	memset(&vi, 0, sizeof(vi));
	vi.dwOSVersionInfoSize = sizeof(vi);
	if (!GetVersionExA((OSVERSIONINFOA *)&vi)) {
		memset(&vi, 0, sizeof(vi));
		vi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOA);
		if (!GetVersionExA((OSVERSIONINFOA *)&vi))
			return;
	}

	if (vi.dwPlatformId == VER_PLATFORM_WIN32_NT) {

		if (vi.dwMajorVersion > 6 || (vi.dwMajorVersion == 6 && vi.dwMinorVersion >= 2)) {
			// Starting with Windows 8.1 Preview, GetVersionEx() does no longer report the actual OS version
			// See: http://msdn.microsoft.com/en-us/library/windows/desktop/dn302074.aspx
			// And starting with Windows 10 Preview 2, Windows enforces the use of the application/supportedOS
			// manifest in order for VerSetConditionMask() to report the ACTUAL OS major and minor...

			major_equal = VerSetConditionMask(0, VER_MAJORVERSION, VER_EQUAL);
			for (major = vi.dwMajorVersion; major <= 9; major++) {
				memset(&vi2, 0, sizeof(vi2));
				vi2.dwOSVersionInfoSize = sizeof(vi2); vi2.dwMajorVersion = major;
				if (!VerifyVersionInfoA(&vi2, VER_MAJORVERSION, major_equal))
					continue;
				if (vi.dwMajorVersion < major) {
					vi.dwMajorVersion = major; vi.dwMinorVersion = 0;
				}

				minor_equal = VerSetConditionMask(0, VER_MINORVERSION, VER_EQUAL);
				for (minor = vi.dwMinorVersion; minor <= 9; minor++) {
					memset(&vi2, 0, sizeof(vi2)); vi2.dwOSVersionInfoSize = sizeof(vi2);
					vi2.dwMinorVersion = minor;
					if (!VerifyVersionInfoA(&vi2, VER_MINORVERSION, minor_equal))
						continue;
					vi.dwMinorVersion = minor;
					break;
				}

				break;
			}
		}

		if (vi.dwMajorVersion <= 0xf && vi.dwMinorVersion <= 0xf) {
			ws = (vi.wProductType <= VER_NT_WORKSTATION);
			nWindowsVersion = vi.dwMajorVersion << 4 | vi.dwMinorVersion;
			switch (nWindowsVersion) {
			case 0x51: w = "XP";
				break;
			case 0x52: w = (!GetSystemMetrics(89)?"Server 2003":"Server 2003_R2");
				break;
			case 0x60: w = (ws?"Vista":"Server 2008");
				break;
			case 0x61: w = (ws?"7":"Server 2008_R2");
				break;
			case 0x62: w = (ws?"8":"Server 2012");
				break;
			case 0x63: w = (ws?"8.1":"Server 2012_R2");
				break;
			case 0x64: w = (ws?"10 (Preview 1)":"Server 10 (Preview 1)");
				break;
			// Starting with Windows 10 Preview 2, the major is the same as the public-facing version
			case 0xA0: w = (ws?"10":"Server 2016");
				break;
			default:
				if (nWindowsVersion < 0x51)
					nWindowsVersion = WINDOWS_UNSUPPORTED;
				else
					w = "11 or later";
				break;
			}
		}
	}

	if (is_x64())
		w64 = "64-bit";

	vptr = &WindowsVersionStr[sizeof("Windows ") - 1];
	vlen = sizeof(WindowsVersionStr) - sizeof("Windows ") - 1;
	if (!w)
		safe_sprintf(vptr, vlen, "%s %u.%u %s", (vi.dwPlatformId==VER_PLATFORM_WIN32_NT?"NT":"??"),
			(unsigned)vi.dwMajorVersion, (unsigned)vi.dwMinorVersion, w64);
	else if (vi.wServicePackMinor)
		safe_sprintf(vptr, vlen, "%s SP%u.%u %s", w, vi.wServicePackMajor, vi.wServicePackMinor, w64);
	else if (vi.wServicePackMajor)
		safe_sprintf(vptr, vlen, "%s SP%u %s", w, vi.wServicePackMajor, w64);
	else
		safe_sprintf(vptr, vlen, "%s %s", w, w64);

	// Add the build number for Windows 8.0 and later
	nWindowsBuildNumber = vi.dwBuildNumber;
	if (nWindowsVersion >= 0x62) {
		vptr = &WindowsVersionStr[safe_strlen(WindowsVersionStr)];
		vlen = sizeof(WindowsVersionStr) - safe_strlen(WindowsVersionStr) - 1;
		safe_sprintf(vptr, vlen, " (Build %d)", nWindowsBuildNumber);
	}

}

/*
 * String array manipulation
 */
void StrArrayCreate(StrArray* arr, uint32_t initial_size)
{
	if (arr == NULL) return;
	arr->Max = initial_size; arr->Index = 0;
	arr->String = (char**)calloc(arr->Max, sizeof(char*));
	if (arr->String == NULL)
		uprintf("Could not allocate string array\n");
}

int32_t StrArrayAdd(StrArray* arr, const char* str, BOOL duplicate)
{
	char** old_table;
	if ((arr == NULL) || (arr->String == NULL) || (str == NULL))
		return -1;
	if (arr->Index == arr->Max) {
		arr->Max *= 2;
		old_table = arr->String;
		arr->String = (char**)realloc(arr->String, arr->Max*sizeof(char*));
		if (arr->String == NULL) {
			free(old_table);
			uprintf("Could not reallocate string array\n");
			return -1;
		}
	}
	arr->String[arr->Index] = (duplicate)?safe_strdup(str):(char*)str;
	if (arr->String[arr->Index] == NULL) {
		uprintf("Could not store string in array\n");
		return -1;
	}
	return arr->Index++;
}

int32_t StrArrayFind(StrArray* arr, const char* str)
{
	uint32_t i;
	if ((str == NULL) || (arr == NULL) || (arr->String == NULL))
		return -1;
	for (i = 0; i<arr->Index; i++) {
		if (strcmp(arr->String[i], str) == 0)
			return (int32_t)i;
	}
	return -1;
}

void StrArrayClear(StrArray* arr)
{
	uint32_t i;
	if ((arr == NULL) || (arr->String == NULL))
		return;
	for (i=0; i<arr->Index; i++) {
		safe_free(arr->String[i]);
	}
	arr->Index = 0;
}

void StrArrayDestroy(StrArray* arr)
{
	StrArrayClear(arr);
	if (arr != NULL)
		safe_free(arr->String);
}

/*
 * Retrieve the SID of the current user. The returned PSID must be freed by the caller using LocalFree()
 */
static PSID GetSID(void) {
	TOKEN_USER* tu = NULL;
	DWORD len;
	HANDLE token;
	PSID ret = NULL;
	char* psid_string = NULL;

	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
		uprintf("OpenProcessToken failed: %s\n", WindowsErrorString());
		return NULL;
	}

	if (!GetTokenInformation(token, TokenUser, tu, 0, &len)) {
		if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
			uprintf("GetTokenInformation (pre) failed: %s\n", WindowsErrorString());
			return NULL;
		}
		tu = (TOKEN_USER*)calloc(1, len);
	}
	if (tu == NULL) {
		return NULL;
	}

	if (GetTokenInformation(token, TokenUser, tu, len, &len)) {
		/*
		 * now of course, the interesting thing is that if you return tu->User.Sid
		 * but free tu, the PSID pointer becomes invalid after a while.
		 * The workaround? Convert to string then back to PSID
		 */
		if (!ConvertSidToStringSidA(tu->User.Sid, &psid_string)) {
			uprintf("Unable to convert SID to string: %s\n", WindowsErrorString());
			ret = NULL;
		} else {
			if (!ConvertStringSidToSidA(psid_string, &ret)) {
				uprintf("Unable to convert string back to SID: %s\n", WindowsErrorString());
				ret = NULL;
			}
			// MUST use LocalFree()
			LocalFree(psid_string);
		}
	} else {
		ret = NULL;
		uprintf("GetTokenInformation (real) failed: %s\n", WindowsErrorString());
	}
	free(tu);
	return ret;
}

/*
 * read or write I/O to a file
 * buffer is allocated by the procedure. path is UTF-8
 */
BOOL FileIO(BOOL save, char* path, char** buffer, DWORD* size)
{
	SECURITY_ATTRIBUTES s_attr, *sa = NULL;
	SECURITY_DESCRIPTOR s_desc;
	PSID sid = NULL;
	HANDLE handle;
	BOOL r;
	BOOL ret = FALSE;

	// Change the owner from admin to regular user
	sid = GetSID();
	if ( (sid != NULL)
	  && InitializeSecurityDescriptor(&s_desc, SECURITY_DESCRIPTOR_REVISION)
	  && SetSecurityDescriptorOwner(&s_desc, sid, FALSE) ) {
		s_attr.nLength = sizeof(SECURITY_ATTRIBUTES);
		s_attr.bInheritHandle = FALSE;
		s_attr.lpSecurityDescriptor = &s_desc;
		sa = &s_attr;
	} else {
		uprintf("Could not set security descriptor: %s\n", WindowsErrorString());
	}

	if (!save) {
		*buffer = NULL;
	}
	handle = CreateFileU(path, save?GENERIC_WRITE:GENERIC_READ, FILE_SHARE_READ,
		sa, save?CREATE_ALWAYS:OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

	if (handle == INVALID_HANDLE_VALUE) {
		uprintf("Could not %s file '%s'\n", save?"create":"open", path);
		goto out;
	}

	if (save) {
		r = WriteFile(handle, *buffer, *size, size, NULL);
	} else {
		*size = GetFileSize(handle, NULL);
		*buffer = (char*)malloc(*size);
		if (*buffer == NULL) {
			uprintf("Could not allocate buffer for reading file\n");
			goto out;
		}
		r = ReadFile(handle, *buffer, *size, size, NULL);
	}

	if (!r) {
		uprintf("I/O Error: %s\n", WindowsErrorString());
		goto out;
	}

	PrintInfoDebug(0, save?MSG_216:MSG_215, path);
	ret = TRUE;

out:
	CloseHandle(handle);
	if (!ret) {
		// Only leave a buffer allocated if successful
		*size = 0;
		if (!save) {
			safe_free(*buffer);
		}
	}
	return ret;
}

unsigned char* GetResource(HMODULE module, char* name, char* type, const char* desc, DWORD* len, BOOL duplicate)
{
	HGLOBAL res_handle;
	HRSRC res;
	unsigned char* p = NULL;

	res = FindResourceA(module, name, type);
	if (res == NULL) {
		uprintf("Could not locate resource '%s': %s\n", desc, WindowsErrorString());
		goto out;
	}
	res_handle = LoadResource(module, res);
	if (res_handle == NULL) {
		uprintf("Could not load resource '%s': %s\n", desc, WindowsErrorString());
		goto out;
	}
	*len = SizeofResource(module, res);

	if (duplicate) {
		p = (unsigned char*)malloc(*len);
		if (p == NULL) {
			uprintf("Coult not allocate resource '%s'\n", desc);
			goto out;
		}
		memcpy(p, LockResource(res_handle), *len);
	} else {
		p = (unsigned char*)LockResource(res_handle);
	}

out:
	return p;
}

DWORD GetResourceSize(HMODULE module, char* name, char* type, const char* desc)
{
	DWORD len = 0;
	return (GetResource(module, name, type, desc, &len, FALSE) == NULL)?0:len;
}

// Run a console command, with optional redirection of stdout and stderr to our log
DWORD RunCommand(const char* cmd, const char* dir, BOOL log)
{
	DWORD ret, dwRead, dwAvail, dwPipeSize = 4096;
	STARTUPINFOA si = {0};
	PROCESS_INFORMATION pi = {0};
	SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
	HANDLE hOutputRead = INVALID_HANDLE_VALUE, hOutputWrite = INVALID_HANDLE_VALUE;
	static char* output;

	si.cb = sizeof(si);
	if (log) {
		// NB: The size of a pipe is a suggestion, NOT an absolute guarantee
		// This means that you may get a pipe of 4K even if you requested 1K
		if (!CreatePipe(&hOutputRead, &hOutputWrite, &sa, dwPipeSize)) {
			ret = GetLastError();
			uprintf("Could not set commandline pipe: %s", WindowsErrorString());
			goto out;
		}
		si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
		si.wShowWindow = SW_HIDE;
		si.hStdOutput = hOutputWrite;
		si.hStdError = hOutputWrite;
	}

	if (!CreateProcessU(NULL, cmd, NULL, NULL, TRUE,
		NORMAL_PRIORITY_CLASS | CREATE_NO_WINDOW, NULL, dir, &si, &pi)) {
		ret = GetLastError();
		uprintf("Unable to launch command '%s': %s", cmd, WindowsErrorString());
		goto out;
	}

	if (log) {
		while (1) {
			// coverity[string_null]
			if (PeekNamedPipe(hOutputRead, NULL, dwPipeSize, NULL, &dwAvail, NULL)) {
				if (dwAvail != 0) {
					output = malloc(dwAvail + 1);
					if ((output != NULL) && (ReadFile(hOutputRead, output, dwAvail, &dwRead, NULL)) && (dwRead != 0)) {
						output[dwAvail] = 0;
						// coverity[tainted_string]
						uprintf(output);
					}
					free(output);
				}
			}
			if (WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0)
				break;
			Sleep(100);
		};
	} else {
		WaitForSingleObject(pi.hProcess, INFINITE);
	}

	if (!GetExitCodeProcess(pi.hProcess, &ret))
		ret = GetLastError();
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);

out:
	safe_closehandle(hOutputWrite);
	safe_closehandle(hOutputRead);
	return ret;
}

BOOL CompareGUID(const GUID *guid1, const GUID *guid2) {
	if ((guid1 != NULL) && (guid2 != NULL)) {
		return (memcmp(guid1, guid2, sizeof(GUID)) == 0);
	}
	return FALSE;
}

static BOOL CALLBACK EnumFontFamExProc(const LOGFONTA *lpelfe,
	const TEXTMETRICA *lpntme, DWORD FontType, LPARAM lParam)
{
	return TRUE;
}

BOOL IsFontAvailable(const char* font_name)
{
	BOOL r;
	LOGFONTA lf = { 0 };
	HDC hDC = GetDC(hMainDialog);

	if (font_name == NULL) {
		safe_release_dc(hMainDialog, hDC);
		return FALSE;
	}

	lf.lfCharSet = DEFAULT_CHARSET;
	safe_strcpy(lf.lfFaceName, LF_FACESIZE, font_name);

	r = EnumFontFamiliesExA(hDC, &lf, EnumFontFamExProc, 0, 0);
	safe_release_dc(hMainDialog, hDC);
	return r;
}

/*
 * Set or restore a Local Group Policy DWORD key indexed by szPath/SzPolicy
 */
#pragma push_macro("INTERFACE")
#undef  INTERFACE
#define INTERFACE IGroupPolicyObject
#define REGISTRY_EXTENSION_GUID { 0x35378EACL, 0x683F, 0x11D2, {0xA8, 0x9A, 0x00, 0xC0, 0x4F, 0xBB, 0xCF, 0xA2} }
#define GPO_OPEN_LOAD_REGISTRY  1
#define GPO_SECTION_MACHINE     2
typedef enum _GROUP_POLICY_OBJECT_TYPE {
	GPOTypeLocal = 0, GPOTypeRemote, GPOTypeDS
} GROUP_POLICY_OBJECT_TYPE, *PGROUP_POLICY_OBJECT_TYPE;
DECLARE_INTERFACE_(IGroupPolicyObject, IUnknown) {
	STDMETHOD(QueryInterface) (THIS_ REFIID riid, LPVOID *ppvObj) PURE;
	STDMETHOD_(ULONG, AddRef) (THIS) PURE;
	STDMETHOD_(ULONG, Release) (THIS) PURE;
	STDMETHOD(New) (THIS_ LPOLESTR pszDomainName, LPOLESTR pszDisplayName, DWORD dwFlags) PURE;
	STDMETHOD(OpenDSGPO) (THIS_ LPOLESTR pszPath, DWORD dwFlags) PURE;
	STDMETHOD(OpenLocalMachineGPO) (THIS_ DWORD dwFlags) PURE;
	STDMETHOD(OpenRemoteMachineGPO) (THIS_ LPOLESTR pszComputerName, DWORD dwFlags) PURE;
	STDMETHOD(Save) (THIS_ BOOL bMachine, BOOL bAdd,GUID *pGuidExtension, GUID *pGuid) PURE;
	STDMETHOD(Delete) (THIS) PURE;
	STDMETHOD(GetName) (THIS_ LPOLESTR pszName, int cchMaxLength) PURE;
	STDMETHOD(GetDisplayName) (THIS_ LPOLESTR pszName, int cchMaxLength) PURE;
	STDMETHOD(SetDisplayName) (THIS_ LPOLESTR pszName) PURE;
	STDMETHOD(GetPath) (THIS_ LPOLESTR pszPath, int cchMaxPath) PURE;
	STDMETHOD(GetDSPath) (THIS_ DWORD dwSection, LPOLESTR pszPath ,int cchMaxPath) PURE;
	STDMETHOD(GetFileSysPath) (THIS_ DWORD dwSection, LPOLESTR pszPath, int cchMaxPath) PURE;
	STDMETHOD(GetRegistryKey) (THIS_ DWORD dwSection, HKEY *hKey) PURE;
	STDMETHOD(GetOptions) (THIS_ DWORD *dwOptions) PURE;
	STDMETHOD(SetOptions) (THIS_ DWORD dwOptions, DWORD dwMask) PURE;
	STDMETHOD(GetType) (THIS_ GROUP_POLICY_OBJECT_TYPE *gpoType) PURE;
	STDMETHOD(GetMachineName) (THIS_ LPOLESTR pszName, int cchMaxLength) PURE;
	STDMETHOD(GetPropertySheetPages) (THIS_ HPROPSHEETPAGE **hPages, UINT *uPageCount) PURE;
};
typedef IGroupPolicyObject *LPGROUPPOLICYOBJECT;

// I've seen rare cases where pLGPO->lpVtbl->Save(...) gets stuck, which prevents the
// application from launching altogether. To alleviate this, use a thread that we can
// terminate if needed...
typedef struct {
	BOOL bRestore;
	BOOL* bExistingKey;
	const char* szPath;
	const char* szPolicy;
	DWORD dwValue;
} SetLGP_Params;

DWORD WINAPI SetLGPThread(LPVOID param)
{
	SetLGP_Params* p = (SetLGP_Params*)param;
	LONG r;
	DWORD disp, regtype, val=0, val_size=sizeof(DWORD);
	HRESULT hr;
	IGroupPolicyObject* pLGPO;
	// Along with global 'existing_key', this static value is used to restore initial state
	static DWORD original_val;
	HKEY path_key = NULL, policy_key = NULL;
	// MSVC is finicky about these ones => redefine them
	const IID my_IID_IGroupPolicyObject =
		{ 0xea502723L, 0xa23d, 0x11d1, { 0xa7, 0xd3, 0x0, 0x0, 0xf8, 0x75, 0x71, 0xe3 } };
	const IID my_CLSID_GroupPolicyObject =
		{ 0xea502722L, 0xa23d, 0x11d1, { 0xa7, 0xd3, 0x0, 0x0, 0xf8, 0x75, 0x71, 0xe3 } };
	GUID ext_guid = REGISTRY_EXTENSION_GUID;
	// Can be anything really
	GUID snap_guid = { 0x3D271CFCL, 0x2BC6, 0x4AC2, {0xB6, 0x33, 0x3B, 0xDF, 0xF5, 0xBD, 0xAB, 0x2A} };

	// Reinitialize COM since it's not shared between threads
	IGNORE_RETVAL(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED));

	// We need an IGroupPolicyObject instance to set a Local Group Policy
	hr = CoCreateInstance(&my_CLSID_GroupPolicyObject, NULL, CLSCTX_INPROC_SERVER, &my_IID_IGroupPolicyObject, (LPVOID*)&pLGPO);
	if (FAILED(hr)) {
		ubprintf("SetLGP: CoCreateInstance failed; hr = %lx", hr);
		goto error;
	}

	hr = pLGPO->lpVtbl->OpenLocalMachineGPO(pLGPO, GPO_OPEN_LOAD_REGISTRY);
	if (FAILED(hr)) {
		ubprintf("SetLGP: OpenLocalMachineGPO failed - error %lx", hr);
		goto error;
	}

	hr = pLGPO->lpVtbl->GetRegistryKey(pLGPO, GPO_SECTION_MACHINE, &path_key);
	if (FAILED(hr)) {
		ubprintf("SetLGP: GetRegistryKey failed - error %lx", hr);
		goto error;
	}

	// The DisableSystemRestore is set in Software\Policies\Microsoft\Windows\DeviceInstall\Settings
	r = RegCreateKeyExA(path_key, p->szPath, 0, NULL, 0, KEY_SET_VALUE | KEY_QUERY_VALUE,
		NULL, &policy_key, &disp);
	if (r != ERROR_SUCCESS) {
		ubprintf("SetLGP: Failed to open LGPO path %s - error %lx", p->szPath, hr);
		policy_key = NULL;
		goto error;
	}

	if ((disp == REG_OPENED_EXISTING_KEY) && (!p->bRestore) && (!(*(p->bExistingKey)))) {
		// backup existing value for restore
		*(p->bExistingKey) = TRUE;
		regtype = REG_DWORD;
		r = RegQueryValueExA(policy_key, p->szPolicy, NULL, &regtype, (LPBYTE)&original_val, &val_size);
		if (r == ERROR_FILE_NOT_FOUND) {
			// The Key exists but not its value, which is OK
			*(p->bExistingKey) = FALSE;
		} else if (r != ERROR_SUCCESS) {
			ubprintf("SetLGP: Failed to read original %s policy value - error %lx", p->szPolicy, r);
		}
	}

	if ((!p->bRestore) || (*(p->bExistingKey))) {
		val = (p->bRestore)?original_val:p->dwValue;
		r = RegSetValueExA(policy_key, p->szPolicy, 0, REG_DWORD, (BYTE*)&val, sizeof(val));
	} else {
		r = RegDeleteValueA(policy_key, p->szPolicy);
	}
	if (r != ERROR_SUCCESS) {
		ubprintf("SetLGP: RegSetValueEx / RegDeleteValue failed - error %lx", r);
	}
	RegCloseKey(policy_key);
	policy_key = NULL;

	// Apply policy
	hr = pLGPO->lpVtbl->Save(pLGPO, TRUE, (p->bRestore)?FALSE:TRUE, &ext_guid, &snap_guid);
	if (hr != S_OK) {
		ubprintf("SetLGP: Unable to apply %s policy - error %lx", p->szPolicy, hr);
		goto error;
	} else {
		if ((!p->bRestore) || (*(p->bExistingKey))) {
			ubprintf("SetLGP: Successfully %s %s policy to 0x%08lX", (p->bRestore)?"restored":"set", p->szPolicy, val);
		} else {
			ubprintf("SetLGP: Successfully removed %s policy key", p->szPolicy);
		}
	}

	RegCloseKey(path_key);
	pLGPO->lpVtbl->Release(pLGPO);
	return TRUE;

error:
	if (path_key != NULL)
		RegCloseKey(path_key);
	if (pLGPO != NULL)
		pLGPO->lpVtbl->Release(pLGPO);
	return FALSE;
}
#pragma pop_macro("INTERFACE")

BOOL SetLGP(BOOL bRestore, BOOL* bExistingKey, const char* szPath, const char* szPolicy, DWORD dwValue)
{
	SetLGP_Params params = {bRestore, bExistingKey, szPath, szPolicy, dwValue};
	DWORD r = FALSE;
	HANDLE thread_id;

	if (ReadSettingBool(SETTING_DISABLE_LGP)) {
		ubprintf("LPG handling disabled, per settings");
		return FALSE;
	}

	thread_id = CreateThread(NULL, 0, SetLGPThread, (LPVOID)&params, 0, NULL);
	if (thread_id == NULL) {
		ubprintf("SetLGP: Unable to start thread");
		return FALSE;
	}
	if (WaitForSingleObject(thread_id, 5000) != WAIT_OBJECT_0) {
		ubprintf("SetLGP: Killing stuck thread!");
		TerminateThread(thread_id, 0);
		CloseHandle(thread_id);
		return FALSE;
	}
	if (!GetExitCodeThread(thread_id, &r))
		return FALSE;
	return (BOOL) r;
}

/*
 * This call tries to evenly balance the affinities for an array of
 * num_threads, according to the number of cores at our disposal...
 */
BOOL SetThreadAffinity(DWORD_PTR* thread_affinity, size_t num_threads)
{
	size_t i, j, pc;
	DWORD_PTR affinity, dummy;

	memset(thread_affinity, 0, num_threads * sizeof(DWORD_PTR));
	if (!GetProcessAffinityMask(GetCurrentProcess(), &affinity, &dummy))
		return FALSE;
	uuprintf("\r\nThread affinities:");
	uuprintf("  avail:\t%s", printbitslz(affinity));

	// If we don't have enough virtual cores to evenly spread our load forget it
	pc = popcnt64(affinity);
	if (pc < num_threads)
		return FALSE;

	// Spread the affinity as evenly as we can
	thread_affinity[num_threads - 1] = affinity;
	for (i = 0; i < num_threads - 1; i++) {
		for (j = 0; j < pc / num_threads; j++) {
			thread_affinity[i] |= affinity & (-1LL * affinity);
			affinity ^= affinity & (-1LL * affinity);
		}
		uuprintf("  thr_%d:\t%s", i, printbitslz(thread_affinity[i]));
		thread_affinity[num_threads - 1] ^= thread_affinity[i];
	}
	uuprintf("  thr_%d:\t%s", i, printbitslz(thread_affinity[i]));
	return TRUE;
}

/*
 * Returns true if:
 * 1. The OS supports UAC, UAC is on, and the current process runs elevated, or
 * 2. The OS doesn't support UAC or UAC is off, and the process is being run by a member of the admin group
 */
BOOL IsCurrentProcessElevated(void)
{
	BOOL r = FALSE;
	DWORD size;
	HANDLE token = INVALID_HANDLE_VALUE;
	TOKEN_ELEVATION te;
	SID_IDENTIFIER_AUTHORITY auth = { SECURITY_NT_AUTHORITY };
	PSID psid;

	if (ReadRegistryKey32(REGKEY_HKLM, "Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System\\EnableLUA") == 1) {
		uprintf("Note: UAC is active");
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
			uprintf("Could not get current process token: %s", WindowsErrorString());
			goto out;
		}
		if (!GetTokenInformation(token, TokenElevation, &te, sizeof(te), &size)) {
			uprintf("Could not get token information: %s", WindowsErrorString());
			goto out;
		}
		r = (te.TokenIsElevated != 0);
	} else {
		uprintf("Note: UAC is either disabled or not available");
		if (!AllocateAndInitializeSid(&auth, 2, SECURITY_BUILTIN_DOMAIN_RID,
			DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &psid))
			goto out;
		if (!CheckTokenMembership(NULL, psid, &r))
			r = FALSE;
		FreeSid(psid);
	}

out:
	safe_closehandle(token);
	return r;
}

char* GetCurrentMUI(void)
{
	static char mui_str[LOCALE_NAME_MAX_LENGTH];
	wchar_t wmui_str[LOCALE_NAME_MAX_LENGTH];

	if (LCIDToLocaleName(GetUserDefaultUILanguage(), wmui_str, LOCALE_NAME_MAX_LENGTH, 0) > 0) {
		wchar_to_utf8_no_alloc(wmui_str, mui_str, LOCALE_NAME_MAX_LENGTH);
	} else {
		static_strcpy(mui_str, "en-US");
	}
	return mui_str;
}
