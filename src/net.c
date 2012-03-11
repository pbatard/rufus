/*
 * Rufus: The Reliable USB Formatting Utility
 * Networking functionality (web file download, etc.)
 * Copyright (c) 2012 Pete Batard <pete@akeo.ie>
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
#include <stdio.h>
#include <string.h>

#include "msapi_utf8.h"
#include "rufus.h"
#include "resource.h"
// winhttp.h is not available for WDK and MinGW32, so we have to use a replacement
#include "net.h"

/*
 * FormatMessage does not handle WinHTTP
 */
const char* WinHTTPErrorString(void)
{
	static char err_string[34];
	DWORD error_code;

	error_code = GetLastError();

	if ((error_code < WINHTTP_ERROR_BASE) || (error_code > WINHTTP_ERROR_LAST))
		return WindowsErrorString();

	switch(error_code) {
	case ERROR_WINHTTP_OUT_OF_HANDLES:
		return "No more handles could be generated at this time.";
	case ERROR_WINHTTP_TIMEOUT:
		return "The request has timed out.";
	case ERROR_WINHTTP_INTERNAL_ERROR:
		return "An internal error has occurred.";
	case ERROR_WINHTTP_INVALID_URL:
		return "The URL is invalid.";
	case ERROR_WINHTTP_UNRECOGNIZED_SCHEME:
		return "The URL scheme could not be recognized or is not supported.";
	case ERROR_WINHTTP_NAME_NOT_RESOLVED:
		return "The server name could not be resolved.";
	case ERROR_WINHTTP_INVALID_OPTION:
		return "The request specified an invalid option value.";
	case ERROR_WINHTTP_OPTION_NOT_SETTABLE:
		return "The request option cannot be set, only queried.";
	case ERROR_WINHTTP_SHUTDOWN:
		return "The Win32 HTTP function support is being shut down or unloaded.";
	case ERROR_WINHTTP_LOGIN_FAILURE:
		return "The request to connect and log on to the server failed.";
	case ERROR_WINHTTP_OPERATION_CANCELLED:
		return "The operation was canceled";
	case ERROR_WINHTTP_INCORRECT_HANDLE_TYPE:
		return "The type of handle supplied is incorrect for this operation.";
	case ERROR_WINHTTP_INCORRECT_HANDLE_STATE:
		return "The requested operation cannot be carried out because the handle supplied is not in the correct state.";
	case ERROR_WINHTTP_CANNOT_CONNECT:
		return "The attempt to connect to the server failed.";
	case ERROR_WINHTTP_CONNECTION_ERROR:
		return "The connection with the server has been terminated.";
	case ERROR_WINHTTP_RESEND_REQUEST:
		return "The Win32 HTTP function needs to redo the request.";
	case ERROR_WINHTTP_SECURE_CERT_DATE_INVALID:
		return "SSL certificate date indicates that the certificate is expired.";
	case ERROR_WINHTTP_SECURE_CERT_CN_INVALID:
		return "SSL certificate common name (host name field) is incorrect.";
	case ERROR_WINHTTP_CLIENT_AUTH_CERT_NEEDED:
		return "Client Authentication certificate needed";
	case ERROR_WINHTTP_SECURE_INVALID_CA:
		return "SSL certificate has been issued by an invalid Certification Authority.";
	case ERROR_WINHTTP_SECURE_CERT_REV_FAILED:
		return "SSL certificate revocation check failed.";
	case ERROR_WINHTTP_CANNOT_CALL_BEFORE_OPEN:
		return "Cannot use this call before WinHttpOpen";
	case ERROR_WINHTTP_CANNOT_CALL_BEFORE_SEND:
		return "Cannot use this call before WinHttpSend";
	case ERROR_WINHTTP_CANNOT_CALL_AFTER_SEND:
		return "Cannot use this call after WinHttpSend";
	case ERROR_WINHTTP_CANNOT_CALL_AFTER_OPEN:
		return "Cannot use this call after WinHttpOpen";
	case ERROR_WINHTTP_HEADER_NOT_FOUND:
		return "HTTP header was not found.";
	case ERROR_WINHTTP_INVALID_SERVER_RESPONSE:
		return "Invalid HTTP server response.";
	case ERROR_WINHTTP_INVALID_HEADER:
		return "Invalid HTTP header.";
	case ERROR_WINHTTP_INVALID_QUERY_REQUEST:
		return "Invalid HTTP query request.";
	case ERROR_WINHTTP_HEADER_ALREADY_EXISTS:
		return "HTTP header already exists.";
	case ERROR_WINHTTP_REDIRECT_FAILED:
		return "HTTP redirect failed.";
	case ERROR_WINHTTP_SECURE_CHANNEL_ERROR:
		return "Unnable to establish secure HTTP channel.";
	case ERROR_WINHTTP_BAD_AUTO_PROXY_SCRIPT:
		return "Bad auto proxy script.";
	case ERROR_WINHTTP_UNABLE_TO_DOWNLOAD_SCRIPT:
		return "Unable to download script.";
	case ERROR_WINHTTP_SECURE_INVALID_CERT:
		return "SSL certificate is invalid.";
	case ERROR_WINHTTP_SECURE_CERT_REVOKED:
		return "SSL certificate has been revoked.";
	case ERROR_WINHTTP_NOT_INITIALIZED:
		return "WinHTTP has not be initialized.";
	case ERROR_WINHTTP_SECURE_FAILURE:
		return "SSL failure.";
	case ERROR_WINHTTP_AUTO_PROXY_SERVICE_ERROR:
		return "Auto proxy service error.";
	case ERROR_WINHTTP_SECURE_CERT_WRONG_USAGE:
		return "Wrong SSL certificate usage.";
	case ERROR_WINHTTP_AUTODETECTION_FAILED:
		return "HTTP autodetection failed.";
	case ERROR_WINHTTP_HEADER_COUNT_EXCEEDED:
		return "HTTP header count exceeded.";
	case ERROR_WINHTTP_HEADER_SIZE_OVERFLOW:
		return "HTTP header size overflow.";
	case ERROR_WINHTTP_CHUNKED_ENCODING_HEADER_SIZE_OVERFLOW:
		return "Chunked encoding HTTP header size overflow.";
	case ERROR_WINHTTP_RESPONSE_DRAIN_OVERFLOW:
		return "Response drain overflow.";
	case ERROR_WINHTTP_CLIENT_CERT_NO_PRIVATE_KEY:
		return "Certificate does not contain a private key.";
	case ERROR_WINHTTP_CLIENT_CERT_NO_ACCESS_PRIVATE_KEY:
		return "Unable to access client certificate's private key.";
	default:
		safe_sprintf(err_string, sizeof(err_string), "WinHTTP unknown error 0x%08X", error_code);
		return err_string;
	}
}

