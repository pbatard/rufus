/*
 * Rufus: The Reliable USB Formatting Utility
 * DOS keyboard locale setup
 * Copyright (c) 2011 Pete Batard <pete@akeo.ie>
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

#include "rufus.h"

enum kb_layouts {
	KB_US = 0, KB_GR, KB_HE, KB_FR, KB_SP, KB_IT, KB_SV, KB_NL, KB_BR, KB_NO,
	KB_DK, KB_SU, KB_RU, KB_CZ, KB_PL, KB_HU, KB_PO, KB_TR, KB_GK, KB_BL,
	KB_BG, KB_YU, KB_BE, KB_CF, KB_UK, KB_ET, KB_SF, KB_SG, KB_IS, KB_IME,
	KB_RO, KB_YC, KB_LA, KB_UR, KB_SL, KB_MAX };

static const char* dos_kb_str[KB_MAX] = {
	"us", "gr", "he", "fr", "sp", "it", "sv", "nl", "br", "no",
	"dk", "su", "ru", "cz", "pl", "hu", "po", "tr", "gk", "bl",
	"bg", "yu", "be", "cf", "uk", "et", "sf", "sg", "is", "ime",
	"ro", "yc", "la", "ur", "sl" };

static const char* dos_kb_human_readable_str[KB_MAX] = {
	"US", "German", "Hebrew", "French", "Spanish", "Italian", "Swedish", "Dutch", "Portuguese (Brazilian)", "Norwegian", "Danish",
	"Finnish", "Russian", "Czech", "Polish", "Hungarian", "Portuguese (Portugal)", "Turkish", "Greek", "Russian (Belarus)", "Bulgarian",
	"Serbian/Croatian/Slovenian", "French (Belgium)", "French (Canada)", "English (UK)", "Estonian", "French (Switzerland)", "German (Switzerland)",
	"Icelandic", "CJK Input Method Editor", "Romanian", "Serbian Cyrillic", "Spanish (Latin America)", "Ukrainian", "Slovakian" };

static const char* dos_kb_drv_str[] = {
	"keyboard.sys", "keybrd2.sys", "keybrd3.sys", "keybrd4.sys" };

static const char* dos_con_str[] = {
	"ega,,1", "ega,,2", "ega,,3", "ega,,4", "ega,,h" };

static const char* dos_cpi_str[] = {
	"ega.cpi", "ega2.cpi", "ega3.cpi", "ega4.cpi", "hebega.cpi" };

static const int kb_to_drv[KB_MAX] = {
	-1, 0, 3, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 2, 1, 3, 1, 0, 1, 3, 2,
	1, 1, 0, 1, 0, 3, 0, 0, 1, 0,
	1, 1, 0, 2, 1 };

/*
 * These locale to keyboard conversions are lifted from Microsoft's diskcopy.dll
 * and are unmodified (apart from Simplified Chinese, that was moved to IME, and
 * Tibet, which was moved out of China)
 * If you feel they should be altered, please provide your locale ID in hex as well
 * as the DOS keyboard layout ID you would like to use
 * For the main IDs ref see http://msdn.microsoft.com/en-us/library/cc233965.aspx or
 * http://msdn.microsoft.com/en-us/goglobal/bb896001.aspx
 * Also see http://en.wikipedia.org/wiki/Keyboard_layout for keyboard layouts
 */
