/*
 * Rufus: The Reliable USB Formatting Utility
 * Networking functionality (web file download, check for update, etc.)
 * Copyright © 2012-2024 Pete Batard <pete@akeo.ie>
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

/* Memory leaks detection - define _CRTDBG_MAP_ALLOC as preprocessor macro */
#ifdef _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#endif

#include <windows.h>
#include <wininet.h>
#include <netlistmgr.h>
#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>
#include <virtdisk.h>

#include "rufus.h"
#include "missing.h"
#include "resource.h"
#include "msapi_utf8.h"
#include "localization.h"
#include "bled/bled.h"

#include "settings.h"

/* Maximum download chunk size, in bytes */
#define DOWNLOAD_BUFFER_SIZE    (10*KB)
/* Default delay between update checks (1 day) */
#define DEFAULT_UPDATE_INTERVAL (24*3600)

DWORD DownloadStatus;
BYTE* fido_script = NULL;
HANDLE update_check_thread = NULL;

extern loc_cmd* selected_locale;
extern HANDLE dialog_handle;
extern BOOL is_x86_64;
static DWORD error_code, fido_len = 0;
static BOOL force_update_check = FALSE;
static const char* request_headers = "Accept-Encoding: gzip, deflate";

#if defined(__MINGW32__)
#define INetworkListManager_get_IsConnectedToInternet INetworkListManager_IsConnectedToInternet
#endif

static char* GetShortName(const char* url)
{
	static char short_name[128];
	char *p;
	size_t i, len = safe_strlen(url);
	if (len < 5)
		return NULL;

	for (i = len - 2; i > 0; i--) {
		if (url[i] == '/') {
			i++;
			break;
		}
	}
	memset(short_name, 0, sizeof(short_name));
	static_strcpy(short_name, &url[i]);
	// If the URL is followed by a query, remove that part
	// Make sure we detect escaped queries too
	p = strstr(short_name, "%3F");
	if (p != NULL)
		*p = 0;
	p = strstr(short_name, "%3f");
	if (p != NULL)
		*p = 0;
	for (i = 0; i < strlen(short_name); i++) {
		if ((short_name[i] == '?') || (short_name[i] == '#')) {
			short_name[i] = 0;
			break;
		}
	}
	return short_name;
}

static __inline BOOL is_WOW64(void)
{
	BOOL ret = FALSE;
	IsWow64Process(GetCurrentProcess(), &ret);
	return ret;
}

// Open an Internet session
static HINTERNET GetInternetSession(const char* user_agent, BOOL bRetry)
{
	int i;
	char default_agent[64];
	BOOL decodingSupport = TRUE;
	VARIANT_BOOL InternetConnection = VARIANT_FALSE;
	DWORD dwFlags, dwTimeout = NET_SESSION_TIMEOUT, dwProtocolSupport = HTTP_PROTOCOL_FLAG_HTTP2;
	HINTERNET hSession = NULL;
	HRESULT hr = S_FALSE;
	INetworkListManager* pNetworkListManager;

	PF_TYPE_DECL(WINAPI, HINTERNET, InternetOpenA, (LPCSTR, DWORD, LPCSTR, LPCSTR, DWORD));
	PF_TYPE_DECL(WINAPI, BOOL, InternetSetOptionA, (HINTERNET, DWORD, LPVOID, DWORD));
	PF_TYPE_DECL(WINAPI, BOOL, InternetGetConnectedState, (LPDWORD, DWORD));
	PF_INIT_OR_OUT(InternetOpenA, WinInet);
	PF_INIT_OR_OUT(InternetSetOptionA, WinInet);
	PF_INIT(InternetGetConnectedState, WinInet);

	// Create a NetworkListManager Instance to check the network connection
	IGNORE_RETVAL(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));
	hr = CoCreateInstance(&CLSID_NetworkListManager, NULL, CLSCTX_ALL,
		&IID_INetworkListManager, (LPVOID*)&pNetworkListManager);
	if (hr == S_OK) {
		for (i = 0; i <= WRITE_RETRIES; i++) {
			hr = INetworkListManager_get_IsConnectedToInternet(pNetworkListManager, &InternetConnection);
			// INetworkListManager may fail with ERROR_SERVICE_DEPENDENCY_FAIL if the DHCP service
			// is not running, in which case we must fall back to using InternetGetConnectedState().
			// See https://github.com/pbatard/rufus/issues/1801.
			if ((hr == HRESULT_FROM_WIN32(ERROR_SERVICE_DEPENDENCY_FAIL)) && (pfInternetGetConnectedState != NULL)) {
				InternetConnection = pfInternetGetConnectedState(&dwFlags, 0) ? VARIANT_TRUE : VARIANT_FALSE;
				break;
			}
			if (hr == S_OK || !bRetry)
				break;
			Sleep(1000);
		}
	}
	if (InternetConnection == VARIANT_FALSE) {
		SetLastError(ERROR_INTERNET_DISCONNECTED);
		goto out;
	}
	static_sprintf(default_agent, APPLICATION_NAME "/%d.%d.%d (Windows NT %lu.%lu%s)",
		rufus_version[0], rufus_version[1], rufus_version[2],
		WindowsVersion.Major, WindowsVersion.Minor, is_WOW64() ? "; WOW64" : "");
	hSession = pfInternetOpenA((user_agent == NULL) ? default_agent : user_agent,
		INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
	// Set the timeouts
	pfInternetSetOptionA(hSession, INTERNET_OPTION_CONNECT_TIMEOUT, (LPVOID)&dwTimeout, sizeof(dwTimeout));
	pfInternetSetOptionA(hSession, INTERNET_OPTION_SEND_TIMEOUT, (LPVOID)&dwTimeout, sizeof(dwTimeout));
	pfInternetSetOptionA(hSession, INTERNET_OPTION_RECEIVE_TIMEOUT, (LPVOID)&dwTimeout, sizeof(dwTimeout));
	// Enable gzip and deflate decoding schemes
	pfInternetSetOptionA(hSession, INTERNET_OPTION_HTTP_DECODING, (LPVOID)&decodingSupport, sizeof(decodingSupport));
	// Enable HTTP/2 protocol support
	pfInternetSetOptionA(hSession, INTERNET_OPTION_ENABLE_HTTP_PROTOCOL, (LPVOID)&dwProtocolSupport, sizeof(dwProtocolSupport));

out:
	return hSession;
}

