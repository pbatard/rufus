/*
 * Rufus: The Reliable USB Formatting Utility
 * PKI functions (code signing, etc.)
 * Copyright © 2015-2016 Pete Batard <pete@akeo.ie>
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
#include <wincrypt.h>
#include <wintrust.h>

#include "rufus.h"
#include "resource.h"
#include "msapi_utf8.h"
#include "localization.h"

#define ENCODING (X509_ASN_ENCODING | PKCS_7_ASN_ENCODING)

// MinGW doesn't seem to have this one
#if !defined(szOID_NESTED_SIGNATURE)
#define szOID_NESTED_SIGNATURE "1.3.6.1.4.1.311.2.4.1"
#endif

// Signatures names we accept. Must be the the exact name, including capitalization,
// that CertGetNameStringA(CERT_NAME_ATTR_TYPE, szOID_COMMON_NAME) returns.
const char* cert_name[3] = { "Akeo Consulting", "Akeo Systems", "Pete Batard" };
// For added security, we also validate the country code of the certificate recipient.
const char* cert_country = "IE";

typedef struct {
	LPWSTR lpszProgramName;
	LPWSTR lpszPublisherLink;
	LPWSTR lpszMoreInfoLink;
} SPROG_PUBLISHERINFO, *PSPROG_PUBLISHERINFO;


/*
 * FormatMessage does not handle PKI errors
 */
const char* WinPKIErrorString(void)
{
	static char error_string[64];
	DWORD error_code = GetLastError();

	if (((error_code >> 16) != 0x8009) && ((error_code >> 16) != 0x800B))
		return WindowsErrorString();

	switch (error_code) {
	case NTE_BAD_UID:
		return "Bad UID.";
	case CRYPT_E_MSG_ERROR:
		return "An error occurred while performing an operation on a cryptographic message.";
	case CRYPT_E_UNKNOWN_ALGO:
		return "Unknown cryptographic algorithm.";
	case CRYPT_E_INVALID_MSG_TYPE:
		return "Invalid cryptographic message type.";
	case CRYPT_E_HASH_VALUE:
		return "The hash value is not correct";
	case CRYPT_E_ISSUER_SERIALNUMBER:
		return "Invalid issuer and/or serial number.";
	case CRYPT_E_BAD_LEN:
		return "The length specified for the output data was insufficient.";
	case CRYPT_E_BAD_ENCODE:
		return "An error occurred during encode or decode operation.";
	case CRYPT_E_FILE_ERROR:
		return "An error occurred while reading or writing to a file.";
	case CRYPT_E_NOT_FOUND:
		return "Cannot find object or property.";
	case CRYPT_E_EXISTS:
		return "The object or property already exists.";
	case CRYPT_E_NO_PROVIDER:
		return "No provider was specified for the store or object.";
	case CRYPT_E_DELETED_PREV:
		return "The previous certificate or CRL context was deleted.";
	case CRYPT_E_NO_MATCH:
		return "Cannot find the requested object.";
	case CRYPT_E_UNEXPECTED_MSG_TYPE:
	case CRYPT_E_NO_KEY_PROPERTY:
	case CRYPT_E_NO_DECRYPT_CERT:
		return "Private key or certificate issue";
	case CRYPT_E_BAD_MSG:
		return "Not a cryptographic message.";
	case CRYPT_E_NO_SIGNER:
		return "The signed cryptographic message does not have a signer for the specified signer index.";
	case CRYPT_E_REVOKED:
		return "The certificate is revoked.";
	case CRYPT_E_NO_REVOCATION_DLL:
	case CRYPT_E_NO_REVOCATION_CHECK:
	case CRYPT_E_REVOCATION_OFFLINE:
	case CRYPT_E_NOT_IN_REVOCATION_DATABASE:
		return "Cannot check certificate revocation.";
	case CRYPT_E_INVALID_NUMERIC_STRING:
	case CRYPT_E_INVALID_PRINTABLE_STRING:
	case CRYPT_E_INVALID_IA5_STRING:
	case CRYPT_E_INVALID_X500_STRING:
	case  CRYPT_E_NOT_CHAR_STRING:
		return "Invalid string.";
	case CRYPT_E_SECURITY_SETTINGS:
		return "The cryptographic operation failed due to a local security option setting.";
	case CRYPT_E_NO_VERIFY_USAGE_CHECK:
	case CRYPT_E_VERIFY_USAGE_OFFLINE:
		return "Cannot complete usage check.";
	case CRYPT_E_NO_TRUSTED_SIGNER:
		return "None of the signers of the cryptographic message or certificate trust list is trusted.";
	case CERT_E_UNTRUSTEDROOT:
		return "The root certificate is not trusted.";
	case TRUST_E_NOSIGNATURE:
		return "Not digitally signed.";
	case TRUST_E_EXPLICIT_DISTRUST:
		return "One of the certificates used was marked as untrusted by the user.";
	case TRUST_E_TIME_STAMP:
		return "The timestamp could not be verified.";
	default:
		static_sprintf(error_string, "Unknown PKI error 0x%08lX", error_code);
		return error_string;
	}
}

// Mostly from https://support.microsoft.com/en-us/kb/323809
char* GetSignatureName(const char* path, const char* country_code)
{
	static char szSubjectName[128];
	char szCountry[3] = "__";
	char *p = NULL, *mpath = NULL;
	BOOL r;
	HMODULE hm;
	HCERTSTORE hStore = NULL;
	HCRYPTMSG hMsg = NULL;
	PCCERT_CONTEXT pCertContext = NULL;
	DWORD dwSize, dwEncoding, dwContentType, dwFormatType;
	PCMSG_SIGNER_INFO pSignerInfo = NULL;
	DWORD dwSignerInfo = 0;
	CERT_INFO CertInfo = { 0 };
	SPROG_PUBLISHERINFO ProgPubInfo = { 0 };
	wchar_t *szFileName;

	// If the path is NULL, get the signature of the current runtime
	if (path == NULL) {
		szFileName = calloc(MAX_PATH, sizeof(wchar_t));
		if (szFileName == NULL)
			return NULL;
		hm = GetModuleHandle(NULL);
		if (hm == NULL) {
			uprintf("PKI: Could not get current executable handle: %s", WinPKIErrorString());
			goto out;
		}
		dwSize = GetModuleFileNameW(hm, szFileName, MAX_PATH);
		if ((dwSize == 0) || ((dwSize == MAX_PATH) && (GetLastError() == ERROR_INSUFFICIENT_BUFFER))) {
			uprintf("PKI: Could not get module filename: %s", WinPKIErrorString());
			goto out;
		}
		mpath = wchar_to_utf8(szFileName);
	} else {
		szFileName = utf8_to_wchar(path);
	}

	// Get message handle and store handle from the signed file.
	r = CryptQueryObject(CERT_QUERY_OBJECT_FILE, szFileName,
		CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED, CERT_QUERY_FORMAT_FLAG_BINARY,
		0, &dwEncoding, &dwContentType, &dwFormatType, &hStore, &hMsg, NULL);
	if (!r) {
		uprintf("PKI: Failed to get signature for '%s': %s", (path==NULL)?mpath:path, WinPKIErrorString());
		goto out;
	}

	// Get signer information size.
	r = CryptMsgGetParam(hMsg, CMSG_SIGNER_INFO_PARAM, 0, NULL, &dwSignerInfo);
	if (!r) {
		uprintf("PKI: Failed to get signer size: %s", WinPKIErrorString());
		goto out;
	}

	// Allocate memory for signer information.
	pSignerInfo = (PCMSG_SIGNER_INFO)calloc(dwSignerInfo, 1);
	if (!pSignerInfo) {
		uprintf("PKI: Could not allocate memory for signer information");
		goto out;
	}

	// Get Signer Information.
	r = CryptMsgGetParam(hMsg, CMSG_SIGNER_INFO_PARAM, 0, (PVOID)pSignerInfo, &dwSignerInfo);
	if (!r) {
		uprintf("PKI: Failed to get signer information: %s", WinPKIErrorString());
		goto out;
	}

	// Search for the signer certificate in the temporary certificate store.
	CertInfo.Issuer = pSignerInfo->Issuer;
	CertInfo.SerialNumber = pSignerInfo->SerialNumber;

	pCertContext = CertFindCertificateInStore(hStore, ENCODING, 0, CERT_FIND_SUBJECT_CERT, (PVOID)&CertInfo, NULL);
	if (!pCertContext) {
		uprintf("PKI: Failed to locate signer certificate in temporary store: %s", WinPKIErrorString());
		goto out;
	}

	// If a country code is provided, validate that the certificate we have is for the same country
	if (country_code != NULL) {
		dwSize = CertGetNameStringA(pCertContext, CERT_NAME_ATTR_TYPE, 0, szOID_COUNTRY_NAME,
			szCountry, sizeof(szCountry));
		if (dwSize < 2) {
			uprintf("PKI: Failed to get Country Code");
			goto out;
		}
		if (strcmpi(country_code, szCountry) != 0) {
			uprintf("PKI: Unexpected Country Code (Found '%s', expected '%s')", szCountry, country_code);
			goto out;
		}
	}

	// Isolate the signing certificate subject name
	dwSize = CertGetNameStringA(pCertContext, CERT_NAME_ATTR_TYPE, 0, szOID_COMMON_NAME,
		szSubjectName, sizeof(szSubjectName));
	if (dwSize <= 1) {
		uprintf("PKI: Failed to get Subject Name");
		goto out;
	}

	if (szCountry[0] == '_')
		uprintf("Binary executable is signed by '%s'", szSubjectName);
	else
		uprintf("Binary executable is signed by '%s' (%s)", szSubjectName, szCountry);
	p = szSubjectName;

out:
	safe_free(mpath);
	safe_free(szFileName);
	safe_free(ProgPubInfo.lpszProgramName);
	safe_free(ProgPubInfo.lpszPublisherLink);
	safe_free(ProgPubInfo.lpszMoreInfoLink);
	safe_free(pSignerInfo);
	if (pCertContext != NULL)
		CertFreeCertificateContext(pCertContext);
	if (hStore != NULL)
		CertCloseStore(hStore, 0);
	if (hMsg != NULL)
		CryptMsgClose(hMsg);
	return p;
}

// The timestamping authorities we use are RFC 3161 compliant
static uint64_t GetRFC3161TimeStamp(PCMSG_SIGNER_INFO pSignerInfo)
{
	BOOL r, found = FALSE;
	DWORD n, dwSize = 0;
	PCRYPT_CONTENT_INFO pCounterSignerInfo = NULL;
	uint64_t ts = 0ULL;
	uint8_t *timestamp_token;
	size_t timestamp_token_size;
	char* timestamp_str;
	size_t timestamp_str_size;

	// Loop through unauthenticated attributes for szOID_RFC3161_counterSign OID
	for (n = 0; n < pSignerInfo->UnauthAttrs.cAttr; n++) {
		if (lstrcmpA(pSignerInfo->UnauthAttrs.rgAttr[n].pszObjId, szOID_RFC3161_counterSign) == 0) {
			// Depending on how Microsoft implemented their timestamp checks, and the fact that we are dealing
			// with UnauthAttrs, there's a possibility that an attacker may add a "fake" RFC 3161 countersigner
			// to try to trick us into using their timestamp data. Detect that.
			if (found) {
				uprintf("PKI: Multiple RFC 3161 countersigners found. This could indicate something very nasty...");
				return 0ULL;
			}
			found = TRUE;

			// Read the countersigner message data
			r = CryptDecodeObjectEx(PKCS_7_ASN_ENCODING, PKCS_CONTENT_INFO,
				pSignerInfo->UnauthAttrs.rgAttr[n].rgValue[0].pbData,
				pSignerInfo->UnauthAttrs.rgAttr[n].rgValue[0].cbData,
				CRYPT_DECODE_ALLOC_FLAG, NULL, (PVOID)&pCounterSignerInfo, &dwSize);
			if (!r) {
				uprintf("PKI: Could not retrieve RFC 3161 countersigner data: %s", WinPKIErrorString());
				continue;
			}

			// Get the RFC 3161 timestamp message
			timestamp_token = get_data_from_asn1(pCounterSignerInfo->Content.pbData,
				pCounterSignerInfo->Content.cbData, szOID_TIMESTAMP_TOKEN,
				// 0x04 = "Octet String" ASN.1 tag
				0x04, &timestamp_token_size);
			if (timestamp_token) {
				timestamp_str = get_data_from_asn1(timestamp_token, timestamp_token_size, NULL,
					// 0x18 = "Generalized Time" ASN.1 tag
					0x18, &timestamp_str_size);
				if (timestamp_str) {
					// As per RFC 3161 The syntax is: YYYYMMDDhhmmss[.s...]Z
					if ((timestamp_str_size < 14) || (timestamp_str[timestamp_str_size - 1] != 'Z')) {
						// Sanity checks
						uprintf("PKI: Not an RFC 3161 timestamp");
						DumpBufferHex(timestamp_str, timestamp_str_size);
					} else {
						ts = strtoull(timestamp_str, NULL, 10);
					}
				}
			}
			LocalFree(pCounterSignerInfo);
		}
	}
	return ts;
}

// The following is used to get the RFP 3161 timestamp of a nested signature
static uint64_t GetNestedRFC3161TimeStamp(PCMSG_SIGNER_INFO pSignerInfo)
{
	BOOL r, found = FALSE;
	DWORD n, dwSize = 0;
	PCRYPT_CONTENT_INFO pNestedSignature = NULL;
	PCMSG_SIGNER_INFO pNestedSignerInfo = NULL;
	HCRYPTMSG hMsg = NULL;
	uint64_t ts = 0ULL;

	// Loop through unauthenticated attributes for szOID_NESTED_SIGNATURE OID
	for (n = 0; ; n++) {
		if (pNestedSignature != NULL) {
			LocalFree(pNestedSignature);
			pNestedSignature = NULL;
		}
		if (hMsg != NULL) {
			CryptMsgClose(hMsg);
			hMsg = NULL;
		}
		safe_free(pNestedSignerInfo);
		if (n >= pSignerInfo->UnauthAttrs.cAttr)
			break;
		if (lstrcmpA(pSignerInfo->UnauthAttrs.rgAttr[n].pszObjId, szOID_NESTED_SIGNATURE) == 0) {
			if (found) {
				uprintf("PKI: Multiple nested signatures found. This could indicate something very nasty...");
				return 0ULL;
			}
			found = TRUE;
			r = CryptDecodeObjectEx(PKCS_7_ASN_ENCODING, PKCS_CONTENT_INFO,
				pSignerInfo->UnauthAttrs.rgAttr[n].rgValue[0].pbData,
				pSignerInfo->UnauthAttrs.rgAttr[n].rgValue[0].cbData,
				CRYPT_DECODE_ALLOC_FLAG, NULL, (PVOID)&pNestedSignature, &dwSize);
			if (!r) {
				uprintf("PKI: Could not retrieve nested signature data: %s", WinPKIErrorString());
				continue;
			}

			hMsg = CryptMsgOpenToDecode(ENCODING, CMSG_DETACHED_FLAG, CMSG_SIGNED, (HCRYPTPROV)NULL, NULL, NULL);
			if (hMsg == NULL) {
				uprintf("PKI: Could not create nested signature message: %s", WinPKIErrorString());
				continue;
			}
			r = CryptMsgUpdate(hMsg, pNestedSignature->Content.pbData, pNestedSignature->Content.cbData, TRUE);
			if (!r) {
				uprintf("PKI: Could not update message: %s", WinPKIErrorString());
				continue;
			}
			// Get nested signer
			r = CryptMsgGetParam(hMsg, CMSG_SIGNER_INFO_PARAM, 0, NULL, &dwSize);
			if (!r) {
				uprintf("PKI: Failed to get nested signer size: %s", WinPKIErrorString());
				continue;
			}
			pNestedSignerInfo = (PCMSG_SIGNER_INFO)calloc(dwSize, 1);
			if (!pNestedSignerInfo) {
				uprintf("PKI: Could not allocate memory for nested signer");
				continue;
			}
			r = CryptMsgGetParam(hMsg, CMSG_SIGNER_INFO_PARAM, 0, (PVOID)pNestedSignerInfo, &dwSize);
			if (!r) {
				uprintf("PKI: Failed to get nested signer information: %s", WinPKIErrorString());
				continue;
			}
			ts = GetRFC3161TimeStamp(pNestedSignerInfo);
		}
	}
	return ts;
}

