/*
 * Rufus: The Reliable USB Formatting Utility
 * DOS keyboard locale setup
 * Copyright © 2011-2013 Pete Batard <pete@akeo.ie>
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
#include <assert.h>

#include "rufus.h"

/*
 * Note: if you want a book that can be used as a keyboards and codepages bible, I
 * would recommend the "OS/2 Warp Server for e-business - Keyboards and Codepages".
 * See http://www.borgendale.com/keyboard.pdf
 */

/* WinME DOS keyboard 2 letter codes & supported keyboard ID(s), as retrieved
 * from the Millenium disk image in diskcopy.dll on a Windows 7 system
 *
 *	KEYBOARD.SYS
 *		GR 129*
 *		SP 172
 *		PO 163*
 *		FR 120*, 189*
 *		DK 159*
 *		SG 000*
 *		IT 141*, 142*
 *		UK 166*, 168*
 *		SF 150*
 *		BE 120*
 *		NL 143*
 *		NO 155*
 *		CF 058*
 *		SV 153*
 *		SU 153
 *		LA 171*
 *		BR 274*
 *		PL 214*
 *		CZ 243*
 *		SL 245*
 *		YU 234*
 *		HU 208*
 *		US/XX 103*
 *		JP defines ID:194 but points to SP entry
 *
 *	KEYBRD2.SYS
 *		GR 129*
 *		RU 441
 *		IT 141*, 142*
 *		UK 166*, 168*
 *		NO 155*
 *		CF 058*
 *		SV 153*
 *		SU 153
 *		BR 274*, 275*
 *		BG 442*
 *		PL 214*
 *		CZ 243*
 *		SL 245*
 *		YU 234*
 *		YC 118
 *		HU 208*
 *		RO 333
 *		IS 161*
 *		TR 179*, 440*
 *		GK 319*
 *		US/XX 103*
 *
 *	KEYBRD3.SYS
 *		GR 129*
 *		SP 172*
 *		FR 189*
 *		DK 159*
 *		SG 000*
 *		IT 141*
 *		UK 166*
 *		SF 150*
 *		BE 120*
 *		NL 143*
 *		SV 153*
 *		SU 153
 *		PL 214*
 *		CZ 243*
 *		SL 245*
 *		YU 234*
 *		HU 208*
 *		RU 091*, 092*, 093*, 341*
 *		UR 094*, 095*, 096*
 *		BL 097*, 098*, 099*
 *		US/XX 103*
 *		JP defines ID:194 but points to SP entry
 *
 *	KEYBRD4.SYS
 *		GK 101*, 319*, 220*
 *		PL 214*
 *		ET 425*
 *		HE 400*
 *		AR 401*, 402*, 403*
 *		US/XX 103*
 */

/*
 * The following lists the keyboard code that are supported in each
 * of the WinMe DOS KEYBOARD.SYS, KEYBRD2.SYS, ...
 */
static const char* ms_kb1[] = {
	"be", "br", "cf", "cz", "dk", "fr", "gr", "hu", "it", "la",
	"nl", "no", "pl", "po", "sf", "sg", "sl", "sp", "su", "sv",
	"uk", "us", "yu" };
static const char* ms_kb2[] = {
	"bg", "br", "cf", "cz", "gk", "gr", "hu", "is", "it", "no",
	"pl", "ro", "ru", "sl", "su", "sv", "tr", "uk", "us", "yc",
	"yu" };
static const char* ms_kb3[] = {
	"be", "bl", "cz", "dk", "fr", "gr", "hu", "it", "nl", "pl",
	"ru", "sf", "sg", "sl", "sp", "su", "sv", "uk", "ur", "us",
	"yu" };
static const char* ms_kb4[] = {
	"ar", "et", "gk", "he", "pl", "us" };

/*
 * The following lists the keyboard code that are supported in each
 * of the FreeDOS DOS KEYBOARD.SYS, KEYBRD2.SYS, ...
 */
static const char* fd_kb1[] = {
	"be", "br", "cf", "co", "cz", "dk", "dv", "fr", "gr", "hu",
	"it", "jp", "la", "lh", "nl", "no", "pl", "po", "rh", "sf",
	"sg", "sk", "sp", "su", "sv", "uk", "us", "yu" };
static const char* fd_kb2[] = {
	"bg", "ce", "gk", "is", "ro", "ru", "rx", "tr", "tt", "yc" };
static const char* fd_kb3[] = {
	"az", "bl", "et", "fo", "hy", "il", "ka", "kk", "ky", "lt",
	"lv", "mk", "mn", "mt", "ph", "sq", "tj", "tm", "ur", "uz",
	"vi" };
static const char* fd_kb4[] = {
	"ar", "bn", "bx", "fx", "ix", "kx", "ne", "ng", "px", "sx",
	"ux" };

typedef struct {
	const char* name;
	ULONG default_cp;
} kb_default;

static kb_default kbdrv_data[] = {
	{ "keyboard.sys", 437 },
	{ "keybrd2.sys", 850 },
	{ "keybrd3.sys", 850 },
	{ "keybrd4.sys", 853 }
};

