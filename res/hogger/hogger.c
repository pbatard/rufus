/*
 * Rufus: The Reliable USB Formatting Utility
 * Commandline hogger, C version
 * Copyright © 2014 Pete Batard <pete@akeo.ie>
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

const char error_msg[] = "Unable to synchronize with UI application.";

int __cdecl main(int argc_ansi, char** argv_ansi)
{
	DWORD size;
	register HANDLE mutex, stdout;
	stdout = GetStdHandle(STD_OUTPUT_HANDLE);
	mutex = OpenMutexA(SYNCHRONIZE, FALSE, "Global/Rufus_CmdLine");
	if (mutex == NULL)
		goto error;
	WaitForSingleObject(mutex, INFINITE);
	goto out;

error:
	WriteFile(stdout, error_msg, sizeof(error_msg), &size, 0);

out:
	ExitProcess(0);
}
