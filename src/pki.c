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
#include <assert.h>

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

// https://msdn.microsoft.com/en-us/library/ee442238.aspx
typedef struct {
	BLOBHEADER BlobHeader;
	RSAPUBKEY  RsaHeader;
	BYTE       Modulus[256];	// 2048 bit modulus
} RSA_2048_PUBKEY;

// The RSA public key modulus for the private key we use to sign the files on the server.
// NOTE 1: This openssl modulus must be *REVERSED* to be usable with Microsoft APIs
// NOTE 2: Also, this modulus is 2052 bits, and not 2048, because openssl adds an extra
// 0x00 at the beginning to force an integer sign. These extra 8 bits *MUST* be removed.
static uint8_t rsa_pubkey_modulus[] = {
	/*
		$ openssl genrsa -aes256 -out private.pem 2048
		$ openssl rsa -in private.pem -pubout -out public.pem
		$ openssl rsa -pubin -inform PEM -text -noout < public.pem
		Public-Key: (2048 bit)
		Modulus:
			00:b6:40:7d:d1:98:7b:81:9e:be:23:0f:32:5d:55:
			60:c6:bf:b4:41:bb:43:1b:f1:e1:e6:f9:2b:d6:dd:
			11:50:e8:b9:3f:19:97:5e:a7:8b:4a:30:c6:76:58:
			72:1c:ac:ff:a1:f8:96:6c:51:5d:13:11:e3:5b:11:
			82:f5:9a:69:e4:28:97:0f:ca:1f:02:ea:1f:7d:dc:
			f9:fc:79:2f:61:ff:8e:45:60:65:ba:37:9b:de:49:
			05:6a:a8:fd:70:d0:0c:79:b6:d7:81:aa:54:c3:c6:
			4a:87:a0:45:ee:ca:d5:d5:c5:c2:ac:86:42:b3:58:
			27:d2:43:b9:37:f2:e6:75:66:17:53:d0:38:d0:c6:
			57:c2:55:36:a2:43:87:ea:24:f0:96:ec:34:dd:79:
			4d:80:54:9d:84:81:a7:cf:0c:a5:7c:d6:63:fa:7a:
			66:30:a9:50:ee:f0:e5:f8:a2:2d:ac:fc:24:21:fe:
			ef:e8:d3:6f:0e:27:b0:64:22:95:3e:6d:a6:66:97:
			c6:98:c2:47:b3:98:69:4d:b1:b5:d3:6f:43:f5:d7:
			a5:13:5e:8c:28:4f:62:4e:01:48:0a:63:89:e7:ca:
			34:aa:7d:2f:bb:70:e0:31:bb:39:49:a3:d2:c9:2e:
			a6:30:54:9a:5c:4d:58:17:d9:fc:3a:43:e6:8e:2a:
			18:e9
		Exponent: 65537 (0x10001)
	*/
	0x00, 0xb6, 0x40, 0x7d, 0xd1, 0x98, 0x7b, 0x81, 0x9e, 0xbe, 0x23, 0x0f, 0x32, 0x5d, 0x55,
	0x60, 0xc6, 0xbf, 0xb4, 0x41, 0xbb, 0x43, 0x1b, 0xf1, 0xe1, 0xe6, 0xf9, 0x2b, 0xd6, 0xdd,
	0x11, 0x50, 0xe8, 0xb9, 0x3f, 0x19, 0x97, 0x5e, 0xa7, 0x8b, 0x4a, 0x30, 0xc6, 0x76, 0x58,
	0x72, 0x1c, 0xac, 0xff, 0xa1, 0xf8, 0x96, 0x6c, 0x51, 0x5d, 0x13, 0x11, 0xe3, 0x5b, 0x11,
	0x82, 0xf5, 0x9a, 0x69, 0xe4, 0x28, 0x97, 0x0f, 0xca, 0x1f, 0x02, 0xea, 0x1f, 0x7d, 0xdc,
	0xf9, 0xfc, 0x79, 0x2f, 0x61, 0xff, 0x8e, 0x45, 0x60, 0x65, 0xba, 0x37, 0x9b, 0xde, 0x49,
	0x05, 0x6a, 0xa8, 0xfd, 0x70, 0xd0, 0x0c, 0x79, 0xb6, 0xd7, 0x81, 0xaa, 0x54, 0xc3, 0xc6,
	0x4a, 0x87, 0xa0, 0x45, 0xee, 0xca, 0xd5, 0xd5, 0xc5, 0xc2, 0xac, 0x86, 0x42, 0xb3, 0x58,
	0x27, 0xd2, 0x43, 0xb9, 0x37, 0xf2, 0xe6, 0x75, 0x66, 0x17, 0x53, 0xd0, 0x38, 0xd0, 0xc6,
	0x57, 0xc2, 0x55, 0x36, 0xa2, 0x43, 0x87, 0xea, 0x24, 0xf0, 0x96, 0xec, 0x34, 0xdd, 0x79,
	0x4d, 0x80, 0x54, 0x9d, 0x84, 0x81, 0xa7, 0xcf, 0x0c, 0xa5, 0x7c, 0xd6, 0x63, 0xfa, 0x7a,
	0x66, 0x30, 0xa9, 0x50, 0xee, 0xf0, 0xe5, 0xf8, 0xa2, 0x2d, 0xac, 0xfc, 0x24, 0x21, 0xfe,
	0xef, 0xe8, 0xd3, 0x6f, 0x0e, 0x27, 0xb0, 0x64, 0x22, 0x95, 0x3e, 0x6d, 0xa6, 0x66, 0x97,
	0xc6, 0x98, 0xc2, 0x47, 0xb3, 0x98, 0x69, 0x4d, 0xb1, 0xb5, 0xd3, 0x6f, 0x43, 0xf5, 0xd7,
	0xa5, 0x13, 0x5e, 0x8c, 0x28, 0x4f, 0x62, 0x4e, 0x01, 0x48, 0x0a, 0x63, 0x89, 0xe7, 0xca,
	0x34, 0xaa, 0x7d, 0x2f, 0xbb, 0x70, 0xe0, 0x31, 0xbb, 0x39, 0x49, 0xa3, 0xd2, 0xc9, 0x2e,
	0xa6, 0x30, 0x54, 0x9a, 0x5c, 0x4d, 0x58, 0x17, 0xd9, 0xfc, 0x3a, 0x43, 0xe6, 0x8e, 0x2a,
	0x18, 0xe9
};

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
	// See also https://docs.microsoft.com/en-gb/windows/desktop/com/com-error-codes-4
	case NTE_BAD_UID:
		return "Bad UID.";
	case NTE_NO_KEY:
		return "Key does not exist.";
	case NTE_BAD_KEYSET:
		return "Keyset does not exist.";
	case NTE_BAD_ALGID:
		return "Invalid algorithm specified.";
	case NTE_BAD_VER:
		return "Bad version of provider.";
	case NTE_BAD_SIGNATURE:
		return "Invalid Signature.";
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
	int i;
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
	for (i = 0; i < 5; i++) {
		r = CryptQueryObject(CERT_QUERY_OBJECT_FILE, szFileName,
			CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED, CERT_QUERY_FORMAT_FLAG_BINARY,
			0, &dwEncoding, &dwContentType, &dwFormatType, &hStore, &hMsg, NULL);
		if (r)
			break;
		if (i == 0)
			uprintf("PKI: Failed to get signature for '%s': %s", (path==NULL)?mpath:path, WinPKIErrorString());
		if (path == NULL)
			break;
		uprintf("PKI: Retrying...");
		Sleep(2000);
	}
	if (!r)
		goto out;

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
			uprintf("PKI: Cannot retrieve the current binary's timestamp - Aborting update");
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

