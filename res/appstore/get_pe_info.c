// Compile with: gcc -lversion -o get_pe_info get_pe_info.c

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    VS_FIXEDFILEINFO *file_info;
    DWORD handle, size;
    WORD lang, codepage;
    UINT len;
    void *buffer = NULL, *translation, *version_info;
    char sub_block[50];
    int ret = 1;

    if (argc != 3 || argv[1][0] != '-') {
        printf("Usage: %s [-i|-v] <path_to_executable>\n", argv[0]);
        goto out;
    }

    size = GetFileVersionInfoSizeA(argv[2], &handle);
    if (size == 0) {
        fprintf(stderr, "Failed to get version info size.\n");
        goto out;
    }

    buffer = malloc(size);
    if (buffer == NULL)
        goto out;

    if (!GetFileVersionInfoA(argv[2], handle, size, buffer)) {
        fprintf(stderr, "Failed to get version info.\n");
        goto out;
    }

    if (argv[1][1] == 'i') {
        if (!VerQueryValueA(buffer, "\\VarFileInfo\\Translation", &translation, &len) || len < 4) {
            fprintf(stderr, "Failed to retrieve language and codepage information.\n");
            goto out;
        }
        lang = *(WORD*)translation;
        codepage = *((WORD*)translation + 1);
        snprintf(sub_block, sizeof(sub_block), "\\StringFileInfo\\%04x%04x\\InternalName", lang, codepage);
        if (!VerQueryValueA(buffer, sub_block, &version_info, &len)) {
            fprintf(stderr, "Failed to retrieve Internal Name.\n");
            goto out;
        }
        printf("%s\n", (char*)version_info);
    } else {
        if (!VerQueryValueA(buffer, "\\", (LPVOID*)&file_info, &len) || len < sizeof(VS_FIXEDFILEINFO)) {
            fprintf(stderr, "Failed to retrieve file info.\n");
            goto out;
        }
        printf("%d.%d.%d.%d\n", HIWORD(file_info->dwFileVersionMS), LOWORD(file_info->dwFileVersionMS),
               HIWORD(file_info->dwFileVersionLS), LOWORD(file_info->dwFileVersionLS));
    }
    ret = 0;

out:
    free(buffer);
    return ret;
}