/*
 * Download a file or fill a buffer from an URL
 * Mostly taken from http://support.microsoft.com/kb/234913
 * If file is NULL, a buffer is allocated for the download (that needs to be freed by the caller)
 * If hProgressDialog is not NULL, this function will send INIT and EXIT messages
 * to the dialog in question, with WPARAM being set to nonzero for EXIT on success
 * and also attempt to indicate progress using an IDC_PROGRESS control
 * Note that when a buffer is used, the actual size of the buffer is one more than its reported
 * size (with the extra byte set to 0) to accommodate for calls that need a NUL-terminated buffer.
 */
uint64_t DownloadToFileOrBufferEx(const char* url, const char* file, const char* user_agent,
	BYTE** buffer, HWND hProgressDialog, BOOL bTaskBarProgress)
{
	const char* accept_types[] = {"*/*\0", NULL};
	const char* short_name;
	unsigned char buf[DOWNLOAD_BUFFER_SIZE];
	char hostname[64], urlpath[128], strsize[32];
	BOOL r = FALSE;
	DWORD dwSize, dwWritten, dwDownloaded;
	HANDLE hFile = INVALID_HANDLE_VALUE;
	HINTERNET hSession = NULL, hConnection = NULL, hRequest = NULL;
	URL_COMPONENTSA UrlParts = {sizeof(URL_COMPONENTSA), NULL, 1, (INTERNET_SCHEME)0,
		hostname, sizeof(hostname), 0, NULL, 1, urlpath, sizeof(urlpath), NULL, 1};
	uint64_t size = 0, total_size = 0;

	// Can't link with wininet.lib because of sideloading issues
	// And we can't delay-load wininet.dll with MinGW either because the application simply exits on startup...
	PF_TYPE_DECL(WINAPI, BOOL, InternetCrackUrlA, (LPCSTR, DWORD, DWORD, LPURL_COMPONENTSA));
	PF_TYPE_DECL(WINAPI, HINTERNET, InternetConnectA, (HINTERNET, LPCSTR, INTERNET_PORT, LPCSTR, LPCSTR, DWORD, DWORD, DWORD_PTR));
	PF_TYPE_DECL(WINAPI, BOOL, InternetReadFile, (HINTERNET, LPVOID, DWORD, LPDWORD));
	PF_TYPE_DECL(WINAPI, BOOL, InternetCloseHandle, (HINTERNET));
	PF_TYPE_DECL(WINAPI, HINTERNET, HttpOpenRequestA, (HINTERNET, LPCSTR, LPCSTR, LPCSTR, LPCSTR, LPCSTR*, DWORD, DWORD_PTR));
	PF_TYPE_DECL(WINAPI, BOOL, HttpSendRequestA, (HINTERNET, LPCSTR, DWORD, LPVOID, DWORD));
	PF_TYPE_DECL(WINAPI, BOOL, HttpQueryInfoA, (HINTERNET, DWORD, LPVOID, LPDWORD, LPDWORD));
	PF_INIT_OR_OUT(InternetCrackUrlA, WinInet);
	PF_INIT_OR_OUT(InternetConnectA, WinInet);
	PF_INIT_OR_OUT(InternetReadFile, WinInet);
	PF_INIT_OR_OUT(InternetCloseHandle, WinInet);
	PF_INIT_OR_OUT(HttpOpenRequestA, WinInet);
	PF_INIT_OR_OUT(HttpSendRequestA, WinInet);
	PF_INIT_OR_OUT(HttpQueryInfoA, WinInet);

	ErrorStatus = 0;
	DownloadStatus = 404;
	if (hProgressDialog != NULL)
		UpdateProgressWithInfoInit(hProgressDialog, FALSE);

	assert(url != NULL);
	if (buffer != NULL)
		*buffer = NULL;

	short_name = (file != NULL) ? PathFindFileNameU(file) : PathFindFileNameU(url);

	if (hProgressDialog != NULL) {
		PrintInfo(5000, MSG_085, short_name);
		uprintf("Downloading %s", url);
	}

	if ( (!pfInternetCrackUrlA(url, (DWORD)safe_strlen(url), 0, &UrlParts))
	  || (UrlParts.lpszHostName == NULL) || (UrlParts.lpszUrlPath == NULL)) {
		uprintf("Unable to decode URL: %s", WindowsErrorString());
		goto out;
	}
	hostname[sizeof(hostname)-1] = 0;

	hSession = GetInternetSession(user_agent, TRUE);
	if (hSession == NULL) {
		uprintf("Could not open Internet session: %s", WindowsErrorString());
		goto out;
	}

	hConnection = pfInternetConnectA(hSession, UrlParts.lpszHostName, UrlParts.nPort, NULL, NULL, INTERNET_SERVICE_HTTP, 0, (DWORD_PTR)NULL);
	if (hConnection == NULL) {
		uprintf("Could not connect to server %s:%d: %s", UrlParts.lpszHostName, UrlParts.nPort, WindowsErrorString());
		goto out;
	}

	hRequest = pfHttpOpenRequestA(hConnection, "GET", UrlParts.lpszUrlPath, NULL, NULL, accept_types,
		INTERNET_FLAG_IGNORE_REDIRECT_TO_HTTP | INTERNET_FLAG_IGNORE_REDIRECT_TO_HTTPS |
		INTERNET_FLAG_NO_COOKIES | INTERNET_FLAG_NO_UI | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_HYPERLINK |
		((UrlParts.nScheme==INTERNET_SCHEME_HTTPS)?INTERNET_FLAG_SECURE:0), (DWORD_PTR)NULL);
	if (hRequest == NULL) {
		uprintf("Could not open URL %s: %s", url, WindowsErrorString());
		goto out;
	}

	if (!pfHttpSendRequestA(hRequest, request_headers, -1L, NULL, 0)) {
		uprintf("Unable to send request: %s", WindowsErrorString());
		goto out;
	}

	// Get the file size
	dwSize = sizeof(DownloadStatus);
	pfHttpQueryInfoA(hRequest, HTTP_QUERY_STATUS_CODE|HTTP_QUERY_FLAG_NUMBER, (LPVOID)&DownloadStatus, &dwSize, NULL);
	if (DownloadStatus != 200) {
		error_code = ERROR_INTERNET_ITEM_NOT_FOUND;
		SetLastError(RUFUS_ERROR(error_code));
		uprintf("%s: %d", (DownloadStatus == 404) ? "File not found" : "Unable to access file", DownloadStatus);
		goto out;
	}
	dwSize = sizeof(strsize);
	if (!pfHttpQueryInfoA(hRequest, HTTP_QUERY_CONTENT_LENGTH, (LPVOID)strsize, &dwSize, NULL)) {
		uprintf("Unable to retrieve file length: %s", WindowsErrorString());
		goto out;
	}
	total_size = strtoull(strsize, NULL, 10);
	if (hProgressDialog != NULL) {
		char msg[128];
		uprintf("File length: %s", SizeToHumanReadable(total_size, FALSE, FALSE));
		if (right_to_left_mode)
			static_sprintf(msg, "(%s) %s", SizeToHumanReadable(total_size, FALSE, FALSE), GetShortName(url));
		else
			static_sprintf(msg, "%s (%s)", GetShortName(url), SizeToHumanReadable(total_size, FALSE, FALSE));
		PrintStatus(5000, MSG_085, msg);
	}

	if (file != NULL) {
		hFile = CreateFileU(file, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (hFile == INVALID_HANDLE_VALUE) {
			uprintf("Unable to create file '%s': %s", short_name, WindowsErrorString());
			goto out;
		}
	} else {
		if (buffer == NULL) {
			uprintf("No buffer pointer provided for download");
			goto out;
		}
		// Allocate one extra byte, so that caller can rely on NUL-terminated text if needed
		*buffer = calloc((size_t)total_size + 1, 1);
		if (*buffer == NULL) {
			uprintf("Could not allocate buffer for download");
			goto out;
		}
	}

	// Keep checking for data until there is nothing left.
	while (1) {
		// User may have cancelled the download
		if (IS_ERROR(ErrorStatus))
			goto out;
		if (!pfInternetReadFile(hRequest, buf, sizeof(buf), &dwDownloaded) || (dwDownloaded == 0))
			break;
		if (hProgressDialog != NULL)
			UpdateProgressWithInfo(OP_NOOP, MSG_241, size, total_size);
		if (file != NULL) {
			if (!WriteFile(hFile, buf, dwDownloaded, &dwWritten, NULL)) {
				uprintf("Error writing file '%s': %s", short_name, WindowsErrorString());
				goto out;
			} else if (dwDownloaded != dwWritten) {
				uprintf("Error writing file '%s': Only %d/%d bytes written", short_name, dwWritten, dwDownloaded);
				goto out;
			}
		} else {
			memcpy(&(*buffer)[size], buf, dwDownloaded);
		}
		size += dwDownloaded;
	}

	if (size != total_size) {
		uprintf("Could not download complete file - read: %lld bytes, expected: %lld bytes", size, total_size);
		ErrorStatus = RUFUS_ERROR(ERROR_WRITE_FAULT);
		goto out;
	} else {
		DownloadStatus = 200;
		r = TRUE;
		if (hProgressDialog != NULL) {
			UpdateProgressWithInfo(OP_NOOP, MSG_241, total_size, total_size);
			uprintf("Successfully downloaded '%s'", short_name);
		}
	}

out:
	error_code = GetLastError();
	if (hFile != INVALID_HANDLE_VALUE) {
		// Force a flush - May help with the PKI API trying to process downloaded updates too early...
		FlushFileBuffers(hFile);
		CloseHandle(hFile);
	}
	if (!r) {
		if (file != NULL)
			DeleteFileU(file);
		if (buffer != NULL)
			safe_free(*buffer);
	}
	if (hRequest)
		pfInternetCloseHandle(hRequest);
	if (hConnection)
		pfInternetCloseHandle(hConnection);
	if (hSession)
		pfInternetCloseHandle(hSession);

	SetLastError(error_code);
	return r ? size : 0;
}

// Download and validate a signed file. The file must have a corresponding '.sig' on the server.
DWORD DownloadSignedFile(const char* url, const char* file, HWND hProgressDialog, BOOL bPromptOnError)
{
	char* url_sig = NULL;
	BYTE *buf = NULL, *sig = NULL;
	DWORD buf_len = 0, sig_len = 0;
	DWORD ret = 0;
	HANDLE hFile = INVALID_HANDLE_VALUE;

	assert(url != NULL);

	url_sig = malloc(strlen(url) + 5);
	if (url_sig == NULL) {
		uprintf("Could not allocate signature URL");
		goto out;
	}
	strcpy(url_sig, url);
	strcat(url_sig, ".sig");

	buf_len = (DWORD)DownloadToFileOrBuffer(url, NULL, &buf, hProgressDialog, FALSE);
	if (buf_len == 0)
		goto out;
	sig_len = (DWORD)DownloadToFileOrBuffer(url_sig, NULL, &sig, NULL, FALSE);
	if ((sig_len != RSA_SIGNATURE_SIZE) || (!ValidateOpensslSignature(buf, buf_len, sig, sig_len))) {
		uprintf("FATAL: Download signature is invalid ✗");
		DownloadStatus = 403;	// Forbidden
		ErrorStatus = RUFUS_ERROR(APPERR(ERROR_BAD_SIGNATURE));
		SendMessage(GetDlgItem(hProgressDialog, IDC_PROGRESS), PBM_SETSTATE, (WPARAM)PBST_ERROR, 0);
		SetTaskbarProgressState(TASKBAR_ERROR);
		goto out;
	}

	uprintf("Download signature is valid ✓");
	DownloadStatus = 206;	// Partial content
	hFile = CreateFileU(file, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE) {
		uprintf("Unable to create file '%s': %s", PathFindFileNameU(file), WindowsErrorString());
		goto out;
	}
	if (!WriteFile(hFile, buf, buf_len, &ret, NULL)) {
		uprintf("Error writing file '%s': %s", PathFindFileNameU(file), WindowsErrorString());
		ret = 0;
		goto out;
	} else if (ret != buf_len) {
		uprintf("Error writing file '%s': Only %d/%d bytes written", PathFindFileNameU(file), ret, buf_len);
		ret = 0;
		goto out;
	}
	DownloadStatus = 200;	// Full content

out:
	if (hProgressDialog != NULL)
		SendMessage(hProgressDialog, UM_PROGRESS_EXIT, (WPARAM)ret, 0);
	if ((bPromptOnError) && (DownloadStatus != 200)) {
		PrintInfo(0, MSG_242);
		SetLastError(error_code);
		MessageBoxExU(hMainDialog, IS_ERROR(ErrorStatus) ? StrError(ErrorStatus, FALSE) : WindowsErrorString(),
			lmprintf(MSG_044), MB_OK | MB_ICONERROR | MB_IS_RTL, selected_langid);
	}
	safe_closehandle(hFile);
	free(url_sig);
	free(buf);
	free(sig);
	return ret;
}

/* Threaded download */
typedef struct {
	const char* url;
	const char* file;
	HWND hProgressDialog;
	BOOL bPromptOnError;
} DownloadSignedFileThreadArgs;

static DWORD WINAPI DownloadSignedFileThread(LPVOID param)
{
	DownloadSignedFileThreadArgs* args = (DownloadSignedFileThreadArgs*)param;
	ExitThread(DownloadSignedFile(args->url, args->file, args->hProgressDialog, args->bPromptOnError));
}

HANDLE DownloadSignedFileThreaded(const char* url, const char* file, HWND hProgressDialog, BOOL bPromptOnError)
{
	static DownloadSignedFileThreadArgs args;
	args.url = url;
	args.file = file;
	args.hProgressDialog = hProgressDialog;
	args.bPromptOnError = bPromptOnError;
	return CreateThread(NULL, 0, DownloadSignedFileThread, &args, 0, NULL);
}

static __inline uint64_t to_uint64_t(uint16_t x[3]) {
	int i;
	uint64_t ret = 0;
	for (i = 0; i < 3; i++)
		ret = (ret << 16) + x[i];
	return ret;
}

/*
 * Background thread to check for updates
 */
static DWORD WINAPI CheckForUpdatesThread(LPVOID param)
{
	BOOL releases_only = TRUE, found_new_version = FALSE;
	int status = 0;
	const char* server_url = RUFUS_URL "/";
	int i, j, k, max_channel, verbose = 0, verpos[4];
	static const char* channel[] = { "release", "beta", "test" };		// release channel
	const char* accept_types[] = { "*/*\0", NULL };
	char* buf = NULL;
	char agent[64], hostname[64], urlpath[128], sigpath[256];
	DWORD dwSize, dwDownloaded, dwTotalSize, dwStatus;
	BYTE *sig = NULL;
	HINTERNET hSession = NULL, hConnection = NULL, hRequest = NULL;
	URL_COMPONENTSA UrlParts = { sizeof(URL_COMPONENTSA), NULL, 1, (INTERNET_SCHEME)0,
		hostname, sizeof(hostname), 0, NULL, 1, urlpath, sizeof(urlpath), NULL, 1 };
	SYSTEMTIME ServerTime, LocalTime;
	FILETIME FileTime;
	int64_t local_time = 0, reg_time, server_time, update_interval;

	// Can't link with wininet.lib because of sideloading issues
	PF_TYPE_DECL(WINAPI, BOOL, InternetCrackUrlA, (LPCSTR, DWORD, DWORD, LPURL_COMPONENTSA));
	PF_TYPE_DECL(WINAPI, HINTERNET, InternetConnectA, (HINTERNET, LPCSTR, INTERNET_PORT, LPCSTR, LPCSTR, DWORD, DWORD, DWORD_PTR));
	PF_TYPE_DECL(WINAPI, BOOL, InternetReadFile, (HINTERNET, LPVOID, DWORD, LPDWORD));
	PF_TYPE_DECL(WINAPI, BOOL, InternetCloseHandle, (HINTERNET));
	PF_TYPE_DECL(WINAPI, HINTERNET, HttpOpenRequestA, (HINTERNET, LPCSTR, LPCSTR, LPCSTR, LPCSTR, LPCSTR*, DWORD, DWORD_PTR));
	PF_TYPE_DECL(WINAPI, BOOL, HttpSendRequestA, (HINTERNET, LPCSTR, DWORD, LPVOID, DWORD));
	PF_TYPE_DECL(WINAPI, BOOL, HttpQueryInfoA, (HINTERNET, DWORD, LPVOID, LPDWORD, LPDWORD));
	PF_INIT_OR_OUT(InternetCrackUrlA, WinInet);
	PF_INIT_OR_OUT(InternetConnectA, WinInet);
	PF_INIT_OR_OUT(InternetReadFile, WinInet);
	PF_INIT_OR_OUT(InternetCloseHandle, WinInet);
	PF_INIT_OR_OUT(HttpOpenRequestA, WinInet);
	PF_INIT_OR_OUT(HttpSendRequestA, WinInet);
	PF_INIT_OR_OUT(HttpQueryInfoA, WinInet);

	verbose = ReadSetting32(SETTING_VERBOSE_UPDATES);
	// Without this the FileDialog will produce error 0x8001010E when compiled for Vista or later
	IGNORE_RETVAL(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));
	// Unless the update was forced, wait a while before performing the update check
	if (!force_update_check) {
		// It would of course be a lot nicer to use a timer and wake the thread, but my
		// development time is limited and this is FASTER to implement.
		do {
			for (i = 0; ( i < 30) && (!force_update_check); i++)
				Sleep(500);
		} while ((!force_update_check) && ((op_in_progress || (dialog_showing > 0))));
		if (!force_update_check) {
			if ((ReadSetting32(SETTING_UPDATE_INTERVAL) == -1)) {
				vuprintf("Check for updates disabled, as per settings.");
				goto out;
			}
			reg_time = ReadSetting64(SETTING_LAST_UPDATE);
			update_interval = (int64_t)ReadSetting32(SETTING_UPDATE_INTERVAL);
			if (update_interval == 0) {
				WriteSetting32(SETTING_UPDATE_INTERVAL, DEFAULT_UPDATE_INTERVAL);
				update_interval = DEFAULT_UPDATE_INTERVAL;
			}
			GetSystemTime(&LocalTime);
			if (!SystemTimeToFileTime(&LocalTime, &FileTime))
				goto out;
			local_time = ((((int64_t)FileTime.dwHighDateTime) << 32) + FileTime.dwLowDateTime) / 10000000;
			vvuprintf("Local time: %" PRId64, local_time);
			if (local_time < reg_time + update_interval) {
				vuprintf("Next update check in %" PRId64 " seconds.", reg_time + update_interval - local_time);
				goto out;
			}
		}
	}

	PrintInfoDebug(3000, MSG_243);
	status++;	// 1

	if (!pfInternetCrackUrlA(server_url, (DWORD)safe_strlen(server_url), 0, &UrlParts))
		goto out;
	hostname[sizeof(hostname)-1] = 0;

	static_sprintf(agent, APPLICATION_NAME "/%d.%d.%d (Windows NT %lu.%lu%s)",
		rufus_version[0], rufus_version[1], rufus_version[2],
		WindowsVersion.Major, WindowsVersion.Minor, is_WOW64() ? "; WOW64" : "");
	hSession = GetInternetSession(NULL, FALSE);
	if (hSession == NULL)
		goto out;
	hConnection = pfInternetConnectA(hSession, UrlParts.lpszHostName, UrlParts.nPort,
		NULL, NULL, INTERNET_SERVICE_HTTP, 0, (DWORD_PTR)NULL);
	if (hConnection == NULL)
		goto out;

	status++;	// 2
	// BETAs are only made available when the application arch is x86_64
	if (is_x86_64)
		releases_only = !ReadSettingBool(SETTING_INCLUDE_BETAS);

	// Test releases get their own distribution channel (and also force beta checks)
#if defined(TEST)
	max_channel = (int)ARRAYSIZE(channel);
#else
	max_channel = releases_only ? 1 : (int)ARRAYSIZE(channel) - 1;
#endif
	vuprintf("Using %s for the update check", RUFUS_URL);
	for (k = 0; (k < max_channel) && (!found_new_version); k++) {
		// Free any previous buffers we might have used
		safe_free(buf);
		safe_free(sig);
		uprintf("Checking %s channel...", channel[k]);
		// At this stage we can query the server for various update version files.
		// We first try to lookup for "<appname>_<os_arch>_<os_version_major>_<os_version_minor>.ver"
		// and then remove each of the <os_> components until we find our match. For instance, we may first
		// look for rufus_win_x64_6.2.ver (Win8 x64) but only get a match for rufus_win_x64_6.ver (Vista x64 or later)
		// This allows sunsetting OS versions (eg XP) or providing different downloads for different archs/groups.
		// Note that for BETAs, we only catter for x64 regardless of the OS arch.
		static_sprintf(urlpath, "%s%s%s_win_%s_%lu.%lu.ver", APPLICATION_NAME, (k == 0) ? "": "_",
			(k == 0) ? "" : channel[k], GetArchName(WindowsVersion.Arch), WindowsVersion.Major, WindowsVersion.Minor);
		vuprintf("Base update check: %s", urlpath);
		for (i = 0, j = (int)safe_strlen(urlpath) - 5; (j > 0) && (i < ARRAYSIZE(verpos)); j--) {
			if ((urlpath[j] == '.') || (urlpath[j] == '_')) {
				verpos[i++] = j;
			}
		}
		assert(i == ARRAYSIZE(verpos));

		UrlParts.lpszUrlPath = urlpath;
		UrlParts.dwUrlPathLength = sizeof(urlpath);
		for (i=0; i<ARRAYSIZE(verpos); i++) {
			vvuprintf("Trying %s", UrlParts.lpszUrlPath);
			hRequest = pfHttpOpenRequestA(hConnection, "GET", UrlParts.lpszUrlPath, NULL, NULL, accept_types,
				INTERNET_FLAG_IGNORE_REDIRECT_TO_HTTP|INTERNET_FLAG_IGNORE_REDIRECT_TO_HTTPS|
				INTERNET_FLAG_NO_COOKIES|INTERNET_FLAG_NO_UI|INTERNET_FLAG_NO_CACHE_WRITE|INTERNET_FLAG_HYPERLINK|
				((UrlParts.nScheme == INTERNET_SCHEME_HTTPS)?INTERNET_FLAG_SECURE:0), (DWORD_PTR)NULL);
			if ((hRequest == NULL) || (!pfHttpSendRequestA(hRequest, request_headers, -1L, NULL, 0))) {
				uprintf("Unable to send request: %s", WindowsErrorString());
				goto out;
			}

			// Ensure that we get a text file
			dwSize = sizeof(dwStatus);
			dwStatus = 404;
			pfHttpQueryInfoA(hRequest, HTTP_QUERY_STATUS_CODE|HTTP_QUERY_FLAG_NUMBER, (LPVOID)&dwStatus, &dwSize, NULL);
			if (dwStatus == 200)
				break;
			pfInternetCloseHandle(hRequest);
			hRequest = NULL;
			safe_strcpy(&urlpath[verpos[i]], 5, ".ver");
		}
		if (dwStatus != 200) {
			vuprintf("Could not find a %s version file on server %s", channel[k], server_url);
			if ((releases_only) || (k + 1 >= ARRAYSIZE(channel)))
				goto out;
			continue;
		}
		vuprintf("Found match for %s on server %s", urlpath, server_url);

		// We also get a date from the web server, which we'll use to avoid out of sync check,
		// in case some set their clock way into the future and back.
		// On the other hand, if local clock is set way back in the past, we will never check.
		dwSize = sizeof(ServerTime);
		// If we can't get a date we can trust, don't bother...
		if ( (!pfHttpQueryInfoA(hRequest, HTTP_QUERY_DATE|HTTP_QUERY_FLAG_SYSTEMTIME, (LPVOID)&ServerTime, &dwSize, NULL))
			|| (!SystemTimeToFileTime(&ServerTime, &FileTime)) )
			goto out;
		server_time = ((((int64_t)FileTime.dwHighDateTime) << 32) + FileTime.dwLowDateTime) / 10000000;
		vvuprintf("Server time: %" PRId64, server_time);
		// Always store the server response time - the only clock we trust!
		WriteSetting64(SETTING_LAST_UPDATE, server_time);
		// Might as well let the user know
		if (!force_update_check) {
			if ((local_time > server_time + 600) || (local_time < server_time - 600)) {
				uprintf("IMPORTANT: Your local clock is more than 10 minutes in the %s. Unless you fix this, "
					APPLICATION_NAME " may not be able to check for updates...",
					(local_time > server_time + 600)?"future":"past");
			}
		}

		dwSize = sizeof(dwTotalSize);
		if (!pfHttpQueryInfoA(hRequest, HTTP_QUERY_CONTENT_LENGTH|HTTP_QUERY_FLAG_NUMBER, (LPVOID)&dwTotalSize, &dwSize, NULL))
			goto out;

		// Make sure the file is NUL terminated
		buf = (char*)calloc(dwTotalSize + 1, 1);
		if (buf == NULL)
			goto out;
		// This is a version file - we should be able to gulp it down in one go
		if (!pfInternetReadFile(hRequest, buf, dwTotalSize, &dwDownloaded) || (dwDownloaded != dwTotalSize))
			goto out;
		vuprintf("Successfully downloaded version file (%d bytes)", dwTotalSize);

		// Now download the signature file
		static_sprintf(sigpath, "%s/%s.sig", server_url, urlpath);
		dwDownloaded = (DWORD)DownloadToFileOrBuffer(sigpath, NULL, &sig, NULL, FALSE);
		if ((dwDownloaded != RSA_SIGNATURE_SIZE) || (!ValidateOpensslSignature(buf, dwTotalSize, sig, dwDownloaded))) {
			uprintf("FATAL: Version signature is invalid ✗");
			goto out;
		}
		vuprintf("Version signature is valid ✓");

		status++;
		parse_update(buf, dwTotalSize + 1);

		vuprintf("UPDATE DATA:");
		vuprintf("  version: %d.%d.%d (%s)", update.version[0], update.version[1], update.version[2], channel[k]);
		vuprintf("  platform_min: %d.%d", update.platform_min[0], update.platform_min[1]);
		vuprintf("  url: %s", update.download_url);

		found_new_version = ((to_uint64_t(update.version) > to_uint64_t(rufus_version)) || (force_update))
			&& ((WindowsVersion.Major > update.platform_min[0])
				|| ((WindowsVersion.Major == update.platform_min[0]) && (WindowsVersion.Minor >= update.platform_min[1])));
		uprintf("N%sew %s version found%c", found_new_version ? "" : "o n", channel[k], found_new_version ? '!' : '.');
	}

out:
	safe_free(buf);
	safe_free(sig);
	if (hRequest)
		pfInternetCloseHandle(hRequest);
	if (hConnection)
		pfInternetCloseHandle(hConnection);
	if (hSession)
		pfInternetCloseHandle(hSession);
	switch (status) {
	case 1:
		PrintInfoDebug(3000, MSG_244);
		break;
	case 2:
		PrintInfoDebug(3000, MSG_245);
		break;
	case 3:
	case 4:
		PrintInfo(3000, found_new_version ? MSG_246 : MSG_247);
	default:
		break;
	}
	// Start the new download after cleanup
	if (found_new_version) {
		// User may have started an operation while we were checking
		while ((!force_update_check) && (op_in_progress || (dialog_showing > 0))) {
			Sleep(15000);
		}
		DownloadNewVersion();
	} else if (force_update_check) {
		PostMessage(hMainDialog, UM_NO_UPDATE, 0, 0);
	}
	force_update_check = FALSE;
	update_check_thread = NULL;
	CoUninitialize();
	ExitThread(0);
}