typedef struct {
	size_t size;
	const char** list;
} kb_list;

static kb_list ms_kb_list[] = {
	{ ARRAYSIZE(ms_kb1), ms_kb1 },
	{ ARRAYSIZE(ms_kb2), ms_kb2 },
	{ ARRAYSIZE(ms_kb3), ms_kb3 },
	{ ARRAYSIZE(ms_kb4), ms_kb4 }
};

static kb_list fd_kb_list[] = {
	{ ARRAYSIZE(fd_kb1), fd_kb1 },
	{ ARRAYSIZE(fd_kb2), fd_kb2 },
	{ ARRAYSIZE(fd_kb3), fd_kb3 },
	{ ARRAYSIZE(fd_kb4), fd_kb4 }
};

static int ms_get_kbdrv(const char* kb)
{
	unsigned int i, j;
	for (i=0; i<ARRAYSIZE(ms_kb_list); i++) {
		for (j=0; j<ms_kb_list[i].size; j++) {
			if (safe_strcmp(ms_kb_list[i].list[j], kb) == 0) {
				return i;
			}
		}
	}
	return -1;
}

static int fd_get_kbdrv(const char* kb)
{
	unsigned int i, j;
	for (i=0; i<ARRAYSIZE(fd_kb_list); i++) {
		for (j=0; j<fd_kb_list[i].size; j++) {
			if (safe_strcmp(fd_kb_list[i].list[j], kb) == 0) {
				return i;
			}
		}
	}
	return -1;
}

/*
 * We display human readable descriptions of the locale in the menu
 * As real estate might be limited, keep it short
 */
static const char* kb_hr_list[][2] = {
	{"ar", "Arabic"},			// Left enabled, but doesn't seem to work in FreeDOS
	{"bg", "Bulgarian"},
	{"ch", "Chinese"},
	{"cz", "Czech"},
	{"dk", "Danish"},
	{"gr", "German"},
	{"sg", "Swiss-German"},
	{"gk", "Greek"},
	{"us", "US-English"},
	{"uk", "UK-English"},
	{"cf", "CA-French"},
	{"dv", "US-Dvorak"},
	{"lh", "US-Dvorak (LH)"},
	{"rh", "US-Dvorak (RH)"},
	{"sp", "Spanish"},
	{"la", "Latin-American"},
	{"su", "Finnish"},
	{"fr", "French"},
	{"be", "Belgian-French"},
	{"sf", "Swiss-French"},
	{"il", "Hebrew"},
	{"hu", "Hungarian"},
	{"is", "Icelandic"},
	{"it", "Italian"},
	{"jp", "Japanese"},
//	{"ko", "Korean"},			// Unsupported by FreeDOS?
	{"nl", "Dutch"},
	{"no", "Norwegian"},
	{"pl", "Polish"},
	{"br", "Brazilian"},
	{"po", "Portuguese"},
	{"ro", "Romanian"},
	{"ru", "Russian"},
	{"yu", "YU-Latin"},
	{"yc", "YU-Cyrillic"},
	{"sl", "Slovak"},
	{"sq", "Albanian"},
	{"sv", "Swedish"},
	{"tr", "Turkish"},
	{"ur", "Ukrainian"},
	{"bl", "Belarusian"},
	{"et", "Estonian"},
	{"lv", "Latvian"},
	{"lt", "Lithuanian"},
	{"tj", "Tajik"},
//	{"fa", "Persian"};			// Unsupported by FreeDOS?
	{"vi", "Vietnamese"},
	{"hy", "Armenian"},
	{"az", "Azeri"},
	{"mk", "Macedonian"},
	{"ka", "Georgian"},
	{"fo", "Faeroese"},
	{"mt", "Maltese"},
	{"kk", "Kazakh"},
	{"ky", "Kyrgyz"},
	{"uz", "Uzbek"},
	{"tm", "Turkmen"},
	{"tt", "Tatar"},
};

static const char* kb_to_hr(const char* kb)
{
	int i;
	for (i=0; i<ARRAYSIZE(kb_hr_list); i++) {
		if (safe_strcmp(kb, kb_hr_list[i][0]) == 0) {
			return kb_hr_list[i][1];
		}
	}
	// Should never happen, so let's try to get some attention here
	assert(i < ARRAYSIZE(kb_hr_list));
	return NULL;
}

typedef struct {
	ULONG cp;
	const char* name;
} cp_list;

