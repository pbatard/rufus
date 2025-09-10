/*
 * Rufus Linux Port - Main Entry Point
 * Copyright © 2025 Linux Port Contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "../include/linux_compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>

#define VERSION "4.10-linux-beta"

// Function prototypes
void print_version(void);
void print_help(void);
int format_device(const char* device, const char* filesystem, const char* label);
int write_image(const char* device, const char* image);
int list_drives(void);

void print_version(void) {
    printf("Rufus Linux Port %s\n", VERSION);
    printf("Copyright © 2025 Linux Port Contributors\n");
    printf("This is free software under the GPLv3+ license.\n\n");
}

void print_help(void) {
    print_version();
    printf("Usage: rufus-linux [OPTIONS]\n\n");
    printf("Options:\n");
    printf("  -h, --help               Show this help message\n");
    printf("  -v, --version            Show version information\n");
    printf("  -l, --list               List available drives\n");
    printf("  -d, --device DEVICE      Target device (e.g., /dev/sdb)\n");
    printf("  -f, --filesystem FS      Filesystem (fat32, ntfs, ext4, ext3, ext2)\n");
    printf("  -L, --label LABEL        Volume label\n");
    printf("  -i, --image IMAGE        Write image file to device\n");
    printf("  -y, --yes                Skip confirmation prompts (dangerous!)\n");
    printf("  --test                   Test mode - enumerate drives only\n\n");
    printf("Examples:\n");
    printf("  rufus-linux -l                                  # List drives\n");
    printf("  rufus-linux -d /dev/sdb -f fat32 -L \"MYUSB\"     # Format as FAT32\n");
    printf("  rufus-linux -d /dev/sdb -i ubuntu.iso          # Write ISO image\n\n");
    printf("WARNING: This tool can permanently erase data. Use with extreme caution!\n");
}

int list_drives(void) {
    printf("Scanning for available drives...\n\n");
    if (!linux_enumerate_drives()) {
        fprintf(stderr, "Error: Failed to enumerate drives\n");
        return 1;
    }
    return 0;
}

int format_device(const char* device, const char* filesystem, const char* label) {
    LINUX_DRIVE_INFO drive_info;
    
    printf("Formatting device: %s\n", device);
    printf("Filesystem: %s\n", filesystem);
    printf("Label: %s\n", label ? label : "(none)");
    
    // Check if device exists and get info
    if (!linux_get_drive_info(device, &drive_info)) {
        fprintf(stderr, "Error: Cannot access device %s\n", device);
        return 1;
    }
    
    printf("Device info:\n");
    printf("  Size: %llu bytes (%.2f GB)\n", 
           (unsigned long long)drive_info.size,
           drive_info.size / (1024.0 * 1024.0 * 1024.0));
    printf("  USB: %s\n", drive_info.is_usb ? "Yes" : "No");
    printf("  Removable: %s\n", drive_info.is_removable ? "Yes" : "No");
    
    if (!drive_info.is_removable) {
        fprintf(stderr, "WARNING: Device is not marked as removable!\n");
        fprintf(stderr, "This might be a system drive. Aborting for safety.\n");
        return 1;
    }
    
    // Perform the format
    printf("\nFormatting...\n");
    if (!linux_create_filesystem(device, filesystem, label)) {
        fprintf(stderr, "Error: Failed to format device: %s\n", WindowsErrorString());
        return 1;
    }
    
    printf("Format completed successfully!\n");
    return 0;
}

int write_image(const char* device, const char* image) {
    LINUX_DRIVE_INFO drive_info;
    
    printf("Writing image: %s\n", image);
    printf("To device: %s\n", device);
    
    // Check if device exists and get info
    if (!linux_get_drive_info(device, &drive_info)) {
        fprintf(stderr, "Error: Cannot access device %s\n", device);
        return 1;
    }
    
    printf("Device info:\n");
    printf("  Size: %llu bytes (%.2f GB)\n", 
           (unsigned long long)drive_info.size,
           drive_info.size / (1024.0 * 1024.0 * 1024.0));
    printf("  USB: %s\n", drive_info.is_usb ? "Yes" : "No");
    printf("  Removable: %s\n", drive_info.is_removable ? "Yes" : "No");
    
    if (!drive_info.is_removable) {
        fprintf(stderr, "WARNING: Device is not marked as removable!\n");
        fprintf(stderr, "This might be a system drive. Aborting for safety.\n");
        return 1;
    }
    
    // Write the image
    printf("\nWriting image...\n");
    if (!linux_write_image_to_drive(device, image)) {
        fprintf(stderr, "Error: Failed to write image: %s\n", WindowsErrorString());
        return 1;
    }
    
    printf("Image written successfully!\n");
    return 0;
}

int main(int argc, char* argv[]) {
    int opt, option_index = 0;
    char *device = NULL, *filesystem = NULL, *label = NULL, *image = NULL;
    int list_flag = 0, yes_flag = 0, test_flag = 0;
    
    static struct option long_options[] = {
        {"help",       no_argument,       0, 'h'},
        {"version",    no_argument,       0, 'v'},
        {"list",       no_argument,       0, 'l'},
        {"device",     required_argument, 0, 'd'},
        {"filesystem", required_argument, 0, 'f'},
        {"label",      required_argument, 0, 'L'},
        {"image",      required_argument, 0, 'i'},
        {"yes",        no_argument,       0, 'y'},
        {"test",       no_argument,       0, 1000},
        {0, 0, 0, 0}
    };
    
    // Check if running as root for device operations
    if (geteuid() != 0) {
        printf("Note: Running as non-root user. Some operations may require sudo.\n\n");
    }
    
    while ((opt = getopt_long(argc, argv, "hvld:f:L:i:y", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'h':
                print_help();
                return 0;
            case 'v':
                print_version();
                return 0;
            case 'l':
                list_flag = 1;
                break;
            case 'd':
                device = optarg;
                break;
            case 'f':
                filesystem = optarg;
                break;
            case 'L':
                label = optarg;
                break;
            case 'i':
                image = optarg;
                break;
            case 'y':
                yes_flag = 1;
                break;
            case 1000: // --test
                test_flag = 1;
                break;
            case '?':
                fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
                return 1;
            default:
                abort();
        }
    }
    
    // Handle test mode
    if (test_flag) {
        printf("Rufus Linux Port - Test Mode\n");
        return list_drives();
    }
    
    // Handle list drives
    if (list_flag) {
        return list_drives();
    }
    
    // Handle image writing
    if (image && device) {
        if (!yes_flag) {
            printf("WARNING: This will overwrite ALL data on %s!\n", device);
            printf("Type 'YES' to continue: ");
            char confirm[10];
            if (fgets(confirm, sizeof(confirm), stdin) == NULL || 
                strncmp(confirm, "YES", 3) != 0) {
                printf("Aborted.\n");
                return 1;
            }
        }
        return write_image(device, image);
    }
    
    // Handle formatting
    if (device && filesystem) {
        if (!yes_flag) {
            printf("WARNING: This will format and erase ALL data on %s!\n", device);
            printf("Type 'YES' to continue: ");
            char confirm[10];
            if (fgets(confirm, sizeof(confirm), stdin) == NULL || 
                strncmp(confirm, "YES", 3) != 0) {
                printf("Aborted.\n");
                return 1;
            }
        }
        return format_device(device, filesystem, label);
    }
    
    // If no specific action, show help
    if (argc == 1) {
        print_help();
        return 0;
    }
    
    fprintf(stderr, "Error: Invalid arguments. Try '%s --help' for more information.\n", argv[0]);
    return 1;
}