/*
 * Initiate a check for updates. If force is true, ignore the wait period
 */
BOOL CheckForUpdates(BOOL force)
{
	force_update_check = force;
	if (update_check_thread != NULL)
		return FALSE;

	update_check_thread = CreateThread(NULL, 0, CheckForUpdatesThread, NULL, 0, NULL);
	if (update_check_thread == NULL) {
		uprintf("Unable to start update check thread");
		return FALSE;
	}
	return TRUE;
}

/*
 * Download an ISO through Fido
 */
static DWORD WINAPI DownloadISOThread(LPVOID param)
{
	char locale_str[1024], cmdline[sizeof(locale_str) + 512], pipe[MAX_GUID_STRING_LENGTH + 16] = "\\\\.\\pipe\\";
	char powershell_path[MAX_PATH], icon_path[MAX_PATH] = { 0 }, script_path[MAX_PATH] = { 0 };
	char *url = NULL, sig_url[128];
	uint64_t uncompressed_size;
	int64_t size = -1;
	BYTE *compressed = NULL, *sig = NULL;
	HANDLE hFile, hPipe;
	DWORD dwExitCode = 99, dwCompressedSize, dwSize, dwAvail, dwPipeSize = 4096;
	GUID guid;

	dialog_showing++;
	IGNORE_RETVAL(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));

	// Use a GUID as random unique string, else ill-intentioned security "researchers"
	// may either spam our pipe or replace our script to fool antivirus solutions into
	// thinking that Rufus is doing something malicious...
	IGNORE_RETVAL(CoCreateGuid(&guid));
	// coverity[fixed_size_dest]
	strcpy(&pipe[9], GuidToString(&guid, TRUE));
	static_sprintf(icon_path, "%s%s.ico", temp_dir, APPLICATION_NAME);
	ExtractAppIcon(icon_path, TRUE);

//#define FORCE_URL "https://github.com/pbatard/rufus/raw/master/res/loc/test/windows_to_go.iso"
//#define FORCE_URL "https://cdimage.debian.org/debian-cd/current/amd64/iso-cd/debian-9.8.0-amd64-netinst.iso"
#if !defined(FORCE_URL)
#if defined(RUFUS_TEST)
	IGNORE_RETVAL(hFile);
	IGNORE_RETVAL(sig_url);
	IGNORE_RETVAL(dwCompressedSize);
	IGNORE_RETVAL(uncompressed_size);
	// In test mode, just use our local script
	static_strcpy(script_path, "D:\\Projects\\Fido\\Fido.ps1");
#else
	// If we don't have the script, download it
	if (fido_script == NULL) {
		dwCompressedSize = (DWORD)DownloadToFileOrBuffer(fido_url, NULL, &compressed, hMainDialog, FALSE);
		if (dwCompressedSize == 0)
			goto out;
		static_sprintf(sig_url, "%s.sig", fido_url);
		dwSize = (DWORD)DownloadToFileOrBuffer(sig_url, NULL, &sig, NULL, FALSE);
		if ((dwSize != RSA_SIGNATURE_SIZE) || (!ValidateOpensslSignature(compressed, dwCompressedSize, sig, dwSize))) {
			uprintf("FATAL: Download signature is invalid ✗");
			ErrorStatus = RUFUS_ERROR(APPERR(ERROR_BAD_SIGNATURE));
			SendMessage(hProgress, PBM_SETSTATE, (WPARAM)PBST_ERROR, 0);
			SetTaskbarProgressState(TASKBAR_ERROR);
			safe_free(compressed);
			free(sig);
			goto out;
		}
		free(sig);
		uprintf("Download signature is valid ✓");
		uncompressed_size = *((uint64_t*)&compressed[5]);
		if ((uncompressed_size < 1 * MB) && (bled_init(0, uprintf, NULL, NULL, NULL, NULL, &ErrorStatus) >= 0)) {
			fido_script = malloc((size_t)uncompressed_size);
			size = bled_uncompress_from_buffer_to_buffer(compressed, dwCompressedSize, fido_script, (size_t)uncompressed_size, BLED_COMPRESSION_LZMA);
			bled_exit();
		}
		safe_free(compressed);
		if (size != uncompressed_size) {
			uprintf("FATAL: Could not uncompressed download script");
			safe_free(fido_script);
			ErrorStatus = RUFUS_ERROR(ERROR_INVALID_DATA);
			SendMessage(hProgress, PBM_SETSTATE, (WPARAM)PBST_ERROR, 0);
			SetTaskbarProgressState(TASKBAR_ERROR);
			goto out;
		}
		fido_len = (DWORD)size;
		SendMessage(hProgress, PBM_SETSTATE, (WPARAM)PBST_NORMAL, 0);
		SetTaskbarProgressState(TASKBAR_NORMAL);
		SetTaskbarProgressValue(0, MAX_PROGRESS);
		SendMessage(hProgress, PBM_SETPOS, 0, 0);
	}
	PrintInfo(0, MSG_148);

	assert((fido_script != NULL) && (fido_len != 0));

	static_sprintf(script_path, "%s%s.ps1", temp_dir, GuidToString(&guid, TRUE));
	hFile = CreateFileU(script_path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_READONLY, NULL);
	if (hFile == INVALID_HANDLE_VALUE) {
		uprintf("Unable to create download script '%s': %s", script_path, WindowsErrorString());
		goto out;
	}
	if ((!WriteFile(hFile, fido_script, fido_len, &dwSize, NULL)) || (dwSize != fido_len)) {
		uprintf("Unable to write download script '%s': %s", script_path, WindowsErrorString());
		goto out;
	}
	// Why oh why does PowerShell refuse to open read-only files that haven't been closed?
	// Because of this limitation, we can't use LockFileEx() on the file we create...
	safe_closehandle(hFile);
