/*
 * Rufus Linux Port - GTK GUI Interface
 * Copyright © 2025 Linux Port Contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "../include/linux_compat.h"
#include <gtk/gtk.h>
#include <pthread.h>
#include <string.h>
#include <stdio.h>

// GUI structures
typedef struct {
    GtkWidget *window;
    GtkWidget *device_combo;
    GtkWidget *filesystem_combo;
    GtkWidget *label_entry;
    GtkWidget *image_entry;
    GtkWidget *image_button;
    GtkWidget *start_button;
    GtkWidget *close_button;
    GtkWidget *progress_bar;
    GtkWidget *status_label;
    GtkWidget *log_textview;
    GtkListStore *device_store;
    
    // Threading for operations
    pthread_t operation_thread;
    gboolean operation_running;
    
    // Operation parameters
    char selected_device[256];
    char selected_filesystem[64];
    char volume_label[256];
    char image_path[512];
    int operation_type; // 0=format, 1=write_image
} RufusGUI;

static RufusGUI *gui_instance = NULL;

// Function prototypes
static void on_start_clicked(GtkWidget *widget, gpointer data);
static void on_close_clicked(GtkWidget *widget, gpointer data);
static void on_select_image_clicked(GtkWidget *widget, gpointer data);
static void refresh_drives(RufusGUI *gui);
static void update_status(const char *message);
static void update_progress(double percentage);
static void log_message(const char *message);
static void* operation_thread_func(void *data);
static gboolean update_gui_from_thread(gpointer data);

// Status update structure for thread communication
typedef struct {
    char message[512];
    double progress;
    int type; // 0=status, 1=progress, 2=log, 3=complete
} StatusUpdate;

// Thread-safe GUI updates
static void update_status(const char *message) {
    StatusUpdate *update = g_malloc(sizeof(StatusUpdate));
    strncpy(update->message, message, sizeof(update->message) - 1);
    update->message[sizeof(update->message) - 1] = '\0';
    update->type = 0;
    g_idle_add(update_gui_from_thread, update);
}

static void update_progress(double percentage) {
    StatusUpdate *update = g_malloc(sizeof(StatusUpdate));
    update->progress = percentage / 100.0;
    update->type = 1;
    g_idle_add(update_gui_from_thread, update);
}

static void log_message(const char *message) {
    StatusUpdate *update = g_malloc(sizeof(StatusUpdate));
    strncpy(update->message, message, sizeof(update->message) - 1);
    update->message[sizeof(update->message) - 1] = '\0';
    update->type = 2;
    g_idle_add(update_gui_from_thread, update);
}

static void operation_complete() {
    StatusUpdate *update = g_malloc(sizeof(StatusUpdate));
    update->type = 3;
    g_idle_add(update_gui_from_thread, update);
}

static gboolean update_gui_from_thread(gpointer data) {
    StatusUpdate *update = (StatusUpdate*)data;
    
    switch (update->type) {
        case 0: // Status
            gtk_label_set_text(GTK_LABEL(gui_instance->status_label), update->message);
            break;
        case 1: // Progress
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(gui_instance->progress_bar), update->progress);
            break;
        case 2: // Log
            {
                GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(gui_instance->log_textview));
                GtkTextIter end;
                gtk_text_buffer_get_end_iter(buffer, &end);
                
                char log_line[600];
                snprintf(log_line, sizeof(log_line), "%s\n", update->message);
                gtk_text_buffer_insert(buffer, &end, log_line, -1);
                
                // Auto-scroll to bottom
                GtkTextMark *mark = gtk_text_buffer_get_insert(buffer);
                gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(gui_instance->log_textview), mark);
            }
            break;
        case 3: // Complete
            gui_instance->operation_running = FALSE;
            gtk_widget_set_sensitive(gui_instance->start_button, TRUE);
            gtk_widget_set_sensitive(gui_instance->device_combo, TRUE);
            gtk_widget_set_sensitive(gui_instance->filesystem_combo, TRUE);
            gtk_widget_set_sensitive(gui_instance->image_button, TRUE);
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(gui_instance->progress_bar), 1.0);
            break;
    }
    
    g_free(update);
    return G_SOURCE_REMOVE;
}

static void* operation_thread_func(void *data) {
    RufusGUI *gui = (RufusGUI*)data;
    LINUX_DRIVE_INFO drive_info;
    
    log_message("Starting operation...");
    
    // Get device information
    if (!linux_get_drive_info(gui->selected_device, &drive_info)) {
        update_status("Error: Cannot access device");
        log_message("ERROR: Failed to get device information");
        operation_complete();
        return NULL;
    }
    
    log_message("Device information:");
    char info_msg[256];
    snprintf(info_msg, sizeof(info_msg), "  Device: %s", drive_info.path);
    log_message(info_msg);
    snprintf(info_msg, sizeof(info_msg), "  Size: %.2f GB", drive_info.size / (1024.0 * 1024.0 * 1024.0));
    log_message(info_msg);
    snprintf(info_msg, sizeof(info_msg), "  Removable: %s", drive_info.is_removable ? "Yes" : "No");
    log_message(info_msg);
    
    // Safety check
    if (!drive_info.is_removable) {
        update_status("Error: Device is not removable");
        log_message("ERROR: Device is not marked as removable - operation aborted for safety");
        operation_complete();
        return NULL;
    }
    
    update_progress(10);
    
    if (gui->operation_type == 0) {
        // Format operation
        update_status("Formatting device...");
        log_message("Starting format operation");
        
        snprintf(info_msg, sizeof(info_msg), "Filesystem: %s", gui->selected_filesystem);
        log_message(info_msg);
        snprintf(info_msg, sizeof(info_msg), "Label: %s", gui->volume_label[0] ? gui->volume_label : "(none)");
        log_message(info_msg);
        
        update_progress(30);
        
        if (linux_create_filesystem(gui->selected_device, gui->selected_filesystem, 
                                   gui->volume_label[0] ? gui->volume_label : NULL)) {
            update_progress(100);
            update_status("Format completed successfully!");
            log_message("SUCCESS: Format operation completed");
        } else {
            update_status("Format failed!");
            log_message("ERROR: Format operation failed");
        }
    } else {
        // Image write operation
        update_status("Writing image to device...");
        log_message("Starting image write operation");
        
        snprintf(info_msg, sizeof(info_msg), "Image: %s", gui->image_path);
        log_message(info_msg);
        
        update_progress(30);
        
        if (linux_write_image_to_drive(gui->selected_device, gui->image_path)) {
            update_progress(100);
            update_status("Image written successfully!");
            log_message("SUCCESS: Image write operation completed");
        } else {
            update_status("Image write failed!");
            log_message("ERROR: Image write operation failed");
        }
    }
    
    operation_complete();
    return NULL;
}

static void on_start_clicked(GtkWidget *widget, gpointer data) {
    RufusGUI *gui = (RufusGUI*)data;
    
    if (gui->operation_running) {
        return;
    }
    
    // Get selected device
    GtkTreeIter iter;
    if (!gtk_combo_box_get_active_iter(GTK_COMBO_BOX(gui->device_combo), &iter)) {
        update_status("Please select a device");
        return;
    }
    
    GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(gui->device_combo));
    gchar *device_path;
    gtk_tree_model_get(model, &iter, 1, &device_path, -1);
    strncpy(gui->selected_device, device_path, sizeof(gui->selected_device) - 1);
    g_free(device_path);
    
    // Get selected filesystem
    const gchar *filesystem = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(gui->filesystem_combo));
    if (filesystem) {
        strncpy(gui->selected_filesystem, filesystem, sizeof(gui->selected_filesystem) - 1);
    }
    
    // Get volume label
    const gchar *label = gtk_entry_get_text(GTK_ENTRY(gui->label_entry));
    strncpy(gui->volume_label, label ? label : "", sizeof(gui->volume_label) - 1);
    
    // Get image path
    const gchar *image = gtk_entry_get_text(GTK_ENTRY(gui->image_entry));
    strncpy(gui->image_path, image ? image : "", sizeof(gui->image_path) - 1);
    
    // Determine operation type
    if (gui->image_path[0] != '\0') {
        gui->operation_type = 1; // Image write
    } else {
        gui->operation_type = 0; // Format
    }
    
    // Confirm operation
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(gui->window),
                                              GTK_DIALOG_MODAL,
                                              GTK_MESSAGE_WARNING,
                                              GTK_BUTTONS_YES_NO,
                                              "WARNING: This will %s ALL data on %s!\n\nAre you sure you want to continue?",
                                              gui->operation_type == 1 ? "overwrite" : "erase",
                                              gui->selected_device);
    
    gtk_window_set_title(GTK_WINDOW(dialog), "Confirm Operation");
    int response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    
    if (response != GTK_RESPONSE_YES) {
        return;
    }
    
    // Start operation
    gui->operation_running = TRUE;
    gtk_widget_set_sensitive(gui->start_button, FALSE);
    gtk_widget_set_sensitive(gui->device_combo, FALSE);
    gtk_widget_set_sensitive(gui->filesystem_combo, FALSE);
    gtk_widget_set_sensitive(gui->image_button, FALSE);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(gui->progress_bar), 0.0);
    
    // Clear log
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(gui->log_textview));
    gtk_text_buffer_set_text(buffer, "", -1);
    
    // Start operation thread
    pthread_create(&gui->operation_thread, NULL, operation_thread_func, gui);
}

static void on_close_clicked(GtkWidget *widget, gpointer data) {
    RufusGUI *gui = (RufusGUI*)data;
    
    if (gui->operation_running) {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(gui->window),
                                                  GTK_DIALOG_MODAL,
                                                  GTK_MESSAGE_WARNING,
                                                  GTK_BUTTONS_YES_NO,
                                                  "An operation is currently running.\n\nAre you sure you want to exit?");
        
        gtk_window_set_title(GTK_WINDOW(dialog), "Operation Running");
        int response = gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        
        if (response != GTK_RESPONSE_YES) {
            return;
        }
        
        // Cancel operation (note: this is not clean cancellation)
        gui->operation_running = FALSE;
    }
    
    gtk_main_quit();
}

static void on_select_image_clicked(GtkWidget *widget, gpointer data) {
    RufusGUI *gui = (RufusGUI*)data;
    
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Select ISO Image",
                                                   GTK_WINDOW(gui->window),
                                                   GTK_FILE_CHOOSER_ACTION_OPEN,
                                                   "_Cancel", GTK_RESPONSE_CANCEL,
                                                   "_Open", GTK_RESPONSE_ACCEPT,
                                                   NULL);
    
    // Add file filters
    GtkFileFilter *filter_iso = gtk_file_filter_new();
    gtk_file_filter_set_name(filter_iso, "ISO Images (*.iso)");
    gtk_file_filter_add_pattern(filter_iso, "*.iso");
    gtk_file_filter_add_pattern(filter_iso, "*.ISO");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter_iso);
    
    GtkFileFilter *filter_img = gtk_file_filter_new();
    gtk_file_filter_set_name(filter_img, "Disk Images (*.img)");
    gtk_file_filter_add_pattern(filter_img, "*.img");
    gtk_file_filter_add_pattern(filter_img, "*.IMG");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter_img);
    
    GtkFileFilter *filter_all = gtk_file_filter_new();
    gtk_file_filter_set_name(filter_all, "All Files");
    gtk_file_filter_add_pattern(filter_all, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter_all);
    
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        gtk_entry_set_text(GTK_ENTRY(gui->image_entry), filename);
        g_free(filename);
    }
    
    gtk_widget_destroy(dialog);
}

static void refresh_drives(RufusGUI *gui) {
    // Clear existing entries
    gtk_list_store_clear(gui->device_store);
    
    // Enumerate drives using our Linux compatibility layer
    DIR *dir = opendir("/sys/block");
    if (!dir) {
        return;
    }
    
    struct dirent *entry;
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
        
        char device_path[256];
        snprintf(device_path, sizeof(device_path), "/dev/%s", entry->d_name);
        
        LINUX_DRIVE_INFO drive_info;
        if (linux_get_drive_info(device_path, &drive_info)) {
            char display_text[512];
            snprintf(display_text, sizeof(display_text), 
                     "%s - %.2f GB %s%s",
                     drive_info.path,
                     drive_info.size / (1024.0 * 1024.0 * 1024.0),
                     drive_info.is_removable ? "[Removable]" : "[Fixed]",
                     drive_info.is_usb ? " [USB]" : "");
            
            GtkTreeIter iter;
            gtk_list_store_append(gui->device_store, &iter);
            gtk_list_store_set(gui->device_store, &iter,
                              0, display_text,
                              1, drive_info.path,
                              -1);
        }
    }
    
    closedir(dir);
}

static void create_gui(RufusGUI *gui) {
    // Main window
    gui->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(gui->window), "Rufus - Linux Port");
    gtk_window_set_default_size(GTK_WINDOW(gui->window), 500, 600);
    gtk_window_set_resizable(GTK_WINDOW(gui->window), FALSE);
    gtk_container_set_border_width(GTK_CONTAINER(gui->window), 10);
    
    // Main vertical box
    GtkWidget *main_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_add(GTK_CONTAINER(gui->window), main_vbox);
    
    // Device selection
    GtkWidget *device_frame = gtk_frame_new("Device");
    gtk_box_pack_start(GTK_BOX(main_vbox), device_frame, FALSE, FALSE, 0);
    
    GtkWidget *device_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(device_vbox), 10);
    gtk_container_add(GTK_CONTAINER(device_frame), device_vbox);
    
    // Device combo box
    gui->device_store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
    gui->device_combo = gtk_combo_box_new_with_model(GTK_TREE_MODEL(gui->device_store));
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(gui->device_combo), renderer, TRUE);
    gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(gui->device_combo), renderer, "text", 0, NULL);
    gtk_box_pack_start(GTK_BOX(device_vbox), gui->device_combo, FALSE, FALSE, 0);
    
    // Refresh button
    GtkWidget *refresh_button = gtk_button_new_with_label("Refresh");
    g_signal_connect_swapped(refresh_button, "clicked", G_CALLBACK(refresh_drives), gui);
    gtk_box_pack_start(GTK_BOX(device_vbox), refresh_button, FALSE, FALSE, 0);
    
    // Boot selection and format options
    GtkWidget *format_frame = gtk_frame_new("Format Options");
    gtk_box_pack_start(GTK_BOX(main_vbox), format_frame, FALSE, FALSE, 0);
    
    GtkWidget *format_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(format_grid), 5);
    gtk_grid_set_column_spacing(GTK_GRID(format_grid), 10);
    gtk_container_set_border_width(GTK_CONTAINER(format_grid), 10);
    gtk_container_add(GTK_CONTAINER(format_frame), format_grid);
    
    // File system
    GtkWidget *fs_label = gtk_label_new("File System:");
    gtk_widget_set_halign(fs_label, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(format_grid), fs_label, 0, 0, 1, 1);
    
    gui->filesystem_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(gui->filesystem_combo), "fat32");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(gui->filesystem_combo), "ntfs");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(gui->filesystem_combo), "ext4");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(gui->filesystem_combo), "ext3");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(gui->filesystem_combo), "ext2");
    gtk_combo_box_set_active(GTK_COMBO_BOX(gui->filesystem_combo), 0);
    gtk_grid_attach(GTK_GRID(format_grid), gui->filesystem_combo, 1, 0, 1, 1);
    
    // Volume label
    GtkWidget *label_label = gtk_label_new("Volume Label:");
    gtk_widget_set_halign(label_label, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(format_grid), label_label, 0, 1, 1, 1);
    
    gui->label_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(gui->label_entry), "Enter volume label (optional)");
    gtk_grid_attach(GTK_GRID(format_grid), gui->label_entry, 1, 1, 1, 1);
    
    // Image selection
    GtkWidget *image_frame = gtk_frame_new("Image Option");
    gtk_box_pack_start(GTK_BOX(main_vbox), image_frame, FALSE, FALSE, 0);
    
    GtkWidget *image_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(image_hbox), 10);
    gtk_container_add(GTK_CONTAINER(image_frame), image_hbox);
    
    gui->image_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(gui->image_entry), "Select an ISO or image file (optional)");
    gtk_box_pack_start(GTK_BOX(image_hbox), gui->image_entry, TRUE, TRUE, 0);
    
    gui->image_button = gtk_button_new_with_label("Browse");
    g_signal_connect(gui->image_button, "clicked", G_CALLBACK(on_select_image_clicked), gui);
    gtk_box_pack_start(GTK_BOX(image_hbox), gui->image_button, FALSE, FALSE, 0);
    
    // Status and progress
    GtkWidget *status_frame = gtk_frame_new("Status");
    gtk_box_pack_start(GTK_BOX(main_vbox), status_frame, TRUE, TRUE, 0);
    
    GtkWidget *status_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(status_vbox), 10);
    gtk_container_add(GTK_CONTAINER(status_frame), status_vbox);
    
    gui->status_label = gtk_label_new("Ready");
    gtk_widget_set_halign(gui->status_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(status_vbox), gui->status_label, FALSE, FALSE, 0);
    
    gui->progress_bar = gtk_progress_bar_new();
    gtk_box_pack_start(GTK_BOX(status_vbox), gui->progress_bar, FALSE, FALSE, 0);
    
    // Log window
    GtkWidget *log_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(log_scroll),
                                  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(log_scroll, -1, 150);
    gtk_box_pack_start(GTK_BOX(status_vbox), log_scroll, TRUE, TRUE, 0);
    
    gui->log_textview = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(gui->log_textview), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(gui->log_textview), FALSE);
    gtk_container_add(GTK_CONTAINER(log_scroll), gui->log_textview);
    
    // Action buttons
    GtkWidget *button_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(main_vbox), button_hbox, FALSE, FALSE, 0);
    
    // Start button (styled like the original Rufus)
    gui->start_button = gtk_button_new_with_label("START");
    gtk_widget_set_size_request(gui->start_button, 100, 40);
    g_signal_connect(gui->start_button, "clicked", G_CALLBACK(on_start_clicked), gui);
    gtk_box_pack_start(GTK_BOX(button_hbox), gui->start_button, FALSE, FALSE, 0);
    
    // Spacer
    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(button_hbox), spacer, TRUE, TRUE, 0);
    
    // Close button
    gui->close_button = gtk_button_new_with_label("CLOSE");
    gtk_widget_set_size_request(gui->close_button, 100, 40);
    g_signal_connect(gui->close_button, "clicked", G_CALLBACK(on_close_clicked), gui);
    gtk_box_pack_start(GTK_BOX(button_hbox), gui->close_button, FALSE, FALSE, 0);
    
    // Connect window close signal
    g_signal_connect(gui->window, "destroy", G_CALLBACK(on_close_clicked), gui);
}

int main(int argc, char *argv[]) {
    // Initialize GTK
    gtk_init(&argc, &argv);
    
    // Check for root privileges
    if (geteuid() != 0) {
        GtkWidget *dialog = gtk_message_dialog_new(NULL,
                                                  GTK_DIALOG_MODAL,
                                                  GTK_MESSAGE_WARNING,
                                                  GTK_BUTTONS_OK,
                                                  "Administrative privileges required!\n\n"
                                                  "This application needs to be run as root to access block devices.\n"
                                                  "Please run: sudo %s", argv[0]);
        gtk_window_set_title(GTK_WINDOW(dialog), "Permission Required");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return 1;
    }
    
    // Create GUI instance
    RufusGUI gui;
    memset(&gui, 0, sizeof(gui));
    gui_instance = &gui;
    
    // Create and show GUI
    create_gui(&gui);
    refresh_drives(&gui);
    
    gtk_widget_show_all(gui.window);
    
    // Run GTK main loop
    gtk_main();
    
    return 0;
}