static unsigned int syslocale_to_kbid(WORD locale)
{
	int pass = 0;
	WORD mask = 0xffff;

	do {
		switch (locale & mask) {
		case 0x0000:	// ???
		case 0x0001:	// ar		Arabic
	//	case 0x0004:	// zh-Hans	Chinese (Simplified)
	//	moved to KB_IME
		case 0x0009:	// en		English
		case 0x001C:	// sq		Albanian
		case 0x001E:	// th		Thai
		case 0x0020:	// ur		Urdu
		case 0x0021:	// id		Indonesian
		case 0x0026:	// lv		Latvian
		case 0x0027:	// lt		Lithuanian
		case 0x0029:	// fa		Persian
		case 0x002A:	// vi		Vietnamese
		case 0x002B:	// hy		Armenian
		case 0x002C:	// az		Azeri
		case 0x002F:	// mk		Macedonian
		case 0x0036:	// af		Afrikaans
		case 0x0037:	// ka		Georgian
		case 0x0039:	// hi		Hindi
		case 0x003E:	// ms		Malay
		case 0x003F:	// kk		Kazakh
		case 0x0041:	// sw		Kiswahili
		case 0x0043:	// uz		Uzbek
		case 0x0044:	// tt		Tatar
		case 0x0045:	// bn		Bengali
		case 0x0046:	// pa		Punjabi
		case 0x0047:	// gu		Gujarati
		case 0x0048:	// or		Oriya
		case 0x0049:	// ta		Tamil
		case 0x004A:	// te		Telugu
		case 0x004B:	// kn		Kannada
		case 0x004C:	// ml		Malayalam
		case 0x004D:	// as		Assamese
		case 0x004E:	// mr		Marathi
		case 0x004F:	// sa		Sanskrit
		case 0x0051:	// bo		Tibetan
		case 0x0057:	// kok		Konkani
		case 0x0058:	// ???
		case 0x0059:	// ???
		case 0x0060:	// ???
		case 0x0061:	// ne		Nepali
		case 0x0417:	// rm-CH	Romansh (Switzerland)
		case 0x0428:	// tg-Cyrl-TJ	Tajik (Cyrillic, Tajikistan)
		case 0x042E:	// hsb-DE	Upper Sorbian (Germany)
		case 0x0430:	// st-ZA	Sutu (South Africa)
		case 0x0431:	// ts-ZA	Tsonga (South Africa)
		case 0x0432:	// tn-ZA	Setswana (South Africa)
		case 0x0433:	// ven-ZA	Venda (South Africa)
		case 0x0434:	// xh-ZA	isiXhosa (South Africa)
		case 0x0435:	// zu-ZA	isiZulu (South Africa)
		case 0x043A:	// mt-MT	Maltese (Malta)
		case 0x043B:	// se-NO	Sami, Northern (Norway)
		case 0x043D:	// ???
		case 0x043E:	// ms-MY	Malay (Malaysia)
		case 0x0440:	// ky-KG	Kyrgyz (Kyrgyzstan)
		case 0x0442:	// tk-TM	Turkmen (Turkmenistan)
		case 0x0450:	// mn-MN	Mongolian (Cyrillic, Mongolia)
		case 0x0452:	// cy-GB	Welsh (United Kingdom)
		case 0x0453:	// km-KH	Khmer (Cambodia)
		case 0x0454:	// lo-LA	Lao (Laos)
		case 0x0455:	// my-MM	Myanmar (Burmese)
		case 0x045A:	// syr-SY	Syriac (Syria)
		case 0x045B:	// si-LK	Sinhala (Sri Lanka)
		case 0x045C:	// chr-US	Cherokee (United States)
		case 0x045E:	// am-ET	Amharic (Ethiopia)
		case 0x0462:	// fy-NL	Frisian (Netherlands)
		case 0x0463:	// ps-AF	Pashto (Afghanistan)
		case 0x0464:	// fil-PH	Filipino (Philippines)
		case 0x0850:	// mn-Mong-CN	Mongolian (Traditional Mongolian, China)
		case 0x101A:	// hr-BA	Croatian (Latin, Bosnia and Herzegovina)
		case 0x4409:	// en-MY	English (Malaysia)
			return KB_US;		//	English (US)

		case 0x0007:	// de		German
			return KB_GR;		//	German

		case 0x000D:	// he		Hebre
			return KB_HE;		//	Hebrew

		case 0x000C:	// fr		French
			return KB_FR;		//	French

		case 0x0003:	// ca		Catalan
		case 0x000A:	// es		Spanish
		case 0x002D:	// eu		Basque
		case 0x0456:	// gl-ES	Galician (Galicia)
		case 0x0C0A:	// es-ES	Spanish (Spain)
			return KB_SP;		//	Spanish

		case 0x0010:	// it		Italian
			return KB_IT;		//	Italian

		case 0x001D:	// sv		Swedish
			return KB_SV;		//	Swedish

		case 0x0013:	// nl		Dutch
			return KB_NL;		//	Dutch

		case 0x0416:	// pt-BR	Portuguese (Brazil)
			return KB_BR;		//	Portuguese (Brazil)

		case 0x0014:	// no		Norwegian
			return KB_NO;		//	Norwegian

		case 0x0006:	// da		Danish
		case 0x0038:	// fo		Faroese
			return KB_DK;		//	Danish

		case 0x000B:	// fi		Finnish
			return KB_SU;		//	Finnish

		case 0x0019:	// ru		Russian
			return KB_RU;		//	Russian

		case 0x0005:	// cs		Czech
			return KB_CZ;		//	Czech

		case 0x0015:	// pl		Polish
			return KB_PL;		//	Polish

		case 0x000E:	// hu		Hungarian
			return KB_HU;		//	Hungarian

		case 0x0016:	// pt		Portuguese
			return KB_PO;		//	Portuguese (Portugal)

		case 0x001F:	// tr		Turkish
			return KB_TR;		//	Turkish

		case 0x0008:	// el		Greek
			return KB_GK;		//	Greek

		case 0x0023:	// be		Belarussian
			return KB_BL;		//	Russian (Belarus)

		case 0x0002:	// bg		Bulgarian
			return KB_BG;		//	Bulgarian

		case 0x001A:	// hr		Croatian
		case 0x0024:	// sl		Slovenian
		case 0x081A:	// sr-Latn-SP	Serbian (Latin, Serbia)
			return KB_YU;		//	Yugoslavian Latin

		case 0x080C:	// fr-BE	French (Belgium)
		case 0x0813:	// nl-BE	Dutch (Belgium)
			return KB_BE;		//	French (Belgium)

		case 0x0C0C:	// fr-CA	French (Canada)
		case 0x1009:	// en-CA	English (Canada)
			return KB_CF;		//	French (Canada)

		case 0x043C:	// ga-GB	Gaelic (Scotland)
		case 0x0809:	// en-GB	English (United Kingdom)
		case 0x083C:	// ga-IE	Irish (Ireland)
		case 0x1809:	// en-IE	English (Ireland)
			return KB_UK;		//	English (UK)

		case 0x0025:	// et		Estonian
			return KB_ET;		//	Estonian

		case 0x100C:	// fr-CH	French (Switzerland)
			return KB_SF;		//	French (Switzerland)

		case 0x0807:	// de-CH	German (Switzerland)
			return KB_SG;		//	German (Switzerland)

		case 0x000F:	// is		Icelandic
			return KB_IS;		//	Icelandic

		case 0x0004:	// zh-Hans	Chinese (Simplified)
		case 0x0011:	// ja		Japanese (Generic)
		case 0x0012:	// ko		Korean (Generic)
			return KB_IME;		//	CJK Input Method Editor

		case 0x0018:	// ro		Romanian
			return KB_RO;		//	Romanian

		case 0x0C1A:	// sr-Cyrl-SP	Serbian (Cyrillic, Serbia)
			return KB_YC;		//	Yougoslavian Cyrillic

		case 0x080A:	// es-MX	Spanish (Mexico)
		case 0x100A:	// es-GT	Spanish (Guatemala)
		case 0x140A:	// es-CR	Spanish (Costa Rica)
		case 0x180A:	// es-PA	Spanish (Panama)
		case 0x1C0A:	// es-DO	Spanish (Dominican Republic)
		case 0x200A:	// es-VE	Spanish (Venezuela)
		case 0x240A:	// es-CO	Spanish (Colombia)
		case 0x280A:	// es-PE	Spanish (Peru)
		case 0x2C0A:	// es-AR	Spanish (Argentina)
		case 0x300A:	// es-EC	Spanish (Ecuador)
		case 0x340A:	// es-CL	Spanish (Chile)
		case 0x380A:	// es-UY	Spanish (Uruguay)
		case 0x3C0A:	// es-PY	Spanish (Paraguay)
		case 0x400A:	// es-BO	Spanish (Bolivia)
		case 0x440A:	// es-SV	Spanish (El Salvador)
		case 0x480A:	// es-HN	Spanish (Honduras)
		case 0x4C0A:	// es-NI	Spanish (Nicaragua)
		case 0x500A:	// es-PR	Spanish (Puerto Rico)
			return KB_LA;		//	Spanish (Latin America)

		case 0x0022:	// uk		Ukrainian
			return KB_UR;		//	Ukrainian

		case 0x001B:	// sk		Slovak
			return KB_SL;		//	Slovakian

		default:
			pass++;
			if (pass > 1) {
				uprintf("Could not match a DOS keyboard ID for locale 0x%04X\n", locale);
				return KB_US;
			}
			// If we didn't get a match on first pass, mask with 0x03ff to try more generic pages
			mask = 0x03ff;
			break;
		}
	} while(1);
}