#endif
	static_sprintf(powershell_path, "%s\\WindowsPowerShell\\v1.0\\powershell.exe", system_dir);
	static_sprintf(locale_str, "%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s",
		selected_locale->txt[0], lmprintf(MSG_135), lmprintf(MSG_136), lmprintf(MSG_137),
		lmprintf(MSG_138), lmprintf(MSG_139), lmprintf(MSG_040), lmprintf(MSG_140), lmprintf(MSG_141),
		lmprintf(MSG_006), lmprintf(MSG_007), lmprintf(MSG_042), lmprintf(MSG_142), lmprintf(MSG_143),
		lmprintf(MSG_144), lmprintf(MSG_145), lmprintf(MSG_146), lmprintf(MSG_199));

	hPipe = CreateNamedPipeA(pipe, PIPE_ACCESS_INBOUND,
		PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, PIPE_UNLIMITED_INSTANCES,
		dwPipeSize, dwPipeSize, 0, NULL);
	if (hPipe == INVALID_HANDLE_VALUE) {
		uprintf("Could not create pipe '%s': %s", pipe, WindowsErrorString());
		goto out;
	}

	static_sprintf(cmdline, "\"%s\" -NonInteractive -Sta -NoProfile –ExecutionPolicy Bypass "
		"-File \"%s\" -PipeName %s -LocData \"%s\" -Icon \"%s\" -AppTitle \"%s\"",
		powershell_path, script_path, &pipe[9], locale_str, icon_path, lmprintf(MSG_149));