// From FreeDOS CPX pack as well as
// http://msdn.microsoft.com/en-us/library/dd317756.aspx
static cp_list cp_hr_list[] = {
	{ 113, "Lat-Yugoslavian"},
	{ 437, "US-English"},
	{ 667, "Polish"},
	{ 668, "Polish (Alt)"},
	{ 708, "Arabic (708)"},
	{ 709, "Arabic (709)"},
	{ 710, "Arabic (710)"},
	{ 720, "Arabic (DOS)"},
	{ 737, "Greek (DOS)"},
	{ 770, "Baltic"},
	{ 771, "Cyr-Russian (KBL)"},
	{ 772, "Cyr-Russian"},
	{ 773, "Baltic Rim (Old)"},
	{ 774, "Lithuanian"},
	{ 775, "Baltic Rim"},
	{ 777, "Acc-Lithuanian (Old)"},
	{ 778, "Acc-Lithuanian"},
	{ 790, "Mazovian-Polish"},
	{ 808, "Cyr-Russian (Euro)"},
	{ 848, "Cyr-Ukrainian (Euro)"},
	{ 849, "Cyr-Belarusian (Euro)"},
	{ 850, "Western-European"},
	{ 851, "Greek"},
	{ 852, "Central-European"},
	{ 853, "Southern-European"},
	{ 855, "Cyr-South-Slavic"},
	{ 856, "Hebrew II"},
	{ 857, "Turkish"},
	{ 858, "Western-European (Euro)"},
	{ 859, "Western-European (Alt)"},
	{ 860, "Portuguese"},
	{ 861, "Icelandic"},
	{ 862, "Hebrew"},
	{ 863, "Canadian-French"},
	{ 864, "Arabic"},
	{ 865, "Nordic"},
	{ 866, "Cyr-Russian"},
	{ 867, "Czech Kamenicky"},
	{ 869, "Modern Greek"},
	{ 872, "Cyr-South-Slavic (Euro)"},
	{ 874, "Thai"},
	{ 895, "Czech Kamenicky (Alt)"},
	{ 899, "Armenian"},
	{ 932, "Japanese"},
	{ 936, "Chinese (Simplified)"},
	{ 949, "Korean"},
	{ 950, "Chinese (Traditional)"},
	{ 991, "Mazovian-Polish (Zloty)"},
	{ 1116, "Estonian"},
	{ 1117, "Latvian"},
	{ 1118, "Lithuanian"},
	{ 1119, "Cyr-Russian (Alt)"},
	{ 1125, "Cyr-Ukrainian"},
	{ 1131, "Cyr-Belarusian"},
	{ 1250, "Central European"},
	{ 1251, "Cyrillic"},
	{ 1252, "Western European"},
	{ 1253, "Greek"},
	{ 1254, "Turkish"},
	{ 1255, "Hebrew"},
	{ 1256, "Arabic"},
	{ 1257, "Baltic"},
	{ 1258, "Vietnamese"},
	{ 1361, "Korean"},
	{ 3012, "Cyr-Latvian"},
	{ 3021, "Cyr-Bulgarian"},
	{ 3845, "Hungarian"},
	{ 3846, "Turkish"},
	{ 3848, "Brazilian (ABICOMP)"},
	{ 30000, "Saami"},
	{ 30001, "Celtic"},
	{ 30002, "Cyr-Tajik"},
	{ 30003, "Latin American"},
	{ 30004, "Greenlandic"},
	{ 30005, "Nigerian"},
	{ 30006, "Vietnamese"},
	{ 30007, "Latin"},
	{ 30008, "Cyr-Ossetian"},
	{ 30009, "Romani"},
	{ 30010, "Cyr-Moldovan"},
	{ 30011, "Cyr-Chechen"},
	{ 30012, "Cyr-Siberian"},
	{ 30013, "Cyr-Turkic"},
	{ 30014, "Cyr-Finno-Ugric"},
	{ 30015, "Cyr-Khanty"},
	{ 30016, "Cyr-Mansi"},
	{ 30017, "Cyr-Northwestern"},
	{ 30018, "Lat-Tatar"},
	{ 30019, "Lat-Chechen"},
	{ 30020, "Low-Saxon and Frisian"},
	{ 30021, "Oceanian"},
	{ 30022, "First Nations"},
	{ 30023, "Southern African"},
	{ 30024, "North & East African"},
	{ 30025, "Western African"},
	{ 30026, "Central African"},
	{ 30027, "Beninese"},
	{ 30028, "Nigerian (Alt)"},
	{ 30029, "Mexican"},
	{ 30030, "Mexican (Alt)"},
	{ 30031, "Northern-European"},
	{ 30032, "Nordic"},
	{ 30033, "Crimean-Tatar (Hryvnia)"},
	{ 30034, "Cherokee"},
	{ 30039, "Cyr-Ukrainian (Hryvnia)"},
	{ 30040, "Cyr-Russian (Hryvnia)"},
	{ 58152, "Cyr-Kazakh (Euro)"},
	{ 58210, "Cyr-Azeri"},
	{ 58335, "Kashubian"},
	{ 59234, "Cyr-Tatar"},
	{ 59829, "Georgian"},
	{ 60258, "Lat-Azeri"},
	{ 60853, "Georgian (Alt)"},
	{ 62306, "Cyr-Uzbek"}
};

static const char* cp_to_hr(ULONG cp)
{
	int i;
	for (i=0; i<ARRAYSIZE(cp_hr_list); i++) {
		if (cp_hr_list[i].cp == cp) {
			return cp_hr_list[i].name;
		}
	}
	// Should never happen, so this oughta get some attention
	assert(i < ARRAYSIZE(cp_hr_list));
	return NULL;
}