// Return the signature timestamp (as a YYYYMMDDHHMMSS value) or 0 on error
uint64_t GetSignatureTimeStamp(const char* path)
{
	char *mpath = NULL;
	BOOL r;
	HMODULE hm;
	HCERTSTORE hStore = NULL;
	HCRYPTMSG hMsg = NULL;
	DWORD dwSize, dwEncoding, dwContentType, dwFormatType;
	PCMSG_SIGNER_INFO pSignerInfo = NULL;
	DWORD dwSignerInfo = 0;
	wchar_t *szFileName;
	uint64_t timestamp = 0ULL, nested_timestamp;

	// If the path is NULL, get the signature of the current runtime
	if (path == NULL) {
		szFileName = calloc(MAX_PATH, sizeof(wchar_t));
		if (szFileName == NULL)
			goto out;
		hm = GetModuleHandle(NULL);
		if (hm == NULL) {
			uprintf("PKI: Could not get current executable handle: %s", WinPKIErrorString());
			goto out;
		}
		dwSize = GetModuleFileNameW(hm, szFileName, MAX_PATH);
		if ((dwSize == 0) || ((dwSize == MAX_PATH) && (GetLastError() == ERROR_INSUFFICIENT_BUFFER))) {
			uprintf("PKI: Could not get module filename: %s", WinPKIErrorString());
			goto out;
		}
		mpath = wchar_to_utf8(szFileName);
	} else {
		szFileName = utf8_to_wchar(path);
	}

	// Get message handle and store handle from the signed file.
	r = CryptQueryObject(CERT_QUERY_OBJECT_FILE, szFileName,
		CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED, CERT_QUERY_FORMAT_FLAG_BINARY,
		0, &dwEncoding, &dwContentType, &dwFormatType, &hStore, &hMsg, NULL);
	if (!r) {
		uprintf("PKI: Failed to get signature for '%s': %s", (path==NULL)?mpath:path, WinPKIErrorString());
		goto out;
	}

	// Get signer information size.
	r = CryptMsgGetParam(hMsg, CMSG_SIGNER_INFO_PARAM, 0, NULL, &dwSignerInfo);
	if (!r) {
		uprintf("PKI: Failed to get signer size: %s", WinPKIErrorString());
		goto out;
	}

	// Allocate memory for signer information.
	pSignerInfo = (PCMSG_SIGNER_INFO)calloc(dwSignerInfo, 1);
	if (!pSignerInfo) {
		uprintf("PKI: Could not allocate memory for signer information");
		goto out;
	}

	// Get Signer Information.
	r = CryptMsgGetParam(hMsg, CMSG_SIGNER_INFO_PARAM, 0, (PVOID)pSignerInfo, &dwSignerInfo);
	if (!r) {
		uprintf("PKI: Failed to get signer information: %s", WinPKIErrorString());
		goto out;
	}

	// Get the RFC 3161 timestamp
	timestamp = GetRFC3161TimeStamp(pSignerInfo);
	if (timestamp)
		uprintf("Note: '%s' has timestamp %s", (path==NULL)?mpath:path, TimestampToHumanReadable(timestamp));
	// Because we are currently using both SHA-1 and SHA-256 signatures, we are in the very specific
	// situation that Windows may say our executable passes Authenticode validation on Windows 7 or
	// later (which includes timestamp validation) even if the SHA-1 signature or timestamps have
	// been altered.
	// This means that, if we don't also check the nested SHA-256 signature timestamp, an attacker
	// could alter the SHA-1 one (which is the one we use by default for chronology validation) and
	// trick us into using an invalid timestamp value. To prevent this, we validate that, if we have
	// both a regular and nested timestamp, they are within 60 seconds of each other.
	nested_timestamp = GetNestedRFC3161TimeStamp(pSignerInfo);
	if (nested_timestamp)
		uprintf("Note: '%s' has nested timestamp %s", (path==NULL)?mpath:path, TimestampToHumanReadable(nested_timestamp));
	if ((timestamp != 0ULL) && (nested_timestamp != 0ULL)) {
		if (_abs64(nested_timestamp - timestamp) > 100) {
			uprintf("PKI: Signature timestamp and nested timestamp differ by more than a minute. "
				"This could indicate something very nasty...", timestamp, nested_timestamp);
			timestamp = 0ULL;
		}
	}

out:
	safe_free(mpath);
	safe_free(szFileName);
	safe_free(pSignerInfo);
	if (hStore != NULL)
		CertCloseStore(hStore, 0);
	if (hMsg != NULL)
		CryptMsgClose(hMsg);
	return timestamp;
}