#ifndef RUFUS_TEST
	// For extra security, even after we validated that the LZMA download is properly
	// signed, we also validate the Authenticode signature of the local script.
	if (ValidateSignature(INVALID_HANDLE_VALUE, script_path) != NO_ERROR) {
		uprintf("FATAL: Script signature is invalid ✗");
		ErrorStatus = RUFUS_ERROR(APPERR(ERROR_BAD_SIGNATURE));
		SendMessage(hProgress, PBM_SETSTATE, (WPARAM)PBST_ERROR, 0);
		SetTaskbarProgressState(TASKBAR_ERROR);
		goto out;
	}
	uprintf("Script signature is valid ✓");
#endif

	ErrorStatus = 0;
	dwExitCode = RunCommand(cmdline, app_data_dir, TRUE);
	uprintf("Exited download script with code: %d", dwExitCode);
	if ((dwExitCode == 0) && PeekNamedPipe(hPipe, NULL, dwPipeSize, NULL, &dwAvail, NULL) && (dwAvail != 0)) {
		url = malloc(dwAvail + 1);
		dwSize = 0;
		if ((url != NULL) && ReadFile(hPipe, url, dwAvail, &dwSize, NULL) && (dwSize > 4)) {
#else
	{	{	url = strdup(FORCE_URL);
			dwSize = (DWORD)strlen(FORCE_URL);
#endif
			IMG_SAVE img_save = { 0 };
// WTF is wrong with Microsoft's static analyzer reporting a potential buffer overflow here?!?
#if defined(_MSC_VER)
#pragma warning(disable: 6386)
#endif
			url[min(dwSize, dwAvail)] = 0;
#if defined(_MSC_VER)
#pragma warning(default: 6386)
#endif
			EXT_DECL(img_ext, GetShortName(url), __VA_GROUP__("*.iso"), __VA_GROUP__(lmprintf(MSG_036)));
			img_save.Type = VIRTUAL_STORAGE_TYPE_DEVICE_ISO;
			img_save.ImagePath = FileDialog(TRUE, NULL, &img_ext, NULL);
			if (img_save.ImagePath == NULL) {
				goto out;
			}
			// Download the ISO and report errors if any
			SendMessage(hMainDialog, UM_PROGRESS_INIT, 0, 0);
			ErrorStatus = 0;
			SendMessage(hMainDialog, UM_TIMER_START, 0, 0);
			if (DownloadToFileOrBuffer(url, img_save.ImagePath, NULL, hMainDialog, TRUE) == 0) {
				SendMessage(hMainDialog, UM_PROGRESS_EXIT, 0, 0);
				if (SCODE_CODE(ErrorStatus) == ERROR_CANCELLED) {
					uprintf("Download cancelled by user");
					Notification(MSG_INFO, NULL, NULL, lmprintf(MSG_211), lmprintf(MSG_041));
					PrintInfo(0, MSG_211);
				} else {
					Notification(MSG_ERROR, NULL, NULL, lmprintf(MSG_194, GetShortName(url)), lmprintf(MSG_043, WindowsErrorString()));
					PrintInfo(0, MSG_212);
				}
			} else {
				// Download was successful => Select and scan the ISO
				image_path = safe_strdup(img_save.ImagePath);
				PostMessage(hMainDialog, UM_SELECT_ISO, 0, 0);
			}
			safe_free(img_save.ImagePath);
		}
	}

out:
	if (icon_path[0] != 0)
		DeleteFileU(icon_path);
#if !defined(RUFUS_TEST)
	if (script_path[0] != 0) {
		SetFileAttributesU(script_path, FILE_ATTRIBUTE_NORMAL);
		DeleteFileU(script_path);
	}
#endif
	free(url);
	SendMessage(hMainDialog, UM_ENABLE_CONTROLS, 0, 0);
	dialog_showing--;
	CoUninitialize();
	ExitThread(dwExitCode);
}

BOOL DownloadISO()
{
	if (CreateThread(NULL, 0, DownloadISOThread, NULL, 0, NULL) == NULL) {
		uprintf("Unable to start Windows ISO download thread");
		ErrorStatus = RUFUS_ERROR(APPERR(ERROR_CANT_START_THREAD));
		SendMessage(hMainDialog, UM_ENABLE_CONTROLS, 0, 0);
		return FALSE;
	}
	return TRUE;
}

BOOL IsDownloadable(const char* url)
{
	DWORD dwSize, dwTotalSize = 0;
	const char* accept_types[] = { "*/*\0", NULL };
	char hostname[64], urlpath[128];
	HINTERNET hSession = NULL, hConnection = NULL, hRequest = NULL;
	URL_COMPONENTSA UrlParts = { sizeof(URL_COMPONENTSA), NULL, 1, (INTERNET_SCHEME)0,
		hostname, sizeof(hostname), 0, NULL, 1, urlpath, sizeof(urlpath), NULL, 1 };

	PF_TYPE_DECL(WINAPI, BOOL, InternetCrackUrlA, (LPCSTR, DWORD, DWORD, LPURL_COMPONENTSA));
	PF_TYPE_DECL(WINAPI, HINTERNET, InternetConnectA, (HINTERNET, LPCSTR, INTERNET_PORT, LPCSTR, LPCSTR, DWORD, DWORD, DWORD_PTR));
	PF_TYPE_DECL(WINAPI, BOOL, InternetCloseHandle, (HINTERNET));
	PF_TYPE_DECL(WINAPI, HINTERNET, HttpOpenRequestA, (HINTERNET, LPCSTR, LPCSTR, LPCSTR, LPCSTR, LPCSTR*, DWORD, DWORD_PTR));
	PF_TYPE_DECL(WINAPI, BOOL, HttpSendRequestA, (HINTERNET, LPCSTR, DWORD, LPVOID, DWORD));
	PF_TYPE_DECL(WINAPI, BOOL, HttpQueryInfoA, (HINTERNET, DWORD, LPVOID, LPDWORD, LPDWORD));
	PF_INIT_OR_OUT(InternetCrackUrlA, WinInet);
	PF_INIT_OR_OUT(InternetConnectA, WinInet);
	PF_INIT_OR_OUT(InternetCloseHandle, WinInet);
	PF_INIT_OR_OUT(HttpOpenRequestA, WinInet);
	PF_INIT_OR_OUT(HttpSendRequestA, WinInet);
	PF_INIT_OR_OUT(HttpQueryInfoA, WinInet);

	if (url == NULL)
		return FALSE;

	ErrorStatus = 0;
	DownloadStatus = 404;

	if ((!pfInternetCrackUrlA(url, (DWORD)safe_strlen(url), 0, &UrlParts))
		|| (UrlParts.lpszHostName == NULL) || (UrlParts.lpszUrlPath == NULL))
		goto out;
	hostname[sizeof(hostname) - 1] = 0;

	// Open an Internet session
	hSession = GetInternetSession(NULL, FALSE);
	if (hSession == NULL)
		goto out;

	hConnection = pfInternetConnectA(hSession, UrlParts.lpszHostName, UrlParts.nPort, NULL, NULL, INTERNET_SERVICE_HTTP, 0, (DWORD_PTR)NULL);
	if (hConnection == NULL)
		goto out;

	hRequest = pfHttpOpenRequestA(hConnection, "GET", UrlParts.lpszUrlPath, NULL, NULL, accept_types,
		INTERNET_FLAG_IGNORE_REDIRECT_TO_HTTP | INTERNET_FLAG_IGNORE_REDIRECT_TO_HTTPS |
		INTERNET_FLAG_NO_COOKIES | INTERNET_FLAG_NO_UI | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_HYPERLINK |
		((UrlParts.nScheme == INTERNET_SCHEME_HTTPS) ? INTERNET_FLAG_SECURE : 0), (DWORD_PTR)NULL);
	if (hRequest == NULL)
		goto out;

	if (!pfHttpSendRequestA(hRequest, request_headers, -1L, NULL, 0))
		goto out;

	// Get the file size
	dwSize = sizeof(DownloadStatus);
	pfHttpQueryInfoA(hRequest, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, (LPVOID)&DownloadStatus, &dwSize, NULL);
	if (DownloadStatus != 200)
		goto out;
	dwSize = sizeof(dwTotalSize);
	pfHttpQueryInfoA(hRequest, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER, (LPVOID)&dwTotalSize, &dwSize, NULL);

out:
	if (hRequest)
		pfInternetCloseHandle(hRequest);
	if (hConnection)
		pfInternetCloseHandle(hConnection);
	if (hSession)
		pfInternetCloseHandle(hSession);

	return (dwTotalSize > 0);
}