// http://blogs.msdn.com/b/michkap/archive/2004/12/05/275231.aspx
static const char* get_kb(void)
{
	unsigned int kbid;
	char kbid_str[KL_NAMELENGTH];
	int pass;

	// Count on Microsoft to add convolution to a simple operation.
	// We use GetKeyboardLayout() because it returns an HKL, which for instance
	// doesn't tell us if the *LAYOUT* is Dvorak or something else. For that we
	// need an KLID which GetKeyboardLayoutNameA() does return ...but only as a
	// string of an hex value...
	GetKeyboardLayoutNameA(kbid_str);
	if (sscanf(kbid_str, "%x", &kbid) == 0) {
		uprintf("Could not scan keyboard layout name - falling back to US as default\n");
		kbid = 0x00000409;
	}
	uprintf("Windows KBID 0x%08x\n", kbid);

	for (pass=0; pass<3; pass++) {
		// Some of these return values are defined in
		// HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Keyboard Layout\DosKeybCodes
		// Others are picked up in FreeDOS official keyboard layouts, v3.0
		// Note: keyboard values are meant to start at 0x400. The cases below 0x400 are added
		// to attempt to figure out a "best match" in case we couldn't find a supported keyboard
		// by using the one most relevant to the language being spoken. Also we intentionally
		// group keyboards that shouldn't be together

		// Note: these cases are mostly organized first by (kbid & 0x3ff) and then by
		// ascending order for same (kbid & 0x3ff)
		switch(kbid) {
		case 0x00000001:
		case 0x00010401: // Arabic (102)
		case 0x00020401: // Arabic (102) AZERTY
			return "ar";
		case 0x00000002:
		case 0x00000402: // Bulgarian (Typewriter)
		case 0x00010402: // Bulgarian (Latin)
		case 0x00020402: // Bulgarian (Phonetic)
		case 0x00030402: // Bulgarian
		case 0x00040402: // Bulgarian (Phonetic Traditional)
			return "bg";
		case 0x00000004:
		case 0x00000404: // Chinese (Traditional) - US Keyboard
		case 0x00000804: // Chinese (Simplified) - US Keyboard
		case 0x00000c04: // Chinese (Traditional, Hong Kong) - US Keyboard
		case 0x00001004: // Chinese (Simplified, Singapore) - US Keyboard
		case 0x00001404: // Chinese (Traditional, Macao) - US Keyboard
			return "ch";
		case 0x00000005:
		case 0x00000405: // Czech
		case 0x00010405: // Czech (QWERTY)
		case 0x00020405: // Czech Programmers
			return "cz";
		case 0x00000006:
		case 0x00000406: // Danish
			return "dk";
		case 0x00000007:
		case 0x00000407: // German
		case 0x00010407: // German (IBM)
			return "gr";
		case 0x00000807: // Swiss German
			return "sg";
		case 0x00000008:
		case 0x00000408: // Greek
		case 0x00010408: // Greek (220)
		case 0x00020408: // Greek (319)
		case 0x00030408: // Greek (220) Latin
		case 0x00040408: // Greek (319) Latin
		case 0x00050408: // Greek Latin
		case 0x00060408: // Greek Polytonic
			return "gk";
		case 0x00000009:
		case 0x00000409: // US
		case 0x00020409: // United States-International
		case 0x00050409: // US English Table for IBM Arabic 238_L
			return "us";
		case 0x00000809: // United Kingdom
		case 0x00000452: // United Kingdom Extended (Welsh)
		case 0x00001809: // Irish
		case 0x00011809: // Gaelic
			return "uk";
		case 0x00000c0c: // Canadian French (Legacy)
		case 0x00001009: // Canadian French
		case 0x00011009: // Canadian Multilingual Standard
			return "cf";
		case 0x00010409: // United States-Dvorak
			return "dv";
		case 0x00030409: // United States-Dvorak for left hand
			return "lh";
		case 0x00040409: // United States-Dvorak for right hand
			return "rh";
		case 0x0000000a:
		case 0x0000040a: // Spanish
		case 0x0001040a: // Spanish Variation
			return "sp";
		case 0x0000080a: // Latin American
			return "la";
		case 0x0000000b:
		case 0x0000040b: // Finnish
		case 0x0001083b: // Finnish with Sami
			return "su";
		case 0x0000000c:
		case 0x0000040c: // French
		case 0x0000046e: // Luxembourgish
			return "fr";
		case 0x0000080c: // Belgian French
		case 0x0001080c: // Belgian (Comma)
			return "be";
		case 0x0000100c: // Swiss French
			return "sf";
		case 0x0000000d:
		case 0x0000040d: // Hebrew
			return "il";
		case 0x0000000e:
		case 0x0000040e: // Hungarian
		case 0x0001040e: // Hungarian 101-key
			return "hu";
		case 0x0000000f:
		case 0x0000040f: // Icelandic
			return "is";
		case 0x00000010:
		case 0x00000410: // Italian
		case 0x00010410: // Italian (142)
			return "it";
		case 0x00000011:
		case 0x00000411: // Japanese
			return "jp";
	//	case 0x00000012:
	//	case 0x00000412: // Korean
	//		return "ko";	// NOT IMPLEMENTED IN FREEDOS?
		case 0x00000013:
		case 0x00000413: // Dutch
		case 0x00000813: // Belgian (Period)
			return "nl";
		case 0x00000014:
		case 0x00000414: // Norwegian
		case 0x0000043b: // Norwegian with Sami
		case 0x0001043b: // Sami Extended Norway
			return "no";
		case 0x00000015:
		case 0x00010415: // Polish (214)
		case 0x00000415: // Polish (Programmers)
			return "pl";
		case 0x00000016:
		case 0x00000416: // Portuguese (Brazilian ABNT)
		case 0x00010416: // Portuguese (Brazilian ABNT2)
			return "br";
		case 0x00000816: // Portuguese (Portugal)
			return "po";
		case 0x00000018:
		case 0x00000418: // Romanian (Legacy)
		case 0x00010418: // Romanian (Standard)
		case 0x00020418: // Romanian (Programmers)
			return "ro";
		case 0x00000019:
		case 0x00000419: // Russian
		case 0x00010419: // Russian (Typewriter)
			return "ru";
		case 0x0000001a:
		case 0x0000041a: // Croatian
		case 0x0000081a: // Serbian (Latin)
		case 0x00000024:
		case 0x00000424: // Slovenian
			return "yu";
		case 0x00000c1a: // Serbian (Cyrillic)
		case 0x0000201a: // Bosnian (Cyrillic)
			return "yc";
		case 0x0000001b:
		case 0x0000041b: // Slovak
		case 0x0001041b: // Slovak (QWERTY)
			return "sl";
		case 0x0000001c:
		case 0x0000041c: // Albanian
			return "sq";
		case 0x0000001d:
		case 0x0000041d: // Swedish
		case 0x0000083b: // Swedish with Sami
			return "sv";
		case 0x0000001f:
		case 0x0000041f: // Turkish Q
		case 0x0001041f: // Turkish F
			return "tr";
		case 0x00000022:
		case 0x00000422: // Ukrainian
		case 0x00020422: // Ukrainian (Enhanced)
			return "ur";
		case 0x00000023:
		case 0x00000423: // Belarusian
			return "bl";
		case 0x00000025:
		case 0x00000425: // Estonian
			return "et";
		case 0x00000026:
		case 0x00000426: // Latvian
		case 0x00010426: // Latvian (QWERTY)
			return "lv";
		case 0x00000027:
		case 0x00000427: // Lithuanian IBM
		case 0x00010427: // Lithuanian
		case 0x00020427: // Lithuanian Standard
			return "lt";
		case 0x00000028:
		case 0x00000428: // Tajik
			return "tj";
	//	case 0x00000029:
	//	case 0x00000429: // Persian
	//		return "fa";	// NOT IMPLEMENTED IN FREEDOS?
		case 0x0000002a:
		case 0x0000042a: // Vietnamese
			return "vi";
		case 0x0000002b:
		case 0x0000042b: // Armenian Eastern
		case 0x0001042b: // Armenian Western
			return "hy";
		case 0x0000002c:
		case 0x0000042c: // Azeri Latin
		case 0x0000082c: // Azeri Cyrillic
			return "az";
		case 0x0000002f:
		case 0x0000042f: // Macedonian (FYROM)
		case 0x0001042f: // Macedonian (FYROM) - Standard
			return "mk";
		case 0x00000037:
		case 0x00000437: // Georgian
		case 0x00010437: // Georgian (QWERTY)
		case 0x00020437: // Georgian (Ergonomic)
			return "ka";
		case 0x00000038:
		case 0x00000438: // Faeroese
			return "fo";
		case 0x0000003a:
		case 0x0000043a: // Maltese 47-Key
		case 0x0001043a: // Maltese 48-Key
			return "mt";
		case 0x0000003f:
		case 0x0000043f: // Kazakh
			return "kk";
		case 0x00000040:
		case 0x00000440: // Kyrgyz Cyrillic
			return "ky";
		case 0x00000043:
		case 0x00000843: // Uzbek Cyrillic
			return "uz";
		case 0x00000042:
		case 0x00000442: // Turkmen
			return "tm";
		case 0x00000044:
		case 0x00000444: // Tatar
			return "tt";

	// Below are more Windows 7 listed keyboards that were left out
	#if 0
		case 0x0000041e: // Thai Kedmanee
		case 0x0001041e: // Thai Pattachote
		case 0x0002041e: // Thai Kedmanee (non-ShiftLock)
		case 0x0003041e: // Thai Pattachote (non-ShiftLock)
		case 0x00000420: // Urdu
		case 0x0000042e: // Sorbian Standard (Legacy)
		case 0x0001042e: // Sorbian Extended
		case 0x0002042e: // Sorbian Standard
		case 0x00000432: // Setswana
		case 0x00000439: // Devanagari - INSCRIPT#
		case 0x00010439: // Hindi Traditional
		case 0x0002083b: // Sami Extended Finland-Sweden
		case 0x00000445: // Bengali
		case 0x00010445: // Bengali - INSCRIPT (Legacy)
		case 0x00020445: // Bengali - INSCRIPT
		case 0x00000446: // Punjabi
		case 0x00000447: // Gujarati
		case 0x00000448: // Oriya
		case 0x00000449: // Tamil
		case 0x0000044a: // Telugu
		case 0x0000044b: // Kannada
		case 0x0000044c: // Malayalam
		case 0x0000044d: // Assamese - INSCRIPT
		case 0x0000044e: // Marathi
		case 0x00000450: // Mongolian Cyrillic
		case 0x00000451: // Tibetan
		case 0x00000850: // Mongolian (Mongolian Script)
		case 0x0000085d: // Inuktitut - Latin
		case 0x0001045d: // Inuktitut - Naqittaut
		case 0x00000453: // Khmer
		case 0x00000454: // Lao
		case 0x0000045a: // Syriac
		case 0x0001045a: // Syriac Phonetic
		case 0x0000045b: // Sinhala
		case 0x0001045b: // Sinhala - Wij 9
		case 0x00000461: // Nepali
		case 0x00000463: // Pashto (Afghanistan)
		case 0x00000465: // Divehi Phonetic
		case 0x00010465: // Divehi Typewriter
		case 0x00000468: // Hausa
		case 0x0000046a: // Yoruba
		case 0x0000046c: // Sesotho sa Leboa
		case 0x0000046d: // Bashkir
		case 0x0000046f: // Greenlandic
		case 0x00000470: // Igbo
		case 0x00000480: // Uyghur (Legacy)
		case 0x00010480: // Uyghur
		case 0x00000481: // Maori
		case 0x00000485: // Yakut
		case 0x00000488: // Wolof
	#endif
		default:
			if (pass == 0) {
				// If we didn't get a match 1st time around, try to match
				// the primary language of the keyboard
				kbid = PRIMARYLANGID(kbid);
			} else if (pass == 1) {
				// If we still didn't get a match, use the system's primary language
				kbid = PRIMARYLANGID(GetSystemDefaultLangID());
				uprintf("Unable to match KBID, trying LangID 0x%04x\n", kbid);
			}
			break;
		}
	}
	uprintf("Unable to match KBID and LangID - defaulting to US\n");
	return "us";
}