// See http://msdn.microsoft.com/en-us/library/windows/desktop/dd317756.aspx
static int get_egacpi_idx(void)
{
	switch(GetOEMCP()) {
	case 708:	// Arabic (ASMO 708)
	case 709:	// Arabic (ASMO 449+, BCON V4)
	case 710:	// Arabic (Transparent Arabic)
	case 860:	// Portuguese
	case 861:	// Icelandic
	case 863:	// French Canadian
	case 864:	// Arabic
	case 865:	// Nordic
	case 874:	// Thai
	case 932:	// Japanese
	case 936:	// Simplified Chinese
	case 949:	// Korean
	case 950:	// Traditional Chinese
	case 1258:	// Vietnamese
		return 0;	// In the original DLL, MS returns 99 here and gets
					// overflowed string indexes as a result - WTF?
	case 437:	// United States
	case 850:	// Latin 1
	case 852:	// Latin 2
		return 0;
	case 737:	// The Greek Formerly Known As 437G
	case 857:	// Turkish
	case 869:	// Modern Greek
		return 1;
	case 775:	// Baltic
	case 855:	// Cyrillic
	case 866:	// Russian
		return 2;
	case 720:	// Arabic (Transparent ASMO)
		return 3;
	case 862:	// Hebrew
		return 4;
	default:
		return 0;
	}
}

