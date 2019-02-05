/*
 * Rufus: The Reliable USB Formatting Utility
 * Networking functionality (web file download, check for update, etc.)
 * Copyright © 2012-2018 Pete Batard <pete@akeo.ie>
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
#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>

#include "rufus.h"
#include "missing.h"
#include "resource.h"
#include "msapi_utf8.h"
#include "localization.h"

#include "settings.h"

/* Maximum download chunk size, in bytes */
#define DOWNLOAD_BUFFER_SIZE    10*KB
/* Default delay between update checks (1 day) */
#define DEFAULT_UPDATE_INTERVAL (24*3600)

DWORD DownloadStatus;

extern BOOL force_update, is_x86_32;
static DWORD error_code;
static BOOL update_check_in_progress = FALSE;
static BOOL force_update_check = FALSE;

/*
 * FormatMessage does not handle internet errors
 * https://docs.microsoft.com/en-us/windows/desktop/wininet/wininet-errors
 */
const char* WinInetErrorString(void)
{
	static char error_string[256];
	DWORD size = sizeof(error_string);
	PF_TYPE_DECL(WINAPI, BOOL, InternetGetLastResponseInfoA, (LPDWORD, LPSTR, LPDWORD));
	PF_INIT(InternetGetLastResponseInfoA, WinInet);

	error_code = HRESULT_CODE(GetLastError());

	if ((error_code < INTERNET_ERROR_BASE) || (error_code > INTERNET_ERROR_LAST))
		return WindowsErrorString();

	// TODO: These should be localized on an ad-hoc basis
	switch(error_code) {
	case ERROR_INTERNET_OUT_OF_HANDLES:
		return "No more handles could be generated at this time.";
	case ERROR_INTERNET_TIMEOUT:
		return "The request has timed out.";
	case ERROR_INTERNET_INTERNAL_ERROR:
		return "An internal error has occurred.";
	case ERROR_INTERNET_INVALID_URL:
		return "The URL is invalid.";
	case ERROR_INTERNET_UNRECOGNIZED_SCHEME:
		return "The URL scheme could not be recognized or is not supported.";
	case ERROR_INTERNET_NAME_NOT_RESOLVED:
		return "The server name could not be resolved.";
	case ERROR_INTERNET_PROTOCOL_NOT_FOUND:
		return "The requested protocol could not be located.";
	case ERROR_INTERNET_INVALID_OPTION:
		return "A request specified an invalid option value.";
	case ERROR_INTERNET_BAD_OPTION_LENGTH:
		return "The length of an option supplied is incorrect for the type of option specified.";
	case ERROR_INTERNET_OPTION_NOT_SETTABLE:
		return "The request option cannot be set, only queried.";
	case ERROR_INTERNET_SHUTDOWN:
		return "The Win32 Internet function support is being shut down or unloaded.";
	case ERROR_INTERNET_INCORRECT_USER_NAME:
		return "The request to connect and log on to an FTP server could not be completed because the supplied user name is incorrect.";
	case ERROR_INTERNET_INCORRECT_PASSWORD:
		return "The request to connect and log on to an FTP server could not be completed because the supplied password is incorrect.";
	case ERROR_INTERNET_LOGIN_FAILURE:
		return "The request to connect to and log on to an FTP server failed.";
	case ERROR_INTERNET_INVALID_OPERATION:
		return "The requested operation is invalid.";
	case ERROR_INTERNET_OPERATION_CANCELLED:
		return "The operation was cancelled, usually because the handle on which the request was operating was closed before the operation completed.";
	case ERROR_INTERNET_INCORRECT_HANDLE_TYPE:
		return "The type of handle supplied is incorrect for this operation.";
	case ERROR_INTERNET_INCORRECT_HANDLE_STATE:
		return "The requested operation cannot be carried out because the handle supplied is not in the correct state.";
	case ERROR_INTERNET_NOT_PROXY_REQUEST:
		return "The request cannot be made via a proxy.";
	case ERROR_INTERNET_REGISTRY_VALUE_NOT_FOUND:
		return "A required registry value could not be located.";
	case ERROR_INTERNET_BAD_REGISTRY_PARAMETER:
		return "A required registry value was located but is an incorrect type or has an invalid value.";
	case ERROR_INTERNET_NO_DIRECT_ACCESS:
		return "Direct network access cannot be made at this time.";
	case ERROR_INTERNET_NO_CONTEXT:
		return "An asynchronous request could not be made because a zero context value was supplied.";
	case ERROR_INTERNET_NO_CALLBACK:
		return "An asynchronous request could not be made because a callback function has not been set.";
	case ERROR_INTERNET_REQUEST_PENDING:
		return "The required operation could not be completed because one or more requests are pending.";
	case ERROR_INTERNET_INCORRECT_FORMAT:
		return "The format of the request is invalid.";
	case ERROR_INTERNET_ITEM_NOT_FOUND:
		return "The requested item could not be located.";
	case ERROR_INTERNET_CANNOT_CONNECT:
		return "The attempt to connect to the server failed.";
	case ERROR_INTERNET_CONNECTION_ABORTED:
		return "The connection with the server has been terminated.";
	case ERROR_INTERNET_CONNECTION_RESET:
		return "The connection with the server has been reset.";
	case ERROR_INTERNET_FORCE_RETRY:
		return "Calls for the Win32 Internet function to redo the request.";
	case ERROR_INTERNET_INVALID_PROXY_REQUEST:
		return "The request to the proxy was invalid.";
	case ERROR_INTERNET_HANDLE_EXISTS:
		return "The request failed because the handle already exists.";
	case ERROR_INTERNET_SEC_INVALID_CERT:
		return "The SSL certificate is invalid.";
	case ERROR_INTERNET_SEC_CERT_DATE_INVALID:
		return "SSL certificate date that was received from the server is bad. The certificate is expired.";
	case ERROR_INTERNET_SEC_CERT_CN_INVALID:
		return "SSL certificate common name (host name field) is incorrect.";
	case ERROR_INTERNET_SEC_CERT_ERRORS:
		return "The SSL certificate contains errors.";
	case ERROR_INTERNET_SEC_CERT_NO_REV:
		return "The SSL certificate was not revoked.";
	case ERROR_INTERNET_SEC_CERT_REV_FAILED:
		return "The revocation check of the SSL certificate failed.";
	case ERROR_INTERNET_HTTP_TO_HTTPS_ON_REDIR:
		return "The application is moving from a non-SSL to an SSL connection because of a redirect.";
	case ERROR_INTERNET_HTTPS_TO_HTTP_ON_REDIR:
		return "The application is moving from an SSL to an non-SSL connection because of a redirect.";
	case ERROR_INTERNET_MIXED_SECURITY:
		return "Some of the content being viewed may have come from unsecured servers.";
	case ERROR_INTERNET_CHG_POST_IS_NON_SECURE:
		return "The application is posting and attempting to change multiple lines of text on a server that is not secure.";
	case ERROR_INTERNET_POST_IS_NON_SECURE:
		return "The application is posting data to a server that is not secure.";
	case ERROR_FTP_TRANSFER_IN_PROGRESS:
		return "The requested operation cannot be made on the FTP session handle because an operation is already in progress.";
	case ERROR_FTP_DROPPED:
		return "The FTP operation was not completed because the session was aborted.";
	case ERROR_GOPHER_PROTOCOL_ERROR:
	case ERROR_GOPHER_NOT_FILE:
	case ERROR_GOPHER_DATA_ERROR:
	case ERROR_GOPHER_END_OF_DATA:
	case ERROR_GOPHER_INVALID_LOCATOR:
	case ERROR_GOPHER_INCORRECT_LOCATOR_TYPE:
	case ERROR_GOPHER_NOT_GOPHER_PLUS:
	case ERROR_GOPHER_ATTRIBUTE_NOT_FOUND:
	case ERROR_GOPHER_UNKNOWN_LOCATOR:
		return "Gopher? Really??? What is this, 1994?";
	case ERROR_HTTP_HEADER_NOT_FOUND:
		return "The requested header could not be located.";
	case ERROR_HTTP_DOWNLEVEL_SERVER:
		return "The server did not return any headers.";
	case ERROR_HTTP_INVALID_SERVER_RESPONSE:
		return "The server response could not be parsed.";
	case ERROR_HTTP_INVALID_HEADER:
		return "The supplied header is invalid.";
	case ERROR_HTTP_INVALID_QUERY_REQUEST:
		return "The request made to HttpQueryInfo is invalid.";
	case ERROR_HTTP_HEADER_ALREADY_EXISTS:
		return "The header could not be added because it already exists.";
	case ERROR_HTTP_REDIRECT_FAILED:
		return "The redirection failed because either the scheme changed or all attempts made to redirect failed.";
	case ERROR_INTERNET_SECURITY_CHANNEL_ERROR:
		return "This system's SSL library is too old to be able to access this website.";
	case ERROR_INTERNET_CLIENT_AUTH_CERT_NEEDED:
		return "Client Authentication certificate needed";
	case ERROR_INTERNET_BAD_AUTO_PROXY_SCRIPT:
		return "Bad auto proxy script.";
	case ERROR_INTERNET_UNABLE_TO_DOWNLOAD_SCRIPT:
		return "Unable to download script.";
	case ERROR_INTERNET_NOT_INITIALIZED:
		return "Internet has not be initialized.";
	case ERROR_INTERNET_UNABLE_TO_CACHE_FILE:
		return "Unable to cache the file.";
	case ERROR_INTERNET_TCPIP_NOT_INSTALLED:
		return "TPC/IP not installed.";
	case ERROR_INTERNET_DISCONNECTED:
		return "Internet is disconnected.";
	case ERROR_INTERNET_SERVER_UNREACHABLE:
		return "Server could not be reached.";
	case ERROR_INTERNET_PROXY_SERVER_UNREACHABLE:
		return "Proxy server could not be reached.";
	case ERROR_INTERNET_FAILED_DUETOSECURITYCHECK:
		return "A security check prevented internet connection.";
	case ERROR_INTERNET_NEED_MSN_SSPI_PKG:
		return "This connection requires an MSN Security Support Provider Interface package.";
	case ERROR_INTERNET_LOGIN_FAILURE_DISPLAY_ENTITY_BODY:
		return "Please ask Microsoft about that one!";
	case ERROR_INTERNET_EXTENDED_ERROR:
		if (pfInternetGetLastResponseInfoA != NULL) {
			pfInternetGetLastResponseInfoA(&error_code, error_string, &size);
			return error_string;
		}
		// fall through
	default:
		static_sprintf(error_string, "Unknown internet error 0x%08lX", error_code);
		return error_string;
	}
}