/*
 * From WinME DOS
 *
 *	EGA.CPI:
 *		0x01B5	437 (United States)
 *		0x0352	850 (Latin 1)
 *		0x0354	852 (Latin 2)
 *		0x035C	860 (Portuguese)
 *		0x035F	863 (French Canadian)
 *		0x0361	865 (Nordic)
 *
 *	EGA2.CPI:
 *		0x0352	850 (Latin 1)
 *		0x0354	852 (Latin 2)
 *		0x0359	857 (Turkish)
 *		0x035D	861 (Icelandic)
 *		0x0365	869 (Greek)
 *		0x02E1	737 (Greek II)
 *
 *	EGA3.CPI:
 *		0x01B5	437 (United States)
 *		0x0307	775 (Baltic)
 *		0x0352	850 (Latin 1)
 *		0x0354	852 (Latin 2)
 *		0x0357	855 (Cyrillic I)
 *		0x0362	866 (Cyrillic II)
 */


// Pick the EGA to use according to the DOS target codepage (see above)
static const char* ms_get_ega(ULONG cp)
{
	switch(cp) {
	case   437: // United States
	case   850: // Latin-1 (Western European)
	case   852: // Latin-2 (Central European)
	case   860: // Portuguese
	case   863: // French Canadian
	case   865: // Nordic
		return "ega.cpi";

//	case   850: // Latin-1 (Western European)
//	case   852: // Latin-2 (Central European)
	case   857: // Turkish
	case   861: // Icelandic
	case   869: // Greek
	case   737: // Greek II
		return "ega2.cpi";

//	case   437: // United States
	case   775: // Baltic
//	case   850: // Latin-1 (Western European)
//	case   852: // Latin-2 (Central European)
	case   855: // Cyrillic I
	case   866: // Cyrillic II
		return "ega3.cpi";

	default:
		return NULL;
	}
}

