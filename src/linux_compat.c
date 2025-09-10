/*
 * Linux Compatibility Layer Implementation for Rufus
 * Copyright © 2025 Linux Port Contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "../include/linux_compat.h"
#include <sys/time.h>
#include <sys/mount.h>
#include <sys/sysmacros.h>
#include <time.h>
#include <stdlib.h>

// Global error state
DWORD g_last_error = ERROR_SUCCESS;

// Error handling functions
DWORD GetLastError(void) {
    return g_last_error;
}

void SetLastError(DWORD dwErrCode) {
    g_last_error = dwErrCode;
}

char* WindowsErrorString(void) {
    return strerror(g_last_error);
}

// File handling replacements
HANDLE CreateFileA(PCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
                   void* lpSecurityAttributes, DWORD dwCreationDisposition,
                   DWORD dwFlagsAndAttributes, HANDLE hTemplateFile) {
    int flags = 0;
    int fd;
    
    if (dwDesiredAccess & GENERIC_READ && dwDesiredAccess & GENERIC_WRITE) {
        flags = O_RDWR;
    } else if (dwDesiredAccess & GENERIC_WRITE) {
        flags = O_WRONLY;
    } else {
        flags = O_RDONLY;
    }
    
    fd = open(lpFileName, flags);
    if (fd == -1) {
        SetLastError(errno);
        return INVALID_HANDLE_VALUE;
    }
    
    SetLastError(ERROR_SUCCESS);
    return (HANDLE)(intptr_t)fd;
}

BOOL CloseHandle(HANDLE hObject) {
    if (hObject == INVALID_HANDLE_VALUE) {
        SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
    
    int fd = (int)(intptr_t)hObject;
    if (close(fd) == -1) {
        SetLastError(errno);
        return FALSE;
    }
    
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

BOOL DeviceIoControl(HANDLE hDevice, DWORD dwIoControlCode, void* lpInBuffer,
                     DWORD nInBufferSize, void* lpOutBuffer, DWORD nOutBufferSize,
                     DWORD* lpBytesReturned, void* lpOverlapped) {
    int fd = (int)(intptr_t)hDevice;
    int ret;
    
    switch (dwIoControlCode) {
        case FSCTL_ALLOW_EXTENDED_DASD_IO:
            // This is automatically allowed on Linux
            ret = 0;
            break;
            
        case FSCTL_LOCK_VOLUME:
            ret = ioctl(fd, BLKFLSBUF, 0);
            if (ret == 0) {
                ret = ioctl(fd, BLKRRPART, 0);
            }
            break;
            
        case IOCTL_MOUNTMGR_SET_AUTO_MOUNT:
            // Linux doesn't have an exact equivalent, return success
            ret = 0;
            break;
            
        case IOCTL_MOUNTMGR_QUERY_AUTO_MOUNT:
            if (lpOutBuffer && nOutBufferSize >= sizeof(BOOL)) {
                *((BOOL*)lpOutBuffer) = TRUE; // Always enabled on Linux
                if (lpBytesReturned) {
                    *lpBytesReturned = sizeof(BOOL);
                }
                ret = 0;
            } else {
                ret = -1;
                errno = EINVAL;
            }
            break;
            
        default:
            ret = ioctl(fd, dwIoControlCode, lpInBuffer);
            break;
    }
    
    if (ret == -1) {
        SetLastError(errno);
        return FALSE;
    }
    
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

// Time functions
uint64_t GetTickCount64(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void Sleep(DWORD dwMilliseconds) {
    usleep(dwMilliseconds * 1000);
}

// Linux-specific device detection functions
BOOL linux_is_usb_device(const char* device_path) {
    char sysfs_path[512];
    char *device_name;
    FILE *fp;
    char line[256];
    BOOL is_usb = FALSE;
    
    // Extract device name from path (e.g., /dev/sdb -> sdb)
    device_name = strrchr(device_path, '/');
    if (!device_name) {
        return FALSE;
    }
    device_name++; // Skip the '/'
    
    // Remove partition numbers if present (e.g., sdb1 -> sdb)
    char clean_name[64];
    strncpy(clean_name, device_name, sizeof(clean_name) - 1);
    clean_name[sizeof(clean_name) - 1] = '\0';
    
    for (int i = strlen(clean_name) - 1; i >= 0; i--) {
        if (isdigit(clean_name[i])) {
            clean_name[i] = '\0';
        } else {
            break;
        }
    }
    
    // Check if it's on USB bus by reading uevent
    snprintf(sysfs_path, sizeof(sysfs_path), "/sys/block/%s/uevent", clean_name);
    
    fp = fopen(sysfs_path, "r");
    if (fp) {
        while (fgets(line, sizeof(line), fp)) {
            if (strstr(line, "DRIVER=usb") || strstr(line, "ID_BUS=usb")) {
                is_usb = TRUE;
                break;
            }
        }
        fclose(fp);
    }
    
    // Alternative: check if device path contains usb
    if (!is_usb) {
        snprintf(sysfs_path, sizeof(sysfs_path), "/sys/block/%s", clean_name);
        char *resolved = realpath(sysfs_path, NULL);
        if (resolved) {
            if (strstr(resolved, "/usb")) {
                is_usb = TRUE;
            }
            free(resolved);
        }
    }
    
    return is_usb;
}

BOOL linux_is_removable_device(const char* device_path) {
    char removable_path[512];
    char *device_name;
    FILE *fp;
    int removable = 0;
    
    // Extract device name from path (e.g., /dev/sdb -> sdb)
    device_name = strrchr(device_path, '/');
    if (!device_name) {
        return FALSE;
    }
    device_name++; // Skip the '/'
    
    // Remove partition numbers if present (e.g., sdb1 -> sdb)
    char clean_name[64];
    strncpy(clean_name, device_name, sizeof(clean_name) - 1);
    clean_name[sizeof(clean_name) - 1] = '\0';
    
    for (int i = strlen(clean_name) - 1; i >= 0; i--) {
        if (isdigit(clean_name[i])) {
            clean_name[i] = '\0';
        } else {
            break;
        }
    }
    
    snprintf(removable_path, sizeof(removable_path), "/sys/block/%s/removable", clean_name);
    
    fp = fopen(removable_path, "r");
    if (fp) {
        fscanf(fp, "%d", &removable);
        fclose(fp);
    }
    
    return removable != 0;
}

uint64_t linux_get_device_size(const char* device_path) {
    int fd;
    uint64_t size = 0;
    
    fd = open(device_path, O_RDONLY);
    if (fd != -1) {
        if (ioctl(fd, BLKGETSIZE64, &size) == -1) {
            size = 0;
        }
        close(fd);
    }
    
    return size;
}

BOOL linux_get_drive_info(const char* device_path, LINUX_DRIVE_INFO* drive_info) {
    if (!drive_info) {
        return FALSE;
    }
    
    memset(drive_info, 0, sizeof(LINUX_DRIVE_INFO));
    strncpy(drive_info->path, device_path, sizeof(drive_info->path) - 1);
    
    drive_info->size = linux_get_device_size(device_path);
    drive_info->is_usb = linux_is_usb_device(device_path);
    drive_info->is_removable = linux_is_removable_device(device_path);
    
    // Set generic model/vendor info (can be enhanced later)
    strncpy(drive_info->model, "Unknown", sizeof(drive_info->model) - 1);
    strncpy(drive_info->vendor, "Unknown", sizeof(drive_info->vendor) - 1);
    
    return TRUE;
}

BOOL linux_enumerate_drives(void) {
    DIR *dir;
    struct dirent *entry;
    char device_path[256];
    
    dir = opendir("/sys/block");
    if (!dir) {
        return FALSE;
    }
    
    while ((entry = readdir(dir)) != NULL) {
        // Skip . and .. entries
        if (entry->d_name[0] == '.') {
            continue;
        }
        
        // Skip loop devices, ram disks, etc.
        if (strncmp(entry->d_name, "loop", 4) == 0 ||
            strncmp(entry->d_name, "ram", 3) == 0 ||
            strncmp(entry->d_name, "dm-", 3) == 0) {
            continue;
        }
        
        snprintf(device_path, sizeof(device_path), "/dev/%s", entry->d_name);
        
        LINUX_DRIVE_INFO drive_info;
        if (linux_get_drive_info(device_path, &drive_info)) {
            printf("Found drive: %s (%s %s, %llu bytes, USB: %s, Removable: %s)\n",
                   drive_info.path, drive_info.vendor, drive_info.model,
                   (unsigned long long)drive_info.size,
                   drive_info.is_usb ? "Yes" : "No",
                   drive_info.is_removable ? "Yes" : "No");
        }
    }
    
    closedir(dir);
    return TRUE;
}

BOOL linux_create_filesystem(const char* device_path, const char* fs_type, const char* label) {
    char cmd[512];
    int ret;
    
    // Construct the appropriate mkfs command based on filesystem type
    if (strcmp(fs_type, "fat32") == 0 || strcmp(fs_type, "vfat") == 0) {
        snprintf(cmd, sizeof(cmd), "mkfs.vfat -F 32 -n \"%.11s\" \"%s\"", 
                 label ? label : "RUFUS", device_path);
    } else if (strcmp(fs_type, "ntfs") == 0) {
        snprintf(cmd, sizeof(cmd), "mkfs.ntfs -f -L \"%.32s\" \"%s\"",
                 label ? label : "RUFUS", device_path);
    } else if (strcmp(fs_type, "ext4") == 0) {
        snprintf(cmd, sizeof(cmd), "mkfs.ext4 -F -L \"%.16s\" \"%s\"",
                 label ? label : "RUFUS", device_path);
    } else if (strcmp(fs_type, "ext3") == 0) {
        snprintf(cmd, sizeof(cmd), "mkfs.ext3 -F -L \"%.16s\" \"%s\"",
                 label ? label : "RUFUS", device_path);
    } else if (strcmp(fs_type, "ext2") == 0) {
        snprintf(cmd, sizeof(cmd), "mkfs.ext2 -F -L \"%.16s\" \"%s\"",
                 label ? label : "RUFUS", device_path);
    } else {
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    
    ret = system(cmd);
    if (ret != 0) {
        SetLastError(ERROR_GEN_FAILURE);
        return FALSE;
    }
    
    return TRUE;
}

BOOL linux_write_image_to_drive(const char* device_path, const char* image_path) {
    FILE *image_fp, *device_fp;
    char buffer[64 * 1024]; // 64KB buffer
    size_t bytes_read, bytes_written;
    BOOL success = TRUE;
    
    image_fp = fopen(image_path, "rb");
    if (!image_fp) {
        SetLastError(errno);
        return FALSE;
    }
    
    device_fp = fopen(device_path, "wb");
    if (!device_fp) {
        SetLastError(errno);
        fclose(image_fp);
        return FALSE;
    }
    
    // Copy image to device
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), image_fp)) > 0) {
        bytes_written = fwrite(buffer, 1, bytes_read, device_fp);
        if (bytes_written != bytes_read) {
            SetLastError(errno);
            success = FALSE;
            break;
        }
    }
    
    fclose(device_fp);
    fclose(image_fp);
    
    // Sync to ensure all data is written
    if (success) {
        sync();
    }
    
    return success;
}

BOOL linux_mount_device(const char* device_path, const char* mount_point) {
    if (mount(device_path, mount_point, "auto", MS_RDONLY, NULL) == -1) {
        SetLastError(errno);
        return FALSE;
    }
    return TRUE;
}

BOOL linux_unmount_device(const char* device_path) {
    if (umount(device_path) == -1) {
        SetLastError(errno);
        return FALSE;
    }
    return TRUE;
}