// From https://msdn.microsoft.com/en-us/library/windows/desktop/aa382384.aspx
LONG ValidateSignature(HWND hDlg, const char* path)
{
	LONG r;
	WINTRUST_DATA trust_data = { 0 };
	WINTRUST_FILE_INFO trust_file = { 0 };
	GUID guid_generic_verify =	// WINTRUST_ACTION_GENERIC_VERIFY_V2
		{ 0xaac56b, 0xcd44, 0x11d0,{ 0x8c, 0xc2, 0x0, 0xc0, 0x4f, 0xc2, 0x95, 0xee } };
	char *signature_name;
	size_t i;
	uint64_t current_ts, update_ts;

	// Check the signature name. Make it specific enough (i.e. don't simply check for "Akeo")
	// so that, besides hacking our server, it'll place an extra hurdle on any malicious entity
	// into also fooling a C.A. to issue a certificate that passes our test.
	signature_name = GetSignatureName(path, cert_country);
	if (signature_name == NULL) {
		uprintf("PKI: Could not get signature name");
		MessageBoxExU(hDlg, lmprintf(MSG_284), lmprintf(MSG_283), MB_OK | MB_ICONERROR | MB_IS_RTL, selected_langid);
		return TRUST_E_NOSIGNATURE;
	}
	for (i = 0; i < ARRAYSIZE(cert_name); i++) {
		if (strcmp(signature_name, cert_name[i]) == 0)
			break;
	}
	if (i >= ARRAYSIZE(cert_name)) {
		uprintf("PKI: Signature '%s' is unexpected...", signature_name);
		if (MessageBoxExU(hDlg, lmprintf(MSG_285, signature_name), lmprintf(MSG_283),
			MB_YESNO | MB_ICONWARNING | MB_IS_RTL, selected_langid) != IDYES)
			return TRUST_E_EXPLICIT_DISTRUST;
	}

	trust_file.cbStruct = sizeof(trust_file);
	trust_file.pcwszFilePath = utf8_to_wchar(path);
	if (trust_file.pcwszFilePath == NULL) {
		uprintf("PKI: Unable to convert '%s' to UTF16", path);
		return ERROR_SEVERITY_ERROR | FAC(FACILITY_CERT) | ERROR_NOT_ENOUGH_MEMORY;
	}

	trust_data.cbStruct = sizeof(trust_data);
	// NB: WTD_UI_ALL can result in ERROR_SUCCESS even if the signature validation fails,
	// because it still prompts the user to run untrusted software, even after explicitly
	// notifying them that the signature invalid (and of course Microsoft had to make
	// that UI prompt a bit too similar to the other benign prompt you get when running
	// trusted software, which, as per cert.org's assessment, may confuse non-security
	// conscious-users who decide to gloss over these kind of notifications).
	trust_data.dwUIChoice = WTD_UI_NONE;
	// We just downloaded from the Internet, so we should be able to check revocation
	trust_data.fdwRevocationChecks = WTD_REVOKE_WHOLECHAIN;
	// 0x400 = WTD_MOTW  for Windows 8.1 or later
	trust_data.dwProvFlags = WTD_REVOCATION_CHECK_CHAIN | 0x400;
	trust_data.dwUnionChoice = WTD_CHOICE_FILE;
	trust_data.pFile = &trust_file;

	r = WinVerifyTrustEx(INVALID_HANDLE_VALUE, &guid_generic_verify, &trust_data);
	safe_free(trust_file.pcwszFilePath);
	switch (r) {
	case ERROR_SUCCESS:
		// Verify that the timestamp of the downloaded update is in the future of our current one.
		// This is done to prevent the use of an officially signed, but older binary, as potential attack vector.
		current_ts = GetSignatureTimeStamp(NULL);
		if (current_ts == 0ULL) {
			uprintf("PKI: Cannot retreive the current binary's timestamp - Aborting update");
			r = TRUST_E_TIME_STAMP;
		} else {
			update_ts = GetSignatureTimeStamp(path);
			if (update_ts < current_ts) {
				uprintf("PKI: Update timestamp (%" PRIi64 ") is younger than ours (%" PRIi64 ") - Aborting update", update_ts, current_ts);
				r = TRUST_E_TIME_STAMP;
			}
		}
		if (r != ERROR_SUCCESS)
		MessageBoxExU(hDlg, lmprintf(MSG_300), lmprintf(MSG_299), MB_OK | MB_ICONERROR | MB_IS_RTL, selected_langid);
		break;
	case TRUST_E_NOSIGNATURE:
		// Should already have been reported, but since we have a custom message for it...
		uprintf("PKI: File does not appear to be signed: %s", WinPKIErrorString());
		MessageBoxExU(hDlg, lmprintf(MSG_284), lmprintf(MSG_283), MB_OK | MB_ICONERROR | MB_IS_RTL, selected_langid);
		break;
	default:
		uprintf("PKI: Failed to validate signature: %s", WinPKIErrorString());
		MessageBoxExU(hDlg, lmprintf(MSG_240), lmprintf(MSG_283), MB_OK | MB_ICONERROR | MB_IS_RTL, selected_langid);
		break;
	}

	return r;
}