// Pick the EGA to use according to the DOS target codepage (from CPIDOS' Codepage.txt)
static const char* fd_get_ega(ULONG cp)
{
	switch(cp) {
	case   437: // United States
	case   850: // Latin-1 (Western European)
	case   852: // Latin-2 (Central European)
	case   853: // Latin-3 (Southern European)
	case   857: // Latin-5
	case   858: // Latin-1 with Euro
		return "ega.cpx";
	case   775: // Latin-7 (Baltic Rim)
	case   859: // Latin-9
	case  1116: // Estonian
	case  1117: // Latvian
	case  1118: // Lithuanian
	case  1119: // Cyrillic Russian and Lithuanian (*)
		return "ega2.cpx";
	case   771: // Cyrillic Russian and Lithuanian (KBL)
	case   772: // Cyrillic Russian and Lithuanian
	case   808: // Cyrillic Russian with Euro
	case   855: // Cyrillic South Slavic
	case   866: // Cyrillic Russian
	case   872: // Cyrillic South Slavic with Euro
		return "ega3.cpx";
	case   848: // Cyrillic Ukrainian with Euro
	case   849: // Cyrillic Belarusian with Euro
	case  1125: // Cyrillic Ukrainian
	case  1131: // Cyrillic Belarusian
	case  3012: // Cyrillic Russian and Latvian ("RusLat")
	case 30010: // Cyrillic Gagauz and Moldovan
		return "ega4.cpx";
	case   113: // Yugoslavian Latin
	case   737: // Greek-2
	case   851: // Greek (old codepage)
//	case   852: // Latin-2
//	case   858: // Multilingual Latin-1 with Euro
	case   869: // Greek
		return "ega5.cpx";
	case   899: // Armenian
	case 30008: // Cyrillic Abkhaz and Ossetian
	case 58210: // Cyrillic Russian and Azeri
	case 59829: // Georgian
	case 60258: // Cyrillic Russian and Latin Azeri
	case 60853: // Georgian with capital letters
		return "ega6.cpx";
	case 30011: // Cyrillic Russian Southern District
	case 30013: // Cyrillic Volga District: // Turkic languages
	case 30014: // Cyrillic Volga District: // Finno-ugric languages
	case 30017: // Cyrillic Northwestern District
	case 30018: // Cyrillic Russian and Latin Tatar
	case 30019: // Cyrillic Russian and Latin Chechen
		return "ega7.cpx";
	case   770: // Baltic
	case   773: // Latin-7 (old standard)
	case   774: // Lithuanian
//	case   775: // Latin-7
	case   777: // Accented Lithuanian (old)
	case   778: // Accented Lithuanian
		return "ega8.cpx";
//	case   858: // Latin-1 with Euro
	case   860: // Portuguese
	case   861: // Icelandic
	case   863: // Canadian French
	case   865: // Nordic
	case   867: // Czech Kamenicky
		return "ega9.cpx";
	case   667: // Polish
	case   668: // Polish (polish letters on cp852 codepoints)
	case   790: // Polish Mazovia
//	case   852: // Latin-2
	case   991: // Polish Mazovia with Zloty sign
	case  3845: // Hungarian
		return "ega10.cpx";
//	case   858: // Latin-1 with Euro
	case 30000: // Saami
	case 30001: // Celtic
	case 30004: // Greenlandic
	case 30007: // Latin
	case 30009: // Romani
		return "ega11.cpx";
//	case   852: // Latin-2
//	case   858: // Latin-1 with Euro
	case 30003: // Latin American
	case 30029: // Mexican
	case 30030: // Mexican II
	case 58335: // Kashubian
		return "ega12";
//	case   852: // Latin-2
	case   895: // Czech Kamenicky
	case 30002: // Cyrillic Tajik
	case 58152: // Cyrillic Kazakh with Euro
	case 59234: // Cyrillic Tatar
	case 62306: // Cyrillic Uzbek
		return "ega13.cpx";
	case 30006: // Vietnamese
	case 30012: // Cyrillic Russian Siberian and Far Eastern Districts
	case 30015: // Cyrillic Khanty
	case 30016: // Cyrillic Mansi
	case 30020: // Low saxon and frisian
	case 30021: // Oceania
		return "ega14.cpx";
	case 30023: // Southern Africa
	case 30024: // Northern and Eastern Africa
	case 30025: // Western Africa
	case 30026: // Central Africa
	case 30027: // Beninese
	case 30028: // Nigerian II
		return "ega15.cpx";
//	case   858: // Latin-1 with Euro
	case  3021: // Cyrillic MIK Bulgarian
	case 30005: // Nigerian
	case 30022: // Canadian First Nations
	case 30031: // Latin-4 (Northern European)
	case 30032: // Latin-6
		return "ega16.cpx";
	case   862: // Hebrew
	case   864: // Arabic
	case 30034: // Cherokee
	case 30033: // Crimean Tatar with Hryvnia
	case 30039: // Cyrillic Ukrainian with Hryvnia
	case 30040: // Cyrillic Russian with Hryvnia
		return "ega17.cpx";
	case   856: // Hebrew II
	case  3846: // Turkish
	case  3848: // Brazilian ABICOMP
		return "ega18.cpx";
	default:
		return NULL;
	}
}