/*
 * Download a file or fill a buffer from an URL
 * Mostly taken from http://support.microsoft.com/kb/234913
 * If file is NULL, a buffer is allocated for the download (that needs to be freed by the caller)
 * If hProgressDialog is not NULL, this function will send INIT and EXIT messages
 * to the dialog in question, with WPARAM being set to nonzero for EXIT on success
 * and also attempt to indicate progress using an IDC_PROGRESS control
 */
static DWORD DownloadToFileOrBuffer(const char* url, const char* file, BYTE** buffer, HWND hProgressDialog)
{
	HWND hProgressBar = NULL;
	BOOL r = FALSE;
	DWORD dwFlags, dwSize, dwWritten, dwDownloaded, dwTotalSize;
	HANDLE hFile = INVALID_HANDLE_VALUE;
	const char* accept_types[] = {"*/*\0", NULL};
	unsigned char buf[DOWNLOAD_BUFFER_SIZE];
	char agent[64], hostname[64], urlpath[128];
	HINTERNET hSession = NULL, hConnection = NULL, hRequest = NULL;
	URL_COMPONENTSA UrlParts = {sizeof(URL_COMPONENTSA), NULL, 1, (INTERNET_SCHEME)0,
		hostname, sizeof(hostname), 0, NULL, 1, urlpath, sizeof(urlpath), NULL, 1};
	const char* short_name;
	size_t i;

	// Can't link with wininet.lib because of sideloading issues
	PF_TYPE_DECL(WINAPI, BOOL, InternetCrackUrlA, (LPCSTR, DWORD, DWORD, LPURL_COMPONENTSA));
	PF_TYPE_DECL(WINAPI, BOOL, InternetGetConnectedState, (LPDWORD, DWORD));
	PF_TYPE_DECL(WINAPI, HINTERNET, InternetOpenA, (LPCSTR, DWORD, LPCSTR, LPCSTR, DWORD));
	PF_TYPE_DECL(WINAPI, HINTERNET, InternetConnectA, (HINTERNET, LPCSTR, INTERNET_PORT, LPCSTR, LPCSTR, DWORD, DWORD, DWORD_PTR));
	PF_TYPE_DECL(WINAPI, BOOL, InternetReadFile, (HINTERNET, LPVOID, DWORD, LPDWORD));
	PF_TYPE_DECL(WINAPI, BOOL, InternetCloseHandle, (HINTERNET));
	PF_TYPE_DECL(WINAPI, HINTERNET, HttpOpenRequestA, (HINTERNET, LPCSTR, LPCSTR, LPCSTR, LPCSTR, LPCSTR*, DWORD, DWORD_PTR));
	PF_TYPE_DECL(WINAPI, BOOL, HttpSendRequestA, (HINTERNET, LPCSTR, DWORD, LPVOID, DWORD));
	PF_TYPE_DECL(WINAPI, BOOL, HttpQueryInfoA, (HINTERNET, DWORD, LPVOID, LPDWORD, LPDWORD));
	PF_INIT_OR_OUT(InternetCrackUrlA, WinInet);
	PF_INIT_OR_OUT(InternetGetConnectedState, WinInet);
	PF_INIT_OR_OUT(InternetOpenA, WinInet);
	PF_INIT_OR_OUT(InternetConnectA, WinInet);
	PF_INIT_OR_OUT(InternetReadFile, WinInet);
	PF_INIT_OR_OUT(InternetCloseHandle, WinInet);
	PF_INIT_OR_OUT(HttpOpenRequestA, WinInet);
	PF_INIT_OR_OUT(HttpSendRequestA, WinInet);
	PF_INIT_OR_OUT(HttpQueryInfoA, WinInet);

	FormatStatus = 0;
	DownloadStatus = 404;
	if (hProgressDialog != NULL) {
		// Use the progress control provided, if any
		hProgressBar = GetDlgItem(hProgressDialog, IDC_PROGRESS);
		if (hProgressBar != NULL) {
			SendMessage(hProgressBar, PBM_SETMARQUEE, FALSE, 0);
			SendMessage(hProgressBar, PBM_SETPOS, 0, 0);
		}
		SendMessage(hProgressDialog, UM_PROGRESS_INIT, 0, 0);
	}

	assert(url != NULL);

	short_name = (file != NULL) ? PathFindFileNameU(file) : PathFindFileNameU(url);

	if (hProgressDialog != NULL) {
		PrintInfo(0, MSG_085, short_name);
		uprintf("Downloading %s", url);
	}

	if ( (!pfInternetCrackUrlA(url, (DWORD)safe_strlen(url), 0, &UrlParts))
	  || (UrlParts.lpszHostName == NULL) || (UrlParts.lpszUrlPath == NULL)) {
		uprintf("Unable to decode URL: %s", WinInetErrorString());
		goto out;
	}
	hostname[sizeof(hostname)-1] = 0;

	// Open an Internet session
	for (i=5; (i>0) && (!pfInternetGetConnectedState(&dwFlags, 0)); i--) {
		Sleep(1000);
	}
	if (i <= 0) {
		// http://msdn.microsoft.com/en-us/library/windows/desktop/aa384702.aspx is wrong...
		SetLastError(ERROR_INTERNET_NOT_INITIALIZED);
		uprintf("Network is unavailable: %s", WinInetErrorString());
		goto out;
	}
	static_sprintf(agent, APPLICATION_NAME "/%d.%d.%d (Windows NT %d.%d%s)",
		rufus_version[0], rufus_version[1], rufus_version[2],
		nWindowsVersion>>4, nWindowsVersion&0x0F, is_x64()?"; WOW64":"");
	hSession = pfInternetOpenA(agent, INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
	if (hSession == NULL) {
		uprintf("Could not open Internet session: %s", WinInetErrorString());
		goto out;
	}

	hConnection = pfInternetConnectA(hSession, UrlParts.lpszHostName, UrlParts.nPort, NULL, NULL, INTERNET_SERVICE_HTTP, 0, (DWORD_PTR)NULL);
	if (hConnection == NULL) {
		uprintf("Could not connect to server %s:%d: %s", UrlParts.lpszHostName, UrlParts.nPort, WinInetErrorString());
		goto out;
	}

	hRequest = pfHttpOpenRequestA(hConnection, "GET", UrlParts.lpszUrlPath, NULL, NULL, accept_types,
		INTERNET_FLAG_IGNORE_REDIRECT_TO_HTTP|INTERNET_FLAG_IGNORE_REDIRECT_TO_HTTPS|
		INTERNET_FLAG_NO_COOKIES|INTERNET_FLAG_NO_UI|INTERNET_FLAG_NO_CACHE_WRITE|INTERNET_FLAG_HYPERLINK|
		((UrlParts.nScheme==INTERNET_SCHEME_HTTPS)?INTERNET_FLAG_SECURE:0), (DWORD_PTR)NULL);
	if (hRequest == NULL) {
		uprintf("Could not open URL %s: %s", url, WinInetErrorString());
		goto out;
	}

	if (!pfHttpSendRequestA(hRequest, NULL, 0, NULL, 0)) {
		uprintf("Unable to send request: %s", WinInetErrorString());
		goto out;
	}

	// Get the file size
	dwSize = sizeof(DownloadStatus);
	pfHttpQueryInfoA(hRequest, HTTP_QUERY_STATUS_CODE|HTTP_QUERY_FLAG_NUMBER, (LPVOID)&DownloadStatus, &dwSize, NULL);
	if (DownloadStatus != 200) {
		error_code = ERROR_INTERNET_ITEM_NOT_FOUND;
		uprintf("Unable to access file: %d", DownloadStatus);
		goto out;
	}
	dwSize = sizeof(dwTotalSize);
	if (!pfHttpQueryInfoA(hRequest, HTTP_QUERY_CONTENT_LENGTH|HTTP_QUERY_FLAG_NUMBER, (LPVOID)&dwTotalSize, &dwSize, NULL)) {
		uprintf("Unable to retrieve file length: %s", WinInetErrorString());
		goto out;
	}
	if (hProgressDialog != NULL)
		uprintf("File length: %d bytes", dwTotalSize);

	if (file != NULL) {
		hFile = CreateFileU(file, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (hFile == INVALID_HANDLE_VALUE) {
			uprintf("Unable to create file '%s': %s", short_name, WinInetErrorString());
			goto out;
		}
	} else {
		if (buffer == NULL) {
			uprintf("No buffer pointer provided for download");
			goto out;
		}
		*buffer = malloc(dwTotalSize);
		if (*buffer == NULL) {
			uprintf("Could not allocate buffer for download");
			goto out;
		}
	}

	// Keep checking for data until there is nothing left.
	dwSize = 0;
	while (1) {
		// User may have cancelled the download
		if (IS_ERROR(FormatStatus))
			goto out;
		if (!pfInternetReadFile(hRequest, buf, sizeof(buf), &dwDownloaded) || (dwDownloaded == 0))
			break;
		if (hProgressDialog != NULL) {
			SendMessage(hProgressBar, PBM_SETPOS, (WPARAM)(MAX_PROGRESS*((1.0f*dwSize) / (1.0f*dwTotalSize))), 0);
			PrintInfo(0, MSG_241, (100.0f*dwSize) / (1.0f*dwTotalSize));
		}
		if (file != NULL) {
			if (!WriteFile(hFile, buf, dwDownloaded, &dwWritten, NULL)) {
				uprintf("Error writing file '%s': %s", short_name, WinInetErrorString());
				goto out;
			} else if (dwDownloaded != dwWritten) {
				uprintf("Error writing file '%s': Only %d/%d bytes written", short_name, dwWritten, dwDownloaded);
				goto out;
			}
		} else {
			memcpy(&(*buffer)[dwSize], buf, dwDownloaded);
		}
		dwSize += dwDownloaded;
	}

	if (dwSize != dwTotalSize) {
		uprintf("Could not download complete file - read: %d bytes, expected: %d bytes", dwSize, dwTotalSize);
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_WRITE_FAULT;
		goto out;
	} else {
		DownloadStatus = 200;
		r = TRUE;
		if (hProgressDialog != NULL) {
			uprintf("Successfully downloaded '%s'", short_name);
			SendMessage(hProgressBar, PBM_SETPOS, (WPARAM)MAX_PROGRESS, 0);
			PrintInfo(0, MSG_241, 100.0f);
		}
	}

out:
	if (hFile != INVALID_HANDLE_VALUE) {
		// Force a flush - May help with the PKI API trying to process downloaded updates too early...
		FlushFileBuffers(hFile);
		CloseHandle(hFile);
	}
	if ((!r) && (file != NULL))
		_unlinkU(file);
	if (hRequest)
		pfInternetCloseHandle(hRequest);
	if (hConnection)
		pfInternetCloseHandle(hConnection);
	if (hSession)
		pfInternetCloseHandle(hSession);

	return r ? dwSize : 0;
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

	buf_len = DownloadToFileOrBuffer(url, NULL, &buf, hProgressDialog);
	if (buf_len == 0)
		goto out;
	sig_len = DownloadToFileOrBuffer(url_sig, NULL, &sig, NULL);
	if ((sig_len != RSA_SIGNATURE_SIZE) || (!ValidateOpensslSignature(buf, buf_len, sig, sig_len))) {
		uprintf("FATAL: Download signature is invalid ✗");
		DownloadStatus = 403;	// Forbidden
		FormatStatus = ERROR_SEVERITY_ERROR | FAC(FACILITY_STORAGE) | APPERR(ERROR_BAD_SIGNATURE);
		goto out;
	}

	uprintf("Download signature is valid ✓");
	DownloadStatus = 206;	// Partial content
	hFile = CreateFileU(file, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE) {
		uprintf("Unable to create file '%s': %s", PathFindFileNameU(file), WinInetErrorString());
		goto out;
	}
	if (!WriteFile(hFile, buf, buf_len, &ret, NULL)) {
		uprintf("Error writing file '%s': %s", PathFindFileNameU(file), WinInetErrorString());
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
		MessageBoxExU(hMainDialog, IS_ERROR(FormatStatus) ? StrError(FormatStatus, FALSE) : WinInetErrorString(),
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

static __inline uint64_t to_uint64_t(uint16_t x[4]) {
	int i;
	uint64_t ret = 0;
	for (i=0; i<3; i++)
		ret = (ret<<16) + x[i];
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
	static const char* archname[] = {"win_x86", "win_x64", "win_arm", "win_arm64", "win_none"};
	static const char* channel[] = {"release", "beta", "test"};		// release channel
	const char* accept_types[] = {"*/*\0", NULL};
	DWORD dwFlags, dwSize, dwDownloaded, dwTotalSize, dwStatus;
	BYTE *sig = NULL;
	char* buf = NULL;
	char agent[64], hostname[64], urlpath[128], sigpath[256];
	OSVERSIONINFOA os_version = {sizeof(OSVERSIONINFOA), 0, 0, 0, 0, ""};
	HINTERNET hSession = NULL, hConnection = NULL, hRequest = NULL;
	URL_COMPONENTSA UrlParts = {sizeof(URL_COMPONENTSA), NULL, 1, (INTERNET_SCHEME)0,
		hostname, sizeof(hostname), 0, NULL, 1, urlpath, sizeof(urlpath), NULL, 1};
	SYSTEMTIME ServerTime, LocalTime;
	FILETIME FileTime;
	int64_t local_time = 0, reg_time, server_time, update_interval;

	// Can't link with wininet.lib because of sideloading issues
	PF_TYPE_DECL(WINAPI, BOOL, InternetCrackUrlA, (LPCSTR, DWORD, DWORD, LPURL_COMPONENTSA));
	PF_TYPE_DECL(WINAPI, BOOL, InternetGetConnectedState, (LPDWORD, DWORD));
	PF_TYPE_DECL(WINAPI, HINTERNET, InternetOpenA, (LPCSTR, DWORD, LPCSTR, LPCSTR, DWORD));
	PF_TYPE_DECL(WINAPI, HINTERNET, InternetConnectA, (HINTERNET, LPCSTR, INTERNET_PORT, LPCSTR, LPCSTR, DWORD, DWORD, DWORD_PTR));
	PF_TYPE_DECL(WINAPI, BOOL, InternetReadFile, (HINTERNET, LPVOID, DWORD, LPDWORD));
	PF_TYPE_DECL(WINAPI, BOOL, InternetCloseHandle, (HINTERNET));
	PF_TYPE_DECL(WINAPI, HINTERNET, HttpOpenRequestA, (HINTERNET, LPCSTR, LPCSTR, LPCSTR, LPCSTR, LPCSTR*, DWORD, DWORD_PTR));
	PF_TYPE_DECL(WINAPI, BOOL, HttpSendRequestA, (HINTERNET, LPCSTR, DWORD, LPVOID, DWORD));
	PF_TYPE_DECL(WINAPI, BOOL, HttpQueryInfoA, (HINTERNET, DWORD, LPVOID, LPDWORD, LPDWORD));
	PF_INIT_OR_OUT(InternetCrackUrlA, WinInet);
	PF_INIT_OR_OUT(InternetGetConnectedState, WinInet);
	PF_INIT_OR_OUT(InternetOpenA, WinInet);
	PF_INIT_OR_OUT(InternetConnectA, WinInet);
	PF_INIT_OR_OUT(InternetReadFile, WinInet);
	PF_INIT_OR_OUT(InternetCloseHandle, WinInet);
	PF_INIT_OR_OUT(HttpOpenRequestA, WinInet);
	PF_INIT_OR_OUT(HttpSendRequestA, WinInet);
	PF_INIT_OR_OUT(HttpQueryInfoA, WinInet);

	update_check_in_progress = TRUE;
	verbose = ReadSetting32(SETTING_VERBOSE_UPDATES);
	// Without this the FileDialog will produce error 0x8001010E when compiled for Vista or later
	IGNORE_RETVAL(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED));
	// Unless the update was forced, wait a while before performing the update check
	if (!force_update_check) {
		// It would of course be a lot nicer to use a timer and wake the thread, but my
		// development time is limited and this is FASTER to implement.
		do {
			for (i=0; (i<30) && (!force_update_check); i++)
				Sleep(500);
		} while ((!force_update_check) && ((iso_op_in_progress || format_op_in_progress || (dialog_showing>0))));
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
			local_time = ((((int64_t)FileTime.dwHighDateTime)<<32) + FileTime.dwLowDateTime) / 10000000;
			vvuprintf("Local time: %" PRId64, local_time);
			if (local_time < reg_time + update_interval) {
				vuprintf("Next update check in %" PRId64 " seconds.", reg_time + update_interval - local_time);
				goto out;
			}
		}
	}

	PrintInfoDebug(3000, MSG_243);
	status++;	// 1

	if (!GetVersionExA(&os_version)) {
		uprintf("Could not read Windows version - Check for updates cancelled.");
		goto out;
	}

	if ((!pfInternetCrackUrlA(server_url, (DWORD)safe_strlen(server_url), 0, &UrlParts)) || (!pfInternetGetConnectedState(&dwFlags, 0)))
		goto out;
	hostname[sizeof(hostname)-1] = 0;

	static_sprintf(agent, APPLICATION_NAME "/%d.%d.%d (Windows NT %d.%d%s)",
		rufus_version[0], rufus_version[1], rufus_version[2],
		nWindowsVersion >> 4, nWindowsVersion & 0x0F, is_x64() ? "; WOW64" : "");
	hSession = pfInternetOpenA(agent, INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
	if (hSession == NULL)
		goto out;
	hConnection = pfInternetConnectA(hSession, UrlParts.lpszHostName, UrlParts.nPort, NULL, NULL, INTERNET_SERVICE_HTTP, 0, (DWORD_PTR)NULL);
	if (hConnection == NULL)
		goto out;

	status++;	// 2
	// BETAs are only made available for x86_32
	if (is_x86_32)
		releases_only = !ReadSettingBool(SETTING_INCLUDE_BETAS);

	// Test releases get their own distribution channel (and also force beta checks)
#if defined(TEST)
	max_channel = (int)ARRAYSIZE(channel);
#else
	max_channel = releases_only ? 1 : (int)ARRAYSIZE(channel) - 1;
#endif
	for (k=0; (k<max_channel) && (!found_new_version); k++) {
		uprintf("Checking %s channel...", channel[k]);
		// At this stage we can query the server for various update version files.
		// We first try to lookup for "<appname>_<os_arch>_<os_version_major>_<os_version_minor>.ver"
		// and then remove each of the <os_> components until we find our match. For instance, we may first
		// look for rufus_win_x64_6.2.ver (Win8 x64) but only get a match for rufus_win_x64_6.ver (Vista x64 or later)
		// This allows sunsetting OS versions (eg XP) or providing different downloads for different archs/groups.
		static_sprintf(urlpath, "%s%s%s_%s_%lu.%lu.ver", APPLICATION_NAME, (k==0)?"":"_",
			(k==0)?"":channel[k], archname[GetCpuArch()], os_version.dwMajorVersion, os_version.dwMinorVersion);
		vuprintf("Base update check: %s", urlpath);
		for (i=0, j=(int)safe_strlen(urlpath)-5; (j>0)&&(i<ARRAYSIZE(verpos)); j--) {
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
			if ((hRequest == NULL) || (!pfHttpSendRequestA(hRequest, NULL, 0, NULL, 0))) {
				uprintf("Unable to send request: %s", WinInetErrorString());
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
			if ((releases_only) || (k+1 >= ARRAYSIZE(channel)))
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
		server_time = ((((int64_t)FileTime.dwHighDateTime)<<32) + FileTime.dwLowDateTime) / 10000000;
		vvuprintf("Server time: %" PRId64, server_time);
		// Always store the server response time - the only clock we trust!
		WriteSetting64(SETTING_LAST_UPDATE, server_time);
		// Might as well let the user know
		if (!force_update_check) {
			if ((local_time > server_time + 600) || (local_time < server_time - 600)) {
				uprintf("IMPORTANT: Your local clock is more than 10 minutes in the %s. Unless you fix this, " APPLICATION_NAME " may not be able to check for updates...",
					(local_time > server_time + 600)?"future":"past");
			}
		}

		dwSize = sizeof(dwTotalSize);
		if (!pfHttpQueryInfoA(hRequest, HTTP_QUERY_CONTENT_LENGTH|HTTP_QUERY_FLAG_NUMBER, (LPVOID)&dwTotalSize, &dwSize, NULL))
			goto out;

		safe_free(buf);
		// Make sure the file is NUL terminated
		buf = (char*)calloc(dwTotalSize+1, 1);
		if (buf == NULL)
			goto out;
		// This is a version file - we should be able to gulp it down in one go
		if (!pfInternetReadFile(hRequest, buf, dwTotalSize, &dwDownloaded) || (dwDownloaded != dwTotalSize))
			goto out;
		vuprintf("Successfully downloaded version file (%d bytes)", dwTotalSize);

		// Now download the signature file
		static_sprintf(sigpath, "%s/%s.sig", server_url, urlpath);
		dwDownloaded = DownloadToFileOrBuffer(sigpath, NULL, &sig, NULL);
		if ((dwDownloaded != RSA_SIGNATURE_SIZE) || (!ValidateOpensslSignature(buf, dwTotalSize, sig, dwDownloaded))) {
			uprintf("FATAL: Version signature is invalid!");
			goto out;
		}
		vuprintf("Version signature is valid");

		status++;
		parse_update(buf, dwTotalSize+1);

		vuprintf("UPDATE DATA:");
		vuprintf("  version: %d.%d.%d (%s)", update.version[0], update.version[1], update.version[2], channel[k]);
		vuprintf("  platform_min: %d.%d", update.platform_min[0], update.platform_min[1]);
		vuprintf("  url: %s", update.download_url);

		found_new_version = ((to_uint64_t(update.version) > to_uint64_t(rufus_version)) || (force_update))
			&& ( (os_version.dwMajorVersion > update.platform_min[0])
			  || ( (os_version.dwMajorVersion == update.platform_min[0]) && (os_version.dwMinorVersion >= update.platform_min[1])) );
		uprintf("N%sew %s version found%c", found_new_version?"":"o n", channel[k], found_new_version?'!':'.');
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
	switch(status) {
	case 1:
		PrintInfoDebug(3000, MSG_244);
		break;
	case 2:
		PrintInfoDebug(3000, MSG_245);
		break;
	case 3:
	case 4:
		PrintInfo(3000, found_new_version?MSG_246:MSG_247);
	default:
		break;
	}
	// Start the new download after cleanup
	if (found_new_version) {
		// User may have started an operation while we were checking
		while ((!force_update_check) && (iso_op_in_progress || format_op_in_progress || (dialog_showing>0))) {
			Sleep(15000);
		}
		DownloadNewVersion();
	} else if (force_update_check) {
		PostMessage(hMainDialog, UM_NO_UPDATE, 0, 0);
	}
	force_update_check = FALSE;
	update_check_in_progress = FALSE;
	ExitThread(0);
}

/*
 * Initiate a check for updates. If force is true, ignore the wait period
 */
BOOL CheckForUpdates(BOOL force)
{
	force_update_check = force;
	if (update_check_in_progress)
		return FALSE;
	if (CreateThread(NULL, 0, CheckForUpdatesThread, NULL, 0, NULL) == NULL) {
		uprintf("Unable to start update check thread");
		return FALSE;
	}
	return TRUE;
}
