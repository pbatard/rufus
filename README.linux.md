# Rufus Linux Port

This branch contains a Linux port of the popular Rufus USB formatting utility, featuring both graphical (GTK) and command-line interfaces.

## About This Port

This Linux port is a community-driven effort to bring the excellent Rufus experience to Linux users. The port maintains the same philosophy, interface design, and safety features as the original Windows version while integrating properly with Linux systems.

## Key Features

- **🖥️ GTK GUI**: Modern interface closely matching the original Windows Rufus
- **💻 CLI Interface**: Full command-line support for automation and scripting
- **🛡️ Safety First**: Multiple protection layers prevent accidental data loss
- **🐧 Linux Native**: Uses standard Linux tools and follows best practices
- **📦 Cross-Distribution**: Works on Ubuntu, Fedora, Arch, and more

## Installation

### Dependencies

#### Ubuntu/Debian
```bash
sudo apt-get install build-essential pkg-config libgtk-3-dev dosfstools ntfs-3g e2fsprogs
```

#### Fedora/Red Hat
```bash
sudo dnf install gcc pkg-config gtk3-devel dosfstools ntfs-3g e2fsprogs
```

#### Arch Linux
```bash
sudo pacman -S gcc pkg-config gtk3 dosfstools ntfs-3g e2fsprogs
```

### Building

```bash
# Clone the repository
git clone https://github.com/JacobEsanders/rufus-linux.git
cd rufus-linux
git checkout linux-port

# Build
make

# Launch GUI (requires root for device access)
sudo ./rufus-linux-gui

# Or use CLI
sudo ./rufus-linux --list
sudo ./rufus-linux --device /dev/sdX --filesystem fat32 --label "MYUSB"
```

## How to Use

### Step 1: Connect Your USB Drive
Insert your USB drive into an available USB port on your computer.

### Step 2: Launch Rufus Linux
Start the application using one of the methods in the Installation section.

### Step 3: Choose Your Options

#### Using the GUI:
1. **Select your USB device** from the dropdown menu (carefully verify it's the correct one!)
2. **Choose a filesystem format** (FAT32 is most compatible)
3. **Enter a volume label** (optional)
4. **Select an ISO file** if you want to create a bootable drive
5. **Click 'START'** to begin the process

#### Using the CLI:
```bash
# Format example:
sudo ./rufus-linux --device /dev/sdX --filesystem fat32 --label "RUFUS_USB"

# Create bootable USB example:
sudo ./rufus-linux --device /dev/sdX --image /path/to/linux.iso
```

### Step 4: Confirm and Wait
Confirm the operation when prompted and wait for the process to complete. Do not disconnect the drive until the operation finishes.

### Pro Tips:
- **Always double-check your device selection** - formatting will erase all data!
- **Use `sudo ./rufus-linux --list`** to see available drives before starting
- **FAT32 is the most universally compatible filesystem** for most use cases
- **For Windows compatibility**, use NTFS format
- **For Linux systems**, ext4 is recommended
- **Always safely eject** your USB drive when the operation completes

### Important Safety Notes:
- ⚠️ **This tool requires root privileges** - only run it when you need to format USB drives
- ⚠️ **Formatting will permanently erase all data** on the selected drive
- ⚠️ **Triple-check device paths** - selecting the wrong device could damage your system
- ✅ **Only removable drives are shown** for your safety

## Implementation Details

### Windows API Compatibility Layer
The port uses a clean abstraction layer (`linux_compat.c/h`) that translates Windows API operations to Linux equivalents:
- **File Operations**: `CreateFile` → `open()` with proper flags
- **Device Control**: `DeviceIoControl` → Linux `ioctl()` operations
- **Device Enumeration**: Windows APIs → Linux sysfs parsing
- **Error Handling**: Windows error codes → Linux errno translation

### Modern Linux Integration
- Uses Linux sysfs (`/sys/block`) for device detection
- Integrates with system formatting tools (`mkfs.*`)
- Follows XDG standards for desktop integration
- Proper privilege handling and user feedback

## Relationship to Original Rufus

This is an **independent Linux port** inspired by the excellent Windows utility [Rufus](https://rufus.ie) by Pete Batard. While not officially affiliated, it:
- Maintains the same user experience philosophy
- Preserves the interface design and workflow  
- Follows the same safety practices
- Respects the GPL v3+ license

We hope this demonstrates the value of Linux support and may inspire official cross-platform development.

## License

GNU General Public License v3.0 - see [LICENSE.txt](LICENSE.txt) file for details.

## Acknowledgments

- **Pete Batard** - Creator of the original Rufus utility
- **GTK Development Team** - Excellent GUI framework
- **Linux Community** - Robust ecosystem enabling this project