// Why-oh-why am I the only one on github doing this openssl vs MS signature validation?!?
// For once, I'd like to find code samples from *OTHER PEOPLE* who went through this ordeal first...
BOOL ValidateOpensslSignature(BYTE* pbBuffer, DWORD dwBufferLen, BYTE* pbSignature, DWORD dwSigLen)
{
	HCRYPTPROV hProv = 0;
	HCRYPTHASH hHash = 0;
	HCRYPTKEY hPubKey;
	// We could load and convert an openssl PEM, but since we know what we need...
	RSA_2048_PUBKEY pbMyPubKey = {
		{ PUBLICKEYBLOB, CUR_BLOB_VERSION, 0, CALG_RSA_KEYX },
		// $ openssl genrsa -aes256 -out private.pem 2048
		// Generating RSA private key, 2048 bit long modulus
		// e is 65537 (0x010001)
		// => 0x010001 below. Also 0x31415352 = "RSA1"
		{ 0x31415352, sizeof(pbMyPubKey.Modulus) * 8, 0x010001 },
		{ 0 }	// Modulus is initialized below
	};
	USHORT dwMyPubKeyLen = sizeof(pbMyPubKey);
	BOOL r;
	BYTE t;
	int i, j;

	// Get a handle to the default PROV_RSA_AES provider (AES so we get SHA-256 support).
	// 2 passes in case we need to create a new container.
	r = CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_NEWKEYSET | CRYPT_VERIFYCONTEXT);
	if (!r) {
		uprintf("PKI: Could not create the default key container: %s", WinPKIErrorString());
		goto out;
	}

	// Reverse the modulus bytes from openssl (and also remove the extra unwanted 0x00)
	assert(sizeof(rsa_pubkey_modulus) >= sizeof(pbMyPubKey.Modulus));
	for (i = 0; i < sizeof(pbMyPubKey.Modulus); i++)
		pbMyPubKey.Modulus[i] = rsa_pubkey_modulus[sizeof(rsa_pubkey_modulus) -1 - i];

	// Import our RSA public key so that the MS API can use it
	r = CryptImportKey(hProv, (BYTE*)&pbMyPubKey.BlobHeader, dwMyPubKeyLen, 0, 0, &hPubKey);
	if (!r) {
		uprintf("PKI: Could not import public key: %s", WinPKIErrorString());
		goto out;
	}

	// Create the hash object.
	r = CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash);
	if (!r) {
		uprintf("PKI: Could not create empty hash: %s", WinPKIErrorString());
		goto out;
	}

	// Compute the cryptographic hash of the buffer.
	r = CryptHashData(hHash, pbBuffer, dwBufferLen, 0);
	if (!r) {
		uprintf("PKI: Could not hash data: %s", WinPKIErrorString());
		goto out;
	}

	// Reverse the signature bytes
	for (i = 0, j = dwSigLen - 1; i < j; i++, j--) {
		t = pbSignature[i];
		pbSignature[i] = pbSignature[j];
		pbSignature[j] = t;
	}

	// Now that we have all of the public key, hash and signature data in a
	// format that Microsoft can handle, we can call CryptVerifySignature().
	r = CryptVerifySignature(hHash, pbSignature, dwSigLen, hPubKey, NULL, 0);
	if (!r) {
		// If the signature is invalid, clear the buffer so that
		// we don't keep potentially nasty stuff in memory.
		memset(pbBuffer, 0, dwBufferLen);
		uprintf("Signature validation failed: %s", WinPKIErrorString());
	}

out:
	if (hHash)
		CryptDestroyHash(hHash);
	if (hProv)
		CryptReleaseContext(hProv, 0);
	return r;
}