static BOOL is_cjkus(void)
{
	switch(GetOEMCP()) {
	case 437:	// United States
	case 932:	// Japanese
	case 936:	// Simplified Chinese
	case 949:	// Korean
	case 950:	// Traditional Chinese
		return TRUE;
	default:
		return FALSE;
	}
}

BOOL SetMSDOSLocale(const char* path)
{
	char filename[MAX_PATH];
	int kb_id;
	int drv_id;
	int egacpi_id;
	unsigned int oem_cp;
	FILE* fd;

	// Microsoft doesn't actually set a locale if the CP is CJK or US
	if (is_cjkus())
		return TRUE;

	kb_id = syslocale_to_kbid((WORD)(LONG_PTR)GetKeyboardLayout(0));
	uprintf("Using DOS keyboard '%s'\n", dos_kb_str[kb_id]);
	drv_id = kb_to_drv[kb_id];
	if (drv_id < 0) {
		kb_id = syslocale_to_kbid((WORD)GetSystemDefaultLangID());
	}
	egacpi_id = get_egacpi_idx();
	oem_cp = GetOEMCP();

	strcpy(filename, path);
	safe_strcat(filename, sizeof(filename), "\\CONFIG.SYS");
	fd = fopen(filename, "w+");
	if (fd == NULL) {
		uprintf("Unable to create 'CONFIG.SYS': %s.\n", WindowsErrorString());
		return FALSE;
	}
	// TODO: address cases where selection is between US and US
	fprintf(fd, "[MENU]\n");
	fprintf(fd, "MENUITEM NON_US, Use %s keyboard locale (default)\n", dos_kb_human_readable_str[kb_id]);
	fprintf(fd, "MENUITEM US, Use US keyboard locale\n");
	fprintf(fd, "MENUDEFAULT 1, 5\n");
	fprintf(fd, "[NON_US]\n");
	fprintf(fd, "device=C:\\locale\\display.sys con=(%s)\n", dos_con_str[egacpi_id]);
	fprintf(fd, "[US]\n");
	fclose(fd);
	uprintf("Succesfully wrote 'CONFIG.SYS'\n");

	strcpy(filename, path);
	safe_strcat(filename, sizeof(filename), "\\AUTOEXEC.BAT");
	fd = fopen(filename, "w+");
	if (fd == NULL) {
		uprintf("Unable to create 'AUTOEXEC.BAT': %s.\n", WindowsErrorString());
		return FALSE;
	}
	fprintf(fd, "@echo off\n");
	fprintf(fd, "set PATH=.;C:\\;C:\\LOCALE\n");
	fprintf(fd, "GOTO %%CONFIG%%\n");
	fprintf(fd, ":NON_US\n");
	fprintf(fd, "mode con codepage prepare=((%d) \\locale\\%s) > NUL\n", oem_cp, dos_cpi_str[egacpi_id]);
	fprintf(fd, "mode con codepage select=%d > NUL\n", oem_cp);
	// TODO: specify /ID: for Turkish and Hebrew
	fprintf(fd, "keyb %s,,\\locale\\%s\n", dos_kb_str[kb_id], dos_kb_drv_str[drv_id]);
	fprintf(fd, ":US\n");
	fclose(fd);
	uprintf("Succesfully wrote 'AUTOEXEC.BAT'\n");

	return TRUE;
}
 