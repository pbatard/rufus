 ; Rufus: The Reliable USB Formatting Utility
 ; Commandline hogger, assembly version (NASM)
 ; Copyright © 2014 Pete Batard <pete@akeo.ie>
 ;
 ; This program is free software: you can redistribute it and/or modify
 ; it under the terms of the GNU General Public License as published by
 ; the Free Software Foundation, either version 3 of the License, or
 ; (at your option) any later version.
 ;
 ; This program is distributed in the hope that it will be useful,
 ; but WITHOUT ANY WARRANTY; without even the implied warranty of
 ; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 ; GNU General Public License for more details.
 ;
 ; You should have received a copy of the GNU General Public License
 ; along with this program.  If not, see <http://www.gnu.org/licenses/>.

	global _main
	extern _GetStdHandle@4
	extern _OpenMutexA@12
	extern _WaitForSingleObject@8
	extern _WriteFile@20
	extern _ExitProcess@4

	section .text
_main:
	; DWORD size;
	mov     ebp, esp
	sub     esp, 4

	; register HANDLE mutex [-> ecx], stdout [-> ebx];

	; stdout = GetStdHandle(STD_OUTPUT_HANDLE);
	push    -11
	call    _GetStdHandle@4
	mov     ebx, eax

	; mutex = OpenMutexA(SYNCHRONIZE, FALSE, "Global/Rufus_CmdLine");
	push    mutex_name
	push    0
	push    1048576 ; 0x00100000
	call    _OpenMutexA@12
	mov     ecx, eax

	; if (mutex == NULL)
	test    eax, eax
	
	; goto error
	je      error
	
	; WaitForSingleObject(mutex, INFINITE);
	push    -1
	push    ecx
	call    _WaitForSingleObject@8
	
	; goto out;
	jmp     out;

	; error:
error:

	;	WriteFile(stdout, error_msg, sizeof(error_msg), &size, 0);
	push    0
	lea     eax, [ebp-4]
	push    eax
	push    (error_msg_end - error_msg)
	push    error_msg
	push    ebx
	call    _WriteFile@20

	; out:
out:

	; ExitProcess(0)
	push    0
	call    _ExitProcess@4

	; Just in case...
	hlt

mutex_name:
	db "Global/Rufus_CmdLine",0
error_msg:
	db "Unable to synchronize with GUI application.",0
error_msg_end: