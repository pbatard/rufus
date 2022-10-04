/*
 * Rufus: The Reliable USB Formatting Utility
 * Windows User Experience
 * Copyright © 2022 Pete Batard <pete@akeo.ie>
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

#include <windows.h>

#pragma once

extern int unattend_xml_flags, unattend_xml_mask;
extern int wintogo_index, wininst_index;
extern char* unattend_xml_path;

char* CreateUnattendXml(int arch, int flags);
BOOL ApplyWindowsCustomization(char drive_letter, int flags);
int SetWinToGoIndex(void);
BOOL SetupWinPE(char drive_letter);
BOOL SetupWinToGo(DWORD DriveIndex, const char* drive_name, BOOL use_esp);
BOOL PopulateWindowsVersion(void);