/* 
 * Download a file from an URL
 * Mostly taken from http://msdn.microsoft.com/en-us/library/aa384270.aspx
 */
BOOL DownloadFile(const char* url, const char* file)
{
	BOOL r=FALSE;
	DWORD dwSize, dwDownloaded, dwTotalSize, dwReadSize, dwTotalSizeSize = sizeof(dwTotalSize);
	FILE* fd = NULL;
	LONG progress_style;
	unsigned char* buf = NULL;
	wchar_t wAgent[64], *wUrl = NULL, wHostName[64], wUrlPath[128];
	HINTERNET hSession=NULL, hConnect=NULL, hRequest=NULL;
	URL_COMPONENTSW UrlParts = {sizeof(URL_COMPONENTSW), NULL, 1, (INTERNET_SCHEME)0,
		wHostName, ARRAYSIZE(wHostName), INTERNET_DEFAULT_PORT, NULL, 1, wUrlPath, ARRAYSIZE(wUrlPath), NULL, 1};

	PF_DECL(WinHttpCrackUrl);
	PF_DECL(WinHttpOpen);
	PF_DECL(WinHttpConnect);
	PF_DECL(WinHttpOpenRequest);
	PF_DECL(WinHttpSendRequest);
	PF_DECL(WinHttpReceiveResponse);
	PF_DECL(WinHttpQueryHeaders);
	PF_DECL(WinHttpQueryDataAvailable);
	PF_DECL(WinHttpReadData);
	PF_DECL(WinHttpCloseHandle);

	PF_INIT_OR_OUT(WinHttpCrackUrl, winhttp);
	PF_INIT_OR_OUT(WinHttpOpen, winhttp);
	PF_INIT_OR_OUT(WinHttpConnect, winhttp);
	PF_INIT_OR_OUT(WinHttpOpenRequest, winhttp);
	PF_INIT_OR_OUT(WinHttpSendRequest, winhttp);
	PF_INIT_OR_OUT(WinHttpReceiveResponse, winhttp);
	PF_INIT_OR_OUT(WinHttpQueryHeaders, winhttp);
	PF_INIT_OR_OUT(WinHttpQueryDataAvailable, winhttp);
	PF_INIT_OR_OUT(WinHttpReadData, winhttp);
	PF_INIT_OR_OUT(WinHttpCloseHandle, winhttp);

	wUrl = utf8_to_wchar(url);
	if (wUrl == NULL) goto out;

	// We reuse the ISO progress dialog for download progress
	SetWindowTextU(hISOProgressDlg, "Downloading file...");
	SetWindowTextU(hISOFileName, url);
	progress_style = GetWindowLong(hISOProgressBar, GWL_STYLE);
	SetWindowLong(hISOProgressBar, GWL_STYLE, progress_style & (~PBS_MARQUEE));
	SendMessage(hISOProgressBar, PBM_SETPOS, 0, 0);
	ShowWindow(hISOProgressDlg, SW_SHOW);
	UpdateWindow(hISOProgressDlg);
//	Sleep(3000);

	PrintStatus(0, FALSE, "Downloading %s: Connecting...\n", file);
	uprintf("Downloading %s from %s\n", file, url);

	if (!pfWinHttpCrackUrl(wUrl, 0, 0, &UrlParts)) {
		uprintf("Unable to decode URL: %s\n", WinHTTPErrorString());
		goto out;
	}

	_snwprintf(wAgent, ARRAYSIZE(wAgent), L"Rufus/%d.%d.%d.%d", rufus_version[0], rufus_version[1], rufus_version[2], rufus_version[3]);
	// Use WinHttpOpen to obtain a session handle.
	hSession = pfWinHttpOpen(wAgent, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!hSession) {
		uprintf("Could not open HTTP session: %s\n", WinHTTPErrorString());
		goto out;
	}

	// Specify an HTTP server.
	hConnect = pfWinHttpConnect(hSession, UrlParts.lpszHostName, UrlParts.nPort, 0);
	if (!hConnect) {
		uprintf("Could not connect to HTTP server: %s\n", WinHTTPErrorString());
		goto out;
	}

	// Create an HTTP request handle.
	hRequest = pfWinHttpOpenRequest(hConnect, L"GET", UrlParts.lpszUrlPath,
		NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
		(UrlParts.nScheme == INTERNET_SCHEME_HTTPS)?WINHTTP_FLAG_SECURE:0);
	if (!hRequest) {
		uprintf("Could not create server request: %s\n", WinHTTPErrorString());
		goto out;
	}

	if (!pfWinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0 )) {
		uprintf("Could not send server request: %s\n", WinHTTPErrorString());
		goto out;
	}

	if (!pfWinHttpReceiveResponse(hRequest, NULL)) {
		uprintf("Failure to receive server response: %s\n", WinHTTPErrorString());
		goto out;
	}

	if (!pfWinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_LENGTH|WINHTTP_QUERY_FLAG_NUMBER,
		WINHTTP_HEADER_NAME_BY_INDEX, &dwTotalSize, &dwTotalSizeSize, WINHTTP_NO_HEADER_INDEX)) {
		uprintf("Could not retreive file length: %s\n", WinHTTPErrorString());
		goto out;
	}
	uprintf("File length: %d bytes\n", dwTotalSize);

	fd = fopen(file, "wb");
	if (fd == NULL) {
		uprintf("Unable to create file %s\n", file);
		goto out;
	}

	// Keep checking for data until there is nothing left.
	dwReadSize = 0;
	while(1) {
		if (IS_ERROR(FormatStatus))
			goto out;

		Sleep(250);

		dwSize = 0;
		if (!pfWinHttpQueryDataAvailable(hRequest, &dwSize))
			uprintf("Error in WinHttpQueryDataAvailable: %s\n", WinHTTPErrorString());
		if (dwSize <= 0)
			break;

		// Allocate space for the buffer.
		buf = (unsigned char*)malloc(dwSize+1);
		if (buf == NULL) {
			uprintf("Could not allocate buffer for download.\n");
			goto out;
		}
		if (!pfWinHttpReadData(hRequest, (LPVOID)buf, dwSize, &dwDownloaded)) {
			uprintf("Error in WinHttpReadData: %s\n", WinHTTPErrorString());
			goto out;
		}
		if (dwDownloaded != dwSize) {
			uprintf("Error: expected %d bytes by received %d\n", dwSize, dwDownloaded);
			goto out;
		}
		dwReadSize += dwDownloaded;
		SendMessage(hISOProgressBar, PBM_SETPOS, (WPARAM)(MAX_PROGRESS*((1.0f*dwReadSize)/(1.0f*dwTotalSize))), 0);
		PrintStatus(0, FALSE, "Downloading %s: %0.1f%%\n", file, (100.0f*dwReadSize)/(1.0f*dwTotalSize));
		if (fwrite(buf, 1, dwSize, fd) != dwSize) {
			uprintf("Error writing file %s\n", file);
			goto out;
		}
		safe_free(buf);
	}
	r = (dwReadSize == dwTotalSize);
	if (r)
		uprintf("Successfully downloaded %s\n", file);

out:
	ShowWindow(hISOProgressDlg, SW_HIDE);
	safe_free(wUrl);
	safe_free(buf);
	if (fd != NULL) fclose(fd);
	if (!r) {
		_unlink(file);
		PrintStatus(0, FALSE, "Failed to download file.");
		MessageBoxA(hMainDialog, IS_ERROR(FormatStatus)?StrError(FormatStatus):WinHTTPErrorString(),
		"File download", MB_OK|MB_ICONERROR);
	}
	if (hRequest) pfWinHttpCloseHandle(hRequest);
	if (hConnect) pfWinHttpCloseHandle(hConnect);
	if (hSession) pfWinHttpCloseHandle(hSession);

	return r;
}