// Transliteration of the codepage (to add currency symbol, etc - FreeDOS only)
static ULONG fd_upgrade_cp(ULONG cp)
{
	switch(cp) {
	case   850: // Latin-1 (Western European)
		return 858; // Latin-1 with Euro
	default:
		return cp;
	}
}


// Don't bother about setting up the country or multiple codepages
BOOL SetDOSLocale(const char* path, BOOL bFreeDOS)
{
	FILE* fd;
	char filename[MAX_PATH];
	ULONG cp;
	const char *kb;
	int kbdrv;
	const char* egadrv;

	// First handle the codepage
	kb = get_kb();
	// We have a keyboard ID, but that doesn't mean it's supported
	kbdrv = bFreeDOS?fd_get_kbdrv(kb):ms_get_kbdrv(kb);
	if (kbdrv < 0) {
		uprintf("Keyboard id '%s' is not supported - falling back to 'us'\n", kb);
		kb = "us";
		kbdrv = bFreeDOS?fd_get_kbdrv(kb):ms_get_kbdrv(kb);	// Always succeeds
	}
	uprintf("Will use DOS keyboard '%s' [%s]\n", kb, kb_to_hr(kb));

	// Now get a codepage
	cp = GetOEMCP();
	egadrv = bFreeDOS?fd_get_ega(cp):ms_get_ega(cp);
	if (egadrv == NULL) {
		// We need to use the fallback CP from the keyboard we got above, as 437 is not always available
		uprintf("Unable to find an EGA file with codepage %d [%s]\n", cp, cp_to_hr(cp));
		cp = kbdrv_data[kbdrv].default_cp;
		egadrv =  bFreeDOS?"ega.cpx":"ega.cpi";
	} else if (bFreeDOS) {
		cp = fd_upgrade_cp(cp);
	}
	uprintf("Will use codepage %d [%s]\n", cp, cp_to_hr(cp));

	if ((cp == 437) && (strcmp(kb, "us") == 0)) {
		// Nothing much to do if US/US - just notify in autoexec.bat
		static_strcpy(filename, path);
		static_strcat(filename, "\\AUTOEXEC.BAT");
		fd = fopen(filename, "w+");
		if (fd == NULL) {
			uprintf("Unable to create 'AUTOEXEC.BAT': %s.\n", WindowsErrorString());
			return FALSE;
		}
		fprintf(fd, "@echo off\n");
		fprintf(fd, "set PATH=.;\\;\\LOCALE\n");
		fprintf(fd, "echo Using %s keyboard with %s codepage [%d]\n", kb_to_hr("us"), cp_to_hr(437), 437);
		fclose(fd);
		uprintf("Successfully wrote 'AUTOEXEC.BAT'\n");
		return TRUE;
	}

	// CONFIG.SYS
	static_strcpy(filename, path);
	static_strcat(filename, "\\CONFIG.SYS");
	fd = fopen(filename, "w+");
	if (fd == NULL) {
		uprintf("Unable to create 'CONFIG.SYS': %s.\n", WindowsErrorString());
		return FALSE;
	}
	if (bFreeDOS) {
		fprintf(fd, "!MENUCOLOR=7,0\nMENU\nMENU   FreeDOS Language Selection Menu\n");
		fprintf(fd, "MENU   \xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD"
			"\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\nMENU\n");
	} else {
		fprintf(fd, "[MENU]\n");
	}
	fprintf(fd, "MENUDEFAULT=1,5\n");
	// Menu item max: 70 characters
	fprintf(fd, "%s1%c Use %s keyboard with %s codepage [%d]\n",
		bFreeDOS?"MENU ":"MENUITEM=", bFreeDOS?')':',', kb_to_hr(kb), cp_to_hr(cp), (int)cp);
	fprintf(fd, "%s2%c Use %s keyboard with %s codepage [%d]\n",
		bFreeDOS?"MENU ":"MENUITEM=", bFreeDOS?')':',', kb_to_hr("us"), cp_to_hr(437), 437);
	fprintf(fd, "%s", bFreeDOS?"MENU\n12?\n":"[1]\ndevice=\\locale\\display.sys con=(ega,,1)\n[2]\n");
	fclose(fd);
	uprintf("Successfully wrote 'CONFIG.SYS'\n");

	// AUTOEXEC.BAT
	static_strcpy(filename, path);
	static_strcat(filename, "\\AUTOEXEC.BAT");
	fd = fopen(filename, "w+");
	if (fd == NULL) {
		uprintf("Unable to create 'AUTOEXEC.BAT': %s.\n", WindowsErrorString());
		return FALSE;
	}
	fprintf(fd, "@echo off\n");
	fprintf(fd, "set PATH=.;\\;\\LOCALE\n");
	if (bFreeDOS)
		fprintf(fd, "display con=(ega,,1)\n");
	fprintf(fd, "GOTO %%CONFIG%%\n");
	fprintf(fd, ":1\n");
	fprintf(fd, "mode con codepage prepare=((%d) \\locale\\%s) > NUL\n", (int)cp, egadrv);
	fprintf(fd, "mode con codepage select=%d > NUL\n", (int)cp);
	fprintf(fd, "keyb %s,,\\locale\\%s\n", kb, kbdrv_data[kbdrv].name);
	fprintf(fd, ":2\n");
	fclose(fd);
	uprintf("Successfully wrote 'AUTOEXEC.BAT'\n");

	return TRUE;
}
