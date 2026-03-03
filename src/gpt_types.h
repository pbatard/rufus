/*
 * Rufus: The Reliable USB Formatting Utility
 * GPT Partition Types
 * Copyright © 2020-2026 Pete Batard <pete@akeo.ie>
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
// MinGW won't properly embed the GUIDs unless the following is defined
#define INITGUID
#include <guiddef.h>

#pragma once

typedef struct {
    uint16_t    code;
    const char* guid_str;
    const char* description;
} gpt_type_entry_t;

#define AddType(code, guid, desc, ...) { code, guid, desc },

/*
 * Because we value our sanity, we build our GPT entries from code lifted
 * verbatim from Debian's gdisk's parttypes.cc and use AddType() as a macro
 * to create a static table.
 */
static const gpt_type_entry_t gpt_type_table[] = {
    // From https://salsa.debian.org/debian/gdisk/-/blob/master/parttypes.cc
    // Semicolons after Addtype() *MUST* be removed.

    // Start with the "unused entry," which should normally appear only
    // on empty partition table entries....
    AddType(0x0000, "00000000-0000-0000-0000-000000000000", "Unused entry", 0)

    // DOS/Windows partition types, most of which are hidden from the "L" listing
    // (they're available mainly for MBR-to-GPT conversions).
    AddType(0x0100, "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", "Microsoft basic data", 0) // FAT-12
    AddType(0x0400, "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", "Microsoft basic data", 0) // FAT-16 < 32M
    AddType(0x0600, "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", "Microsoft basic data", 0) // FAT-16
    AddType(0x0700, "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", "Microsoft basic data", 1) // NTFS (or HPFS)
    AddType(0x0701, "558D43C5-A1AC-43C0-AAC8-D1472B2923D1", "Microsoft Storage Replica", 1)
    AddType(0x0702, "90B6FF38-B98F-4358-A21F-48F35B4A8AD3", "ArcaOS Type 1", 1)
    AddType(0x0b00, "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", "Microsoft basic data", 0) // FAT-32
    AddType(0x0c00, "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", "Microsoft basic data", 0) // FAT-32 LBA
    AddType(0x0c01, "E3C9E316-0B5C-4DB8-817D-F92DF00215AE", "Microsoft reserved")
    AddType(0x0e00, "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", "Microsoft basic data", 0) // FAT-16 LBA
    AddType(0x1100, "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", "Microsoft basic data", 0) // Hidden FAT-12
    AddType(0x1400, "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", "Microsoft basic data", 0) // Hidden FAT-16 < 32M
    AddType(0x1600, "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", "Microsoft basic data", 0) // Hidden FAT-16
    AddType(0x1700, "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", "Microsoft basic data", 0) // Hidden NTFS (or HPFS)
    AddType(0x1b00, "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", "Microsoft basic data", 0) // Hidden FAT-32
    AddType(0x1c00, "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", "Microsoft basic data", 0) // Hidden FAT-32 LBA
    AddType(0x1e00, "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", "Microsoft basic data", 0) // Hidden FAT-16 LBA
    AddType(0x2700, "DE94BBA4-06D1-4D40-A16A-BFD50179D6AC", "Windows RE")

    // Open Network Install Environment (ONIE) specific types.
    // See http://www.onie.org/ and
    // https://github.com/opencomputeproject/onie/blob/master/patches/gptfdisk/add-onie-partition-types.patch
    AddType(0x3000, "7412F7D5-A156-4B13-81DC-867174929325", "ONIE boot")
    AddType(0x3001, "D4E6E2CD-4469-46F3-B5CB-1BFF57AFC149", "ONIE config")

    // Plan 9; see http://man.cat-v.org/9front/8/prep
    AddType(0x3900, "C91818F9-8025-47AF-89D2-F030D7000C2C", "Plan 9")

    // PowerPC reference platform boot partition
    AddType(0x4100, "9E1A2D38-C612-4316-AA26-8B49521E5A8B", "PowerPC PReP boot")

    // Windows LDM ("dynamic disk") types
    AddType(0x4200, "AF9B60A0-1431-4F62-BC68-3311714A69AD", "Windows LDM data") // Logical disk manager
    AddType(0x4201, "5808C8AA-7E8F-42E0-85D2-E1E90434CFB3", "Windows LDM metadata") // Logical disk manager
    AddType(0x4202, "E75CAF8F-F680-4CEE-AFA3-B001E56EFC2D", "Windows Storage Spaces") // A newer LDM-type setup

    // An oddball IBM filesystem....
    AddType(0x7501, "37AFFC90-EF7D-4E96-91C3-2D7AE055B174", "IBM GPFS") // General Parallel File System (GPFS)

    // ChromeOS-specific partition types...
    // Values taken from vboot_reference/firmware/lib/cgptlib/include/gpt.h in
    // ChromeOS source code, retrieved 12/23/2010. They're also at
    // http://www.chromium.org/chromium-os/chromiumos-design-docs/disk-format.
    // These have no MBR equivalents, AFAIK, so I'm using 0x7Fxx values, since they're close
    // to the Linux values.
    AddType(0x7f00, "FE3A2A5D-4F32-41A7-B725-ACCC3285A309", "ChromeOS kernel")
    AddType(0x7f01, "3CB8E202-3B7E-47DD-8A3C-7FF2A13CFCEC", "ChromeOS root")
    AddType(0x7f02, "2E0A753D-9E48-43B0-8337-B15192CB1B5E", "ChromeOS reserved")
    AddType(0x7f03, "CAB6E88E-ABF3-4102-A07A-D4BB9BE3C1D3", "ChromeOS firmware")
    AddType(0x7f04, "09845860-705F-4BB5-B16C-8A8A099CAF52", "ChromeOS mini-OS")
    AddType(0x7f05, "3F0F8318-F146-4E6B-8222-C28C8F02E0D5", "ChromeOS hibernate")

    // Linux-specific partition types....
    AddType(0x8200, "0657FD6D-A4AB-43C4-84E5-0933C84B4F4F", "Linux swap") // Linux swap (or Solaris on MBR)
    AddType(0x8300, "0FC63DAF-8483-4772-8E79-3D69D8477DE4", "Linux filesystem") // Linux native
    AddType(0x8301, "8DA63339-0007-60C0-C436-083AC8230908", "Linux reserved")
    // See https://www.freedesktop.org/software/systemd/man/systemd-gpt-auto-generator.html
    // and https://systemd.io/DISCOVERABLE_PARTITIONS
    AddType(0x8302, "933AC7E1-2EB4-4F13-B844-0E14E2AEF915", "Linux /home") // Linux /home (auto-mounted by systemd)
    AddType(0x8303, "44479540-F297-41B2-9AF7-D131D5F0458A", "Linux x86 root (/)") // Linux / on x86 (auto-mounted by systemd)
    AddType(0x8304, "4F68BCE3-E8CD-4DB1-96E7-FBCAF984B709", "Linux x86-64 root (/)") // Linux / on x86-64 (auto-mounted by systemd)
    AddType(0x8305, "B921B045-1DF0-41C3-AF44-4C6F280D3FAE", "Linux ARM64 root (/)") // Linux / on 64-bit ARM (auto-mounted by systemd)
    AddType(0x8306, "3B8F8425-20E0-4F3B-907F-1A25A76F98E8", "Linux /srv") // Linux /srv (auto-mounted by systemd)
    AddType(0x8307, "69DAD710-2CE4-4E3C-B16C-21A1D49ABED3", "Linux ARM32 root (/)") // Linux / on 32-bit ARM (auto-mounted by systemd)
    AddType(0x8308, "7FFEC5C9-2D00-49B7-8941-3EA10A5586B7", "Linux dm-crypt")
    AddType(0x8309, "CA7D7CCB-63ED-4C53-861C-1742536059CC", "Linux LUKS")
    AddType(0x830A, "993D8D3D-F80E-4225-855A-9DAF8ED7EA97", "Linux IA-64 root (/)") // Linux / on Itanium (auto-mounted by systemd)
    AddType(0x830B, "D13C5D3B-B5D1-422A-B29F-9454FDC89D76", "Linux x86 root verity")
    AddType(0x830C, "2C7357ED-EBD2-46D9-AEC1-23D437EC2BF5", "Linux x86-64 root verity")
    AddType(0x830D, "7386CDF2-203C-47A9-A498-F2ECCE45A2D6", "Linux ARM32 root verity")
    AddType(0x830E, "DF3300CE-D69F-4C92-978C-9BFB0F38D820", "Linux ARM64 root verity")
    AddType(0x830F, "86ED10D5-B607-45BB-8957-D350F23D0571", "Linux IA-64 root verity")
    AddType(0x8310, "4D21B016-B534-45C2-A9FB-5C16E091FD2D", "Linux /var") // Linux /var (auto-mounted by systemd)
    AddType(0x8311, "7EC6F557-3BC5-4ACA-B293-16EF5DF639D1", "Linux /var/tmp") // Linux /var/tmp (auto-mounted by systemd)
    // https://systemd.io/HOME_DIRECTORY/
    AddType(0x8312, "773F91EF-66D4-49B5-BD83-D683BF40AD16", "Linux user's home") // used by systemd-homed
    AddType(0x8313, "75250D76-8CC6-458E-BD66-BD47CC81A812", "Linux x86 /usr ") // Linux /usr on x86 (auto-mounted by systemd)
    AddType(0x8314, "8484680C-9521-48C6-9C11-B0720656F69E", "Linux x86-64 /usr") // Linux /usr on x86-64 (auto-mounted by systemd)
    AddType(0x8315, "7D0359A3-02B3-4F0A-865C-654403E70625", "Linux ARM32 /usr") // Linux /usr on 32-bit ARM (auto-mounted by systemd)
    AddType(0x8316, "B0E01050-EE5F-4390-949A-9101B17104E9", "Linux ARM64 /usr") // Linux /usr on 64-bit ARM (auto-mounted by systemd)
    AddType(0x8317, "4301D2A6-4E3B-4B2A-BB94-9E0B2C4225EA", "Linux IA-64 /usr") // Linux /usr on Itanium (auto-mounted by systemd)
    AddType(0x8318, "8F461B0D-14EE-4E81-9AA9-049B6FB97ABD", "Linux x86 /usr verity")
    AddType(0x8319, "77FF5F63-E7B6-4633-ACF4-1565B864C0E6", "Linux x86-64 /usr verity")
    AddType(0x831A, "C215D751-7BCD-4649-BE90-6627490A4C05", "Linux ARM32 /usr verity")
    AddType(0x831B, "6E11A4E7-FBCA-4DED-B9E9-E1A512BB664E", "Linux ARM64 /usr verity")
    AddType(0x831C, "6A491E03-3BE7-4545-8E38-83320E0EA880", "Linux IA-64 /usr verity")
    AddType(0x831D, "6523F8AE-3EB1-4E2A-A05A-18B695AE656F", "Linux Alpha root (/)")
    AddType(0x831E, "D27F46ED-2919-4CB8-BD25-9531F3C16534", "Linux ARC root (/)")
    AddType(0x831F, "77055800-792C-4F94-B39A-98C91B762BB6", "Linux LoongArch root (/)")
    AddType(0x8320, "E9434544-6E2C-47CC-BAE2-12D6DEAFB44C", "Linux MIPS-32 BE root (/)")
    AddType(0x8321, "D113AF76-80EF-41B4-BDB6-0CFF4D3D4A25", "Linux MIPS-64 BE root (/)")
    AddType(0x8322, "37C58C8A-D913-4156-A25F-48B1B64E07F0", "Linux MIPS-32 LE root (/)")
    AddType(0x8323, "700BDA43-7A34-4507-B179-EEB93D7A7CA3", "Linux MIPS-64 LE root (/)")
    AddType(0x8324, "1AACDB3B-5444-4138-BD9E-E5C2239B2346", "Linux PA-RISC root (/)")
    AddType(0x8325, "1DE3F1EF-FA98-47B5-8DCD-4A860A654D78", "Linux PowerPC-32 root (/)")
    AddType(0x8326, "912ADE1D-A839-4913-8964-A10EEE08FBD2", "Linux PowerPC-64 BE root (/)")
    AddType(0x8327, "C31C45E6-3F39-412E-80FB-4809C4980599", "Linux PowerPC-64 LE root (/)")
    AddType(0x8328, "60D5A7FE-8E7D-435C-B714-3DD8162144E1", "Linux RISC-V-32 root (/)")
    AddType(0x8329, "72EC70A6-CF74-40E6-BD49-4BDA08E8F224", "Linux RISC-V-64 root (/)")
    AddType(0x832A, "08A7ACEA-624C-4A20-91E8-6E0FA67D23F9", "Linux s390 root (/)")
    AddType(0x832B, "5EEAD9A9-FE09-4A1E-A1D7-520D00531306", "Linux s390x root (/)")
    AddType(0x832C, "C50CDD70-3862-4CC3-90E1-809A8C93EE2C", "Linux TILE-Gx root (/)")
    AddType(0x832D, "E18CF08C-33EC-4C0D-8246-C6C6FB3DA024", "Linux Alpha /usr")
    AddType(0x832E, "7978A683-6316-4922-BBEE-38BFF5A2FECC", "Linux ARC /usr")
    AddType(0x832F, "E611C702-575C-4CBE-9A46-434FA0BF7E3F", "Linux LoongArch /usr")
    AddType(0x8330, "773B2ABC-2A99-4398-8BF5-03BAAC40D02B", "Linux MIPS-32 BE /usr")
    AddType(0x8331, "57E13958-7331-4365-8E6E-35EEEE17C61B", "Linux MIPS-64 BE /usr")
    AddType(0x8332, "0F4868E9-9952-4706-979F-3ED3A473E947", "Linux MIPS-32 LE /usr")
    AddType(0x8333, "C97C1F32-BA06-40B4-9F22-236061B08AA8", "Linux MIPS-64 LE /usr")
    AddType(0x8334, "DC4A4480-6917-4262-A4EC-DB9384949F25", "Linux PA-RISC /usr")
    AddType(0x8335, "7D14FEC5-CC71-415D-9D6C-06BF0B3C3EAF", "Linux PowerPC-32 /usr")
    AddType(0x8336, "2C9739E2-F068-46B3-9FD0-01C5A9AFBCCA", "Linux PowerPC-64 BE /usr")
    AddType(0x8337, "15BB03AF-77E7-4D4A-B12B-C0D084F7491C", "Linux PowerPC-64 LE /usr")
    AddType(0x8338, "B933FB22-5C3F-4F91-AF90-E2BB0FA50702", "Linux RISC-V-32 /usr")
    AddType(0x8339, "BEAEC34B-8442-439B-A40B-984381ED097D", "Linux RISC-V-64 /usr")
    AddType(0x833A, "CD0F869B-D0FB-4CA0-B141-9EA87CC78D66", "Linux s390 /usr")
    AddType(0x833B, "8A4F5770-50AA-4ED3-874A-99B710DB6FEA", "Linux s390x /usr")
    AddType(0x833C, "55497029-C7C1-44CC-AA39-815ED1558630", "Linux TILE-Gx /usr")
    AddType(0x833D, "FC56D9E9-E6E5-4C06-BE32-E74407CE09A5", "Linux Alpha root verity")
    AddType(0x833E, "24B2D975-0F97-4521-AFA1-CD531E421B8D", "Linux ARC root verity")
    AddType(0x833F, "F3393B22-E9AF-4613-A948-9D3BFBD0C535", "Linux LoongArch root verity")
    AddType(0x8340, "7A430799-F711-4C7E-8E5B-1D685BD48607", "Linux MIPS-32 BE root verity")
    AddType(0x8341, "579536F8-6A33-4055-A95A-DF2D5E2C42A8", "Linux MIPS-64 BE root verity")
    AddType(0x8342, "D7D150D2-2A04-4A33-8F12-16651205FF7B", "Linux MIPS-32 LE root verity")
    AddType(0x8343, "16B417F8-3E06-4F57-8DD2-9B5232F41AA6", "Linux MIPS-64 LE root verity")
    AddType(0x8344, "D212A430-FBC5-49F9-A983-A7FEEF2B8D0E", "Linux PA-RISC root verity")
    AddType(0x8345, "906BD944-4589-4AAE-A4E4-DD983917446A", "Linux PowerPC-64 LE root verity")
    AddType(0x8346, "9225A9A3-3C19-4D89-B4F6-EEFF88F17631", "Linux PowerPC-64 BE root verity")
    AddType(0x8347, "98CFE649-1588-46DC-B2F0-ADD147424925", "Linux PowerPC-32 root verity")
    AddType(0x8348, "AE0253BE-1167-4007-AC68-43926C14C5DE", "Linux RISC-V-32 root verity")
    AddType(0x8349, "B6ED5582-440B-4209-B8DA-5FF7C419EA3D", "Linux RISC-V-64 root verity")
    AddType(0x834A, "7AC63B47-B25C-463B-8DF8-B4A94E6C90E1", "Linux s390 root verity")
    AddType(0x834B, "B325BFBE-C7BE-4AB8-8357-139E652D2F6B", "Linux s390x root verity")
    AddType(0x834C, "966061EC-28E4-4B2E-B4A5-1F0A825A1D84", "Linux TILE-Gx root verity")
    AddType(0x834D, "8CCE0D25-C0D0-4A44-BD87-46331BF1DF67", "Linux Alpha /usr verity")
    AddType(0x834E, "FCA0598C-D880-4591-8C16-4EDA05C7347C", "Linux ARC /usr verity")
    AddType(0x834F, "F46B2C26-59AE-48F0-9106-C50ED47F673D", "Linux LoongArch /usr verity")
    AddType(0x8350, "6E5A1BC8-D223-49B7-BCA8-37A5FCCEB996", "Linux MIPS-32 BE /usr verity")
    AddType(0x8351, "81CF9D90-7458-4DF4-8DCF-C8A3A404F09B", "Linux MIPS-64 BE /usr verity")
    AddType(0x8352, "46B98D8D-B55C-4E8F-AAB3-37FCA7F80752", "Linux MIPS-32 LE /usr verity")
    AddType(0x8353, "3C3D61FE-B5F3-414D-BB71-8739A694A4EF", "Linux MIPS-64 LE /usr verity")
    AddType(0x8354, "5843D618-EC37-48D7-9F12-CEA8E08768B2", "Linux PA-RISC /usr verity")
    AddType(0x8355, "EE2B9983-21E8-4153-86D9-B6901A54D1CE", "Linux PowerPC-64 LE /usr verity")
    AddType(0x8356, "BDB528A5-A259-475F-A87D-DA53FA736A07", "Linux PowerPC-64 BE /usr verity")
    AddType(0x8357, "DF765D00-270E-49E5-BC75-F47BB2118B09", "Linux PowerPC-32 /usr verity")
    AddType(0x8358, "CB1EE4E3-8CD0-4136-A0A4-AA61A32E8730", "Linux RISC-V-32 /usr verity")
    AddType(0x8359, "8F1056BE-9B05-47C4-81D6-BE53128E5B54", "Linux RISC-V-64 /usr verity")
    AddType(0x835A, "B663C618-E7BC-4D6D-90AA-11B756BB1797", "Linux s390 /usr verity")
    AddType(0x835B, "31741CC4-1A2A-4111-A581-E00B447D2D06", "Linux s390x /usr verity")
    AddType(0x835C, "2FB4BF56-07FA-42DA-8132-6B139F2026AE", "Linux TILE-Gx /usr verity")
    AddType(0x835D, "D46495B7-A053-414F-80F7-700C99921EF8", "Linux Alpha root verity signature")
    AddType(0x835E, "143A70BA-CBD3-4F06-919F-6C05683A78BC", "Linux ARC root verity signature")
    AddType(0x835F, "42B0455F-EB11-491D-98D3-56145BA9D037", "Linux ARM32 root verity signature")
    AddType(0x8360, "6DB69DE6-29F4-4758-A7A5-962190F00CE3", "Linux ARM64 root verity signature")
    AddType(0x8361, "E98B36EE-32BA-4882-9B12-0CE14655F46A", "Linux IA-64 root verity signature")
    AddType(0x8362, "5AFB67EB-ECC8-4F85-AE8E-AC1E7C50E7D0", "Linux LoongArch root verity signature")
    AddType(0x8363, "BBA210A2-9C5D-45EE-9E87-FF2CCBD002D0", "Linux MIPS-32 BE root verity signature")
    AddType(0x8364, "43CE94D4-0F3D-4999-8250-B9DEAFD98E6E", "Linux MIPS-64 BE root verity signature")
    AddType(0x8365, "C919CC1F-4456-4EFF-918C-F75E94525CA5", "Linux MIPS-32 LE root verity signature")
    AddType(0x8366, "904E58EF-5C65-4A31-9C57-6AF5FC7C5DE7", "Linux MIPS-64 LE root verity signature")
    AddType(0x8367, "15DE6170-65D3-431C-916E-B0DCD8393F25", "Linux PA-RISC root verity signature")
    AddType(0x8368, "D4A236E7-E873-4C07-BF1D-BF6CF7F1C3C6", "Linux PowerPC-64 LE root verity signature")
    AddType(0x8369, "F5E2C20C-45B2-4FFA-BCE9-2A60737E1AAF", "Linux PowerPC-64 BE root verity signature")
    AddType(0x836A, "1B31B5AA-ADD9-463A-B2ED-BD467FC857E7", "Linux PowerPC-32 root verity signature")
    AddType(0x836B, "3A112A75-8729-4380-B4CF-764D79934448", "Linux RISC-V-32 root verity signature")
    AddType(0x836C, "EFE0F087-EA8D-4469-821A-4C2A96A8386A", "Linux RISC-V-64 root verity signature")
    AddType(0x836D, "3482388E-4254-435A-A241-766A065F9960", "Linux s390 root verity signature")
    AddType(0x836E, "C80187A5-73A3-491A-901A-017C3FA953E9", "Linux s390x root verity signature")
    AddType(0x836F, "B3671439-97B0-4A53-90F7-2D5A8F3AD47B", "Linux TILE-Gx root verity signature")
    AddType(0x8370, "41092B05-9FC8-4523-994F-2DEF0408B176", "Linux x86-64 root verity signature")
    AddType(0x8371, "5996FC05-109C-48DE-808B-23FA0830B676", "Linux x86 root verity signature")
    AddType(0x8372, "5C6E1C76-076A-457A-A0FE-F3B4CD21CE6E", "Linux Alpha /usr verity signature")
    AddType(0x8373, "94F9A9A1-9971-427A-A400-50CB297F0F35", "Linux ARC /usr verity signature")
    AddType(0x8374, "D7FF812F-37D1-4902-A810-D76BA57B975A", "Linux ARM32 /usr verity signature")
    AddType(0x8375, "C23CE4FF-44BD-4B00-B2D4-B41B3419E02A", "Linux ARM64 /usr verity signature")
    AddType(0x8376, "8DE58BC2-2A43-460D-B14E-A76E4A17B47F", "Linux IA-64 /usr verity signature")
    AddType(0x8377, "B024F315-D330-444C-8461-44BBDE524E99", "Linux LoongArch /usr verity signature")
    AddType(0x8378, "97AE158D-F216-497B-8057-F7F905770F54", "Linux MIPS-32 BE /usr verity signature")
    AddType(0x8379, "05816CE2-DD40-4AC6-A61D-37D32DC1BA7D", "Linux MIPS-64 BE /usr verity signature")
    AddType(0x837A, "3E23CA0B-A4BC-4B4E-8087-5AB6A26AA8A9", "Linux MIPS-32 LE /usr verity signature")
    AddType(0x837B, "F2C2C7EE-ADCC-4351-B5C6-EE9816B66E16", "Linux MIPS-64 LE /usr verity signature")
    AddType(0x837C, "450DD7D1-3224-45EC-9CF2-A43A346D71EE", "Linux PA-RISC /usr verity signature")
    AddType(0x837D, "C8BFBD1E-268E-4521-8BBA-BF314C399557", "Linux PowerPC-64 LE /usr verity signature")
    AddType(0x837E, "0B888863-D7F8-4D9E-9766-239FCE4D58AF", "Linux PowerPC-64 BE /usr verity signature")
    AddType(0x837F, "7007891D-D371-4A80-86A4-5CB875B9302E", "Linux PowerPC-32 /usr verity signature")
    AddType(0x8380, "C3836A13-3137-45BA-B583-B16C50FE5EB4", "Linux RISC-V-32 /usr verity signature")
    AddType(0x8381, "D2F9000A-7A18-453F-B5CD-4D32F77A7B32", "Linux RISC-V-64 /usr verity signature")
    AddType(0x8382, "17440E4F-A8D0-467F-A46E-3912AE6EF2C5", "Linux s390 /usr verity signature")
    AddType(0x8383, "3F324816-667B-46AE-86EE-9B0C0C6C11B4", "Linux s390x /usr verity signature")
    AddType(0x8384, "4EDE75E2-6CCC-4CC8-B9C7-70334B087510", "Linux TILE-Gx /usr verity signature")
    AddType(0x8385, "E7BB33FB-06CF-4E81-8273-E543B413E2E2", "Linux x86-64 /usr verity signature")
    AddType(0x8386, "974A71C0-DE41-43C3-BE5D-5C5CCD1AD2C0", "Linux x86 /usr verity signature")

    // Used by Intel Rapid Start technology
    AddType(0x8400, "D3BFE2DE-3DAF-11DF-BA40-E3A556D89593", "Intel Rapid Start")
    // This is another Intel-associated technology, so I'm keeping it close to the previous one....
    AddType(0x8401, "7C5222BD-8F5D-4087-9C00-BF9843C7B58C", "SPDK block device")

    // Type codes for Container Linux (formerly CoreOS; https://coreos.com)
    AddType(0x8500, "5DFBF5F4-2848-4BAC-AA5E-0D9A20B745A6", "Container Linux /usr")
    AddType(0x8501, "3884DD41-8582-4404-B9A8-E9B84F2DF50E", "Container Linux resizable rootfs")
    AddType(0x8502, "C95DC21A-DF0E-4340-8D7B-26CBFA9A03E0", "Container Linux /OEM customizations")
    AddType(0x8503, "BE9067B9-EA49-4F15-B4F6-F36F8C9E1818", "Container Linux root on RAID")

    // Another Linux type code....
    AddType(0x8e00, "E6D6D379-F507-44C2-A23C-238F2A3DF928", "Linux LVM")

    // Android type codes....
    // from Wikipedia, https://gist.github.com/culots/704afd126dec2f45c22d0c9d42cb7fab,
    // and my own Android devices' partition tables
    AddType(0xa000, "2568845D-2332-4675-BC39-8FA5A4748D15", "Android bootloader")
    AddType(0xa001, "114EAFFE-1552-4022-B26E-9B053604CF84", "Android bootloader 2")
    AddType(0xa002, "49A4D17F-93A3-45C1-A0DE-F50B2EBE2599", "Android boot 1")
    AddType(0xa003, "4177C722-9E92-4AAB-8644-43502BFD5506", "Android recovery 1")
    AddType(0xa004, "EF32A33B-A409-486C-9141-9FFB711F6266", "Android misc")
    AddType(0xa005, "20AC26BE-20B7-11E3-84C5-6CFDB94711E9", "Android metadata")
    AddType(0xa006, "38F428E6-D326-425D-9140-6E0EA133647C", "Android system 1")
    AddType(0xa007, "A893EF21-E428-470A-9E55-0668FD91A2D9", "Android cache")
    AddType(0xa008, "DC76DDA9-5AC1-491C-AF42-A82591580C0D", "Android data")
    AddType(0xa009, "EBC597D0-2053-4B15-8B64-E0AAC75F4DB1", "Android persistent")
    AddType(0xa00a, "8F68CC74-C5E5-48DA-BE91-A0C8C15E9C80", "Android factory")
    AddType(0xa00b, "767941D0-2085-11E3-AD3B-6CFDB94711E9", "Android fastboot/tertiary")
    AddType(0xa00c, "AC6D7924-EB71-4DF8-B48D-E267B27148FF", "Android OEM")
    AddType(0xa00d, "C5A0AEEC-13EA-11E5-A1B1-001E67CA0C3C", "Android vendor")
    AddType(0xa00e, "BD59408B-4514-490D-BF12-9878D963F378", "Android config")
    AddType(0xa00f, "9FDAA6EF-4B3F-40D2-BA8D-BFF16BFB887B", "Android factory (alt)")
    AddType(0xa010, "19A710A2-B3CA-11E4-B026-10604B889DCF", "Android meta")
    AddType(0xa011, "193D1EA4-B3CA-11E4-B075-10604B889DCF", "Android EXT")
    AddType(0xa012, "DEA0BA2C-CBDD-4805-B4F9-F428251C3E98", "Android SBL1")
    AddType(0xa013, "8C6B52AD-8A9E-4398-AD09-AE916E53AE2D", "Android SBL2")
    AddType(0xa014, "05E044DF-92F1-4325-B69E-374A82E97D6E", "Android SBL3")
    AddType(0xa015, "400FFDCD-22E0-47E7-9A23-F16ED9382388", "Android APPSBL")
    AddType(0xa016, "A053AA7F-40B8-4B1C-BA08-2F68AC71A4F4", "Android QSEE/tz")
    AddType(0xa017, "E1A6A689-0C8D-4CC6-B4E8-55A4320FBD8A", "Android QHEE/hyp")
    AddType(0xa018, "098DF793-D712-413D-9D4E-89D711772228", "Android RPM")
    AddType(0xa019, "D4E0D938-B7FA-48C1-9D21-BC5ED5C4B203", "Android WDOG debug/sdi")
    AddType(0xa01a, "20A0C19C-286A-42FA-9CE7-F64C3226A794", "Android DDR")
    AddType(0xa01b, "A19F205F-CCD8-4B6D-8F1E-2D9BC24CFFB1", "Android CDT")
    AddType(0xa01c, "66C9B323-F7FC-48B6-BF96-6F32E335A428", "Android RAM dump")
    AddType(0xa01d, "303E6AC3-AF15-4C54-9E9B-D9A8FBECF401", "Android SEC")
    AddType(0xa01e, "C00EEF24-7709-43D6-9799-DD2B411E7A3C", "Android PMIC")
    AddType(0xa01f, "82ACC91F-357C-4A68-9C8F-689E1B1A23A1", "Android misc 1")
    AddType(0xa020, "E2802D54-0545-E8A1-A1E8-C7A3E245ACD4", "Android misc 2")
    AddType(0xa021, "65ADDCF4-0C5C-4D9A-AC2D-D90B5CBFCD03", "Android device info")
    AddType(0xa022, "E6E98DA2-E22A-4D12-AB33-169E7DEAA507", "Android APDP")
    AddType(0xa023, "ED9E8101-05FA-46B7-82AA-8D58770D200B", "Android MSADP")
    AddType(0xa024, "11406F35-1173-4869-807B-27DF71802812", "Android DPO")
    AddType(0xa025, "9D72D4E4-9958-42DA-AC26-BEA7A90B0434", "Android recovery 2")
    AddType(0xa026, "6C95E238-E343-4BA8-B489-8681ED22AD0B", "Android persist")
    AddType(0xa027, "EBBEADAF-22C9-E33B-8F5D-0E81686A68CB", "Android modem ST1")
    AddType(0xa028, "0A288B1F-22C9-E33B-8F5D-0E81686A68CB", "Android modem ST2")
    AddType(0xa029, "57B90A16-22C9-E33B-8F5D-0E81686A68CB", "Android FSC")
    AddType(0xa02a, "638FF8E2-22C9-E33B-8F5D-0E81686A68CB", "Android FSG 1")
    AddType(0xa02b, "2013373E-1AC4-4131-BFD8-B6A7AC638772", "Android FSG 2")
    AddType(0xa02c, "2C86E742-745E-4FDD-BFD8-B6A7AC638772", "Android SSD")
    AddType(0xa02d, "DE7D4029-0F5B-41C8-AE7E-F6C023A02B33", "Android keystore")
    AddType(0xa02e, "323EF595-AF7A-4AFA-8060-97BE72841BB9", "Android encrypt")
    AddType(0xa02f, "45864011-CF89-46E6-A445-85262E065604", "Android EKSST")
    AddType(0xa030, "8ED8AE95-597F-4C8A-A5BD-A7FF8E4DFAA9", "Android RCT")
    AddType(0xa031, "DF24E5ED-8C96-4B86-B00B-79667DC6DE11", "Android spare1")
    AddType(0xa032, "7C29D3AD-78B9-452E-9DEB-D098D542F092", "Android spare2")
    AddType(0xa033, "379D107E-229E-499D-AD4F-61F5BCF87BD4", "Android spare3")
    AddType(0xa034, "0DEA65E5-A676-4CDF-823C-77568B577ED5", "Android spare4")
    AddType(0xa035, "4627AE27-CFEF-48A1-88FE-99C3509ADE26", "Android raw resources")
    AddType(0xa036, "20117F86-E985-4357-B9EE-374BC1D8487D", "Android boot 2")
    AddType(0xa037, "86A7CB80-84E1-408C-99AB-694F1A410FC7", "Android FOTA")
    AddType(0xa038, "97D7B011-54DA-4835-B3C4-917AD6E73D74", "Android system 2")
    AddType(0xa039, "5594C694-C871-4B5F-90B1-690A6F68E0F7", "Android cache")
    AddType(0xa03a, "1B81E7E6-F50D-419B-A739-2AEEF8DA3335", "Android user data")
    AddType(0xa03b, "98523EC6-90FE-4C67-B50A-0FC59ED6F56D", "LG (Android) advanced flasher")
    AddType(0xa03c, "2644BCC0-F36A-4792-9533-1738BED53EE3", "Android PG1FS")
    AddType(0xa03d, "DD7C91E9-38C9-45C5-8A12-4A80F7E14057", "Android PG2FS")
    AddType(0xa03e, "7696D5B6-43FD-4664-A228-C563C4A1E8CC", "Android board info")
    AddType(0xa03f, "0D802D54-058D-4A20-AD2D-C7A362CEACD4", "Android MFG")
    AddType(0xa040, "10A0C19C-516A-5444-5CE3-664C3226A794", "Android limits")

    // Atari TOS partition type
    AddType(0xa200, "734E5AFE-F61A-11E6-BC64-92361F002671", "Atari TOS basic data")

    // FreeBSD partition types....
    // Note: Rather than extract FreeBSD disklabel data, convert FreeBSD
    // partitions in-place, and let FreeBSD sort out the details....
    AddType(0xa500, "516E7CB4-6ECF-11D6-8FF8-00022D09712B", "FreeBSD disklabel")
    AddType(0xa501, "83BD6B9D-7F41-11DC-BE0B-001560B84F0F", "FreeBSD boot")
    AddType(0xa502, "516E7CB5-6ECF-11D6-8FF8-00022D09712B", "FreeBSD swap")
    AddType(0xa503, "516E7CB6-6ECF-11D6-8FF8-00022D09712B", "FreeBSD UFS")
    AddType(0xa504, "516E7CBA-6ECF-11D6-8FF8-00022D09712B", "FreeBSD ZFS")
    AddType(0xa505, "516E7CB8-6ECF-11D6-8FF8-00022D09712B", "FreeBSD Vinum/RAID")
    AddType(0xa506, "74BA7DD9-A689-11E1-BD04-00E081286ACF", "FreeBSD nandfs")

    // Midnight BSD partition types....
    AddType(0xa580, "85D5E45A-237C-11E1-B4B3-E89A8F7FC3A7", "Midnight BSD data")
    AddType(0xa581, "85D5E45E-237C-11E1-B4B3-E89A8F7FC3A7", "Midnight BSD boot")
    AddType(0xa582, "85D5E45B-237C-11E1-B4B3-E89A8F7FC3A7", "Midnight BSD swap")
    AddType(0xa583, "0394Ef8B-237E-11E1-B4B3-E89A8F7FC3A7", "Midnight BSD UFS")
    AddType(0xa584, "85D5E45D-237C-11E1-B4B3-E89A8F7FC3A7", "Midnight BSD ZFS")
    AddType(0xa585, "85D5E45C-237C-11E1-B4B3-E89A8F7FC3A7", "Midnight BSD Vinum")

    // OpenBSD partition type....
    AddType(0xa600, "824CC7A0-36A8-11E3-890A-952519AD3F61", "OpenBSD disklabel")

    // A MacOS partition type, separated from others by NetBSD partition types...
    AddType(0xa800, "55465300-0000-11AA-AA11-00306543ECAC", "Apple UFS") // Mac OS X

    // NetBSD partition types. Note that the main entry sets it up as a
    // FreeBSD disklabel. I'm not 100% certain this is the correct behavior.
    AddType(0xa900, "516E7CB4-6ECF-11D6-8FF8-00022D09712B", "FreeBSD disklabel", 0) // NetBSD disklabel
    AddType(0xa901, "49F48D32-B10E-11DC-B99B-0019D1879648", "NetBSD swap")
    AddType(0xa902, "49F48D5A-B10E-11DC-B99B-0019D1879648", "NetBSD FFS")
    AddType(0xa903, "49F48D82-B10E-11DC-B99B-0019D1879648", "NetBSD LFS")
    AddType(0xa904, "2DB519C4-B10F-11DC-B99B-0019D1879648", "NetBSD concatenated")
    AddType(0xa905, "2DB519EC-B10F-11DC-B99B-0019D1879648", "NetBSD encrypted")
    AddType(0xa906, "49F48DAA-B10E-11DC-B99B-0019D1879648", "NetBSD RAID")

    // Mac OS partition types (See also 0xa800, above)....
    AddType(0xab00, "426F6F74-0000-11AA-AA11-00306543ECAC", "Recovery HD")
    AddType(0xaf00, "48465300-0000-11AA-AA11-00306543ECAC", "Apple HFS/HFS+")
    AddType(0xaf01, "52414944-0000-11AA-AA11-00306543ECAC", "Apple RAID")
    AddType(0xaf02, "52414944-5F4F-11AA-AA11-00306543ECAC", "Apple RAID offline")
    AddType(0xaf03, "4C616265-6C00-11AA-AA11-00306543ECAC", "Apple label")
    AddType(0xaf04, "5265636F-7665-11AA-AA11-00306543ECAC", "AppleTV recovery")
    AddType(0xaf05, "53746F72-6167-11AA-AA11-00306543ECAC", "Apple Core Storage")
    AddType(0xaf06, "B6FA30DA-92D2-4A9A-96F1-871EC6486200", "Apple SoftRAID Status")
    AddType(0xaf07, "2E313465-19B9-463F-8126-8A7993773801", "Apple SoftRAID Scratch")
    AddType(0xaf08, "FA709C7E-65B1-4593-BFD5-E71D61DE9B02", "Apple SoftRAID Volume")
    AddType(0xaf09, "BBBA6DF5-F46F-4A89-8F59-8765B2727503", "Apple SoftRAID Cache")
    AddType(0xaf0a, "7C3457EF-0000-11AA-AA11-00306543ECAC", "Apple APFS")
    AddType(0xaf0b, "69646961-6700-11AA-AA11-00306543ECAC", "Apple APFS Pre-Boot")
    AddType(0xaf0c, "52637672-7900-11AA-AA11-00306543ECAC", "Apple APFS Recovery")

    // U-Boot boot loader; see https://lists.denx.de/pipermail/u-boot/2020-November/432928.html
    // and https://source.denx.de/u-boot/u-boot/-/blob/v2021.07/include/part_efi.h#L59-61
    AddType(0xb000, "3DE21764-95BD-54BD-A5C3-4ABE786F38A8", "U-Boot boot loader")

    // QNX Power-Safe (QNX6)
    AddType(0xb300, "CEF5A9AD-73BC-4601-89F3-CDEEEEE321A1", "QNX6 Power-Safe")

    // Barebox boot loader; see https://barebox.org/doc/latest/user/state.html?highlight=guid#sd-emmc-and-ata
    AddType(0xbb00, "4778ED65-BF42-45FA-9C5B-287A1DC4AAB1", "Barebox boot loader")

    // Acronis Secure Zone
    AddType(0xbc00, "0311FC50-01CA-4725-AD77-9ADBB20ACE98", "Acronis Secure Zone")

    // Solaris partition types (one of which is shared with MacOS)
    AddType(0xbe00, "6A82CB45-1DD2-11B2-99A6-080020736631", "Solaris boot")
    AddType(0xbf00, "6A85CF4D-1DD2-11B2-99A6-080020736631", "Solaris root")
    AddType(0xbf01, "6A898CC3-1DD2-11B2-99A6-080020736631", "Solaris /usr & Mac ZFS") // Solaris/MacOS
    AddType(0xbf02, "6A87C46F-1DD2-11B2-99A6-080020736631", "Solaris swap")
    AddType(0xbf03, "6A8B642B-1DD2-11B2-99A6-080020736631", "Solaris backup")
    AddType(0xbf04, "6A8EF2E9-1DD2-11B2-99A6-080020736631", "Solaris /var")
    AddType(0xbf05, "6A90BA39-1DD2-11B2-99A6-080020736631", "Solaris /home")
    AddType(0xbf06, "6A9283A5-1DD2-11B2-99A6-080020736631", "Solaris alternate sector")
    AddType(0xbf07, "6A945A3B-1DD2-11B2-99A6-080020736631", "Solaris Reserved 1")
    AddType(0xbf08, "6A9630D1-1DD2-11B2-99A6-080020736631", "Solaris Reserved 2")
    AddType(0xbf09, "6A980767-1DD2-11B2-99A6-080020736631", "Solaris Reserved 3")
    AddType(0xbf0a, "6A96237F-1DD2-11B2-99A6-080020736631", "Solaris Reserved 4")
    AddType(0xbf0b, "6A8D2AC7-1DD2-11B2-99A6-080020736631", "Solaris Reserved 5")

    // I can find no MBR equivalents for these, but they're on the
    // Wikipedia page for GPT, so here we go....
    AddType(0xc001, "75894C1E-3AEB-11D3-B7C1-7B03A0000000", "HP-UX data")
    AddType(0xc002, "E2A1E728-32E3-11D6-A682-7B03A0000000", "HP-UX service")

    // Open Network Install Environment (ONIE) partitions....
    AddType(0xe100, "7412F7D5-A156-4B13-81DC-867174929325", "ONIE boot")
    AddType(0xe101, "D4E6E2CD-4469-46F3-B5CB-1BFF57AFC149", "ONIE config")

    // Veracrypt (https://www.veracrypt.fr/en/Home.html) encrypted partition
    AddType(0xe900, "8C8F8EFF-AC95-4770-814A-21994F2DBC8F", "Veracrypt data")

    // See https://systemd.io/BOOT_LOADER_SPECIFICATION/
    AddType(0xea00, "BC13C2FF-59E6-4262-A352-B275FD6F7172", "XBOOTLDR partition")

    // Type code for Haiku; uses BeOS MBR code as hex code base
    AddType(0xeb00, "42465331-3BA3-10F1-802A-4861696B7521", "Haiku BFS")

    // Manufacturer-specific ESP-like partitions (in order in which they were added)
    AddType(0xed00, "F4019732-066E-4E12-8273-346C5641494F", "Sony system partition")
    AddType(0xed01, "BFBFAFE7-A34F-448A-9A5B-6213EB736C22", "Lenovo system partition")

    // EFI system and related partitions
    AddType(0xef00, "C12A7328-F81F-11D2-BA4B-00A0C93EC93B", "EFI system partition") // Parted identifies these as having the "boot flag" set
    AddType(0xef01, "024DEE41-33E7-11D3-9D69-0008C781F39F", "MBR partition scheme") // Used to nest MBR in GPT
    AddType(0xef02, "21686148-6449-6E6F-744E-656564454649", "BIOS boot partition") // Used by GRUB

    // Fuscia OS codes; see https://cs.opensource.google/fuchsia/fuchsia/+/main:zircon/system/public/zircon/hw/gpt.h
    AddType(0xf100, "FE8A2634-5E2E-46BA-99E3-3A192091A350", "Fuchsia boot loader (slot A/B/R)")
    AddType(0xf101, "D9FD4535-106C-4CEC-8D37-DFC020CA87CB", "Fuchsia durable mutable encrypted system data")
    AddType(0xf102, "A409E16B-78AA-4ACC-995C-302352621A41", "Fuchsia durable mutable boot loader")
    AddType(0xf103, "F95D940E-CABA-4578-9B93-BB6C90F29D3E", "Fuchsia factory ro system data")
    AddType(0xf104, "10B8DBAA-D2BF-42A9-98C6-A7C5DB3701E7", "Fuchsia factory ro bootloader data")
    AddType(0xf105, "49FD7CB8-DF15-4E73-B9D9-992070127F0F", "Fuchsia Volume Manager")
    AddType(0xf106, "421A8BFC-85D9-4D85-ACDA-B64EEC0133E9", "Fuchsia verified boot metadata (slot A/B/R)")
    AddType(0xf107, "9B37FFF6-2E58-466A-983A-F7926D0B04E0", "Fuchsia Zircon boot image (slot A/B/R)")
    AddType(0xf108, "C12A7328-F81F-11D2-BA4B-00A0C93EC93B", "Fuchsia ESP")
    AddType(0xf109, "606B000B-B7C7-4653-A7D5-B737332C899D", "Fuchsia System")
    AddType(0xf10a, "08185F0C-892D-428A-A789-DBEEC8F55E6A", "Fuchsia Data")
    AddType(0xf10b, "48435546-4953-2041-494E-5354414C4C52", "Fuchsia Install")
    AddType(0xf10c, "2967380E-134C-4CBB-B6DA-17E7CE1CA45D", "Fuchsia Blob")
    AddType(0xf10d, "41D0E340-57E3-954E-8C1E-17ECAC44CFF5", "Fuchsia FVM")
    AddType(0xf10e, "DE30CC86-1F4A-4A31-93C4-66F147D33E05", "Fuchsia Zircon boot image (slot A)")
    AddType(0xf10f, "23CC04DF-C278-4CE7-8471-897D1A4BCDF7", "Fuchsia Zircon boot image (slot B)")
    AddType(0xf110, "A0E5CF57-2DEF-46BE-A80C-A2067C37CD49", "Fuchsia Zircon boot image (slot R)")
    AddType(0xf111, "4E5E989E-4C86-11E8-A15B-480FCF35F8E6", "Fuchsia sys-config")
    AddType(0xf112, "5A3A90BE-4C86-11E8-A15B-480FCF35F8E6", "Fuchsia factory-config")
    AddType(0xf113, "5ECE94FE-4C86-11E8-A15B-480FCF35F8E6", "Fuchsia bootloader")
    AddType(0xf114, "8B94D043-30BE-4871-9DFA-D69556E8C1F3", "Fuchsia guid-test")
    AddType(0xf115, "A13B4D9A-EC5F-11E8-97D8-6C3BE52705BF", "Fuchsia verified boot metadata (A)")
    AddType(0xf116, "A288ABF2-EC5F-11E8-97D8-6C3BE52705BF", "Fuchsia verified boot metadata (B)")
    AddType(0xf117, "6A2460C3-CD11-4E8B-80A8-12CCE268ED0A", "Fuchsia verified boot metadata (R)")
    AddType(0xf118, "1D75395D-F2C6-476B-A8B7-45CC1C97B476", "Fuchsia misc")
    AddType(0xf119, "900B0FC5-90CD-4D4F-84F9-9F8ED579DB88", "Fuchsia emmc-boot1")
    AddType(0xf11a, "B2B2E8D1-7C10-4EBC-A2D0-4614568260AD", "Fuchsia emmc-boot2")

    // Ceph type codes; see https://github.com/ceph/ceph/blob/9bcc42a3e6b08521694b5c0228b2c6ed7b3d312e/src/ceph-disk#L76-L81
    // and Wikipedia
    AddType(0xf800, "4FBD7E29-9D25-41B8-AFD0-062C0CEFF05D", "Ceph OSD") // Ceph Object Storage Daemon
    AddType(0xf801, "4FBD7E29-9D25-41B8-AFD0-5EC00CEFF05D", "Ceph dm-crypt OSD") // Ceph Object Storage Daemon (encrypted)
    AddType(0xf802, "45B0969E-9B03-4F30-B4C6-B4B80CEFF106", "Ceph journal")
    AddType(0xf803, "45B0969E-9B03-4F30-B4C6-5EC00CEFF106", "Ceph dm-crypt journal")
    AddType(0xf804, "89C57F98-2FE5-4DC0-89C1-F3AD0CEFF2BE", "Ceph disk in creation")
    AddType(0xf805, "89C57F98-2FE5-4DC0-89C1-5EC00CEFF2BE", "Ceph dm-crypt disk in creation")
    AddType(0xf806, "CAFECAFE-9B03-4F30-B4C6-B4B80CEFF106", "Ceph block")
    AddType(0xf807, "30CD0809-C2B2-499C-8879-2D6B78529876", "Ceph block DB")
    AddType(0xf808, "5CE17FCE-4087-4169-B7FF-056CC58473F9", "Ceph block write-ahead log")
    AddType(0xf809, "FB3AABF9-D25F-47CC-BF5E-721D1816496B", "Ceph lockbox for dm-crypt keys")
    AddType(0xf80a, "4FBD7E29-8AE0-4982-BF9D-5A8D867AF560", "Ceph multipath OSD")
    AddType(0xf80b, "45B0969E-8AE0-4982-BF9D-5A8D867AF560", "Ceph multipath journal")
    AddType(0xf80c, "CAFECAFE-8AE0-4982-BF9D-5A8D867AF560", "Ceph multipath block 1")
    AddType(0xf80d, "7F4A666A-16F3-47A2-8445-152EF4D03F6C", "Ceph multipath block 2")
    AddType(0xf80e, "EC6D6385-E346-45DC-BE91-DA2A7C8B3261", "Ceph multipath block DB")
    AddType(0xf80f, "01B41E1B-002A-453C-9F17-88793989FF8F", "Ceph multipath block write-ahead log")
    AddType(0xf810, "CAFECAFE-9B03-4F30-B4C6-5EC00CEFF106", "Ceph dm-crypt block")
    AddType(0xf811, "93B0052D-02D9-4D8A-A43B-33A3EE4DFBC3", "Ceph dm-crypt block DB")
    AddType(0xf812, "306E8683-4FE2-4330-B7C0-00A917C16966", "Ceph dm-crypt block write-ahead log")
    AddType(0xf813, "45B0969E-9B03-4F30-B4C6-35865CEFF106", "Ceph dm-crypt LUKS journal")
    AddType(0xf814, "CAFECAFE-9B03-4F30-B4C6-35865CEFF106", "Ceph dm-crypt LUKS block")
    AddType(0xf815, "166418DA-C469-4022-ADF4-B30AFD37F176", "Ceph dm-crypt LUKS block DB")
    AddType(0xf816, "86A32090-3647-40B9-BBBD-38D8C573AA86", "Ceph dm-crypt LUKS block write-ahead log")
    AddType(0xf817, "4FBD7E29-9D25-41B8-AFD0-35865CEFF05D", "Ceph dm-crypt LUKS OSD")

    // VMWare ESX partition types codes
    AddType(0xfb00, "AA31E02A-400F-11DB-9590-000C2911D1B8", "VMWare VMFS")
    AddType(0xfb01, "9198EFFC-31C0-11DB-8F78-000C2911D1B8", "VMWare reserved")
    AddType(0xfc00, "9D275380-40AD-11DB-BF97-000C2911D1B8", "VMWare kcore crash protection")

    // A straggler Linux partition type....
    AddType(0xfd00, "A19D880F-05FC-4D3B-A006-743F0F84911E", "Linux RAID")
};
#undef AddType

static inline const gpt_type_entry_t* gpt_type_lookup(uint16_t code)
{
    for (int i = 0; i < ARRAYSIZE(gpt_type_table); i++) {
        if (gpt_type_table[i].code == code)
            return &gpt_type_table[i];
    }
    return NULL;
}

static inline const char* gpt_type_desc(const GUID* guid)
{
    const char* guid_str = GuidToString(guid, TRUE);
    if (guid_str == NULL)
        return NULL;
    for (int i = 0; i < ARRAYSIZE(gpt_type_table); i++) {
        if (_strnicmp(&guid_str[1], gpt_type_table[i].guid_str, 36) == 0)
            return gpt_type_table[i].description;
    }
    return NULL;
}

static inline const char* gpt_type_guid_str(uint16_t code)
{
    const gpt_type_entry_t* e = gpt_type_lookup(code);
    return (e != NULL) ? e->guid_str : NULL;
}

static inline const char* gpt_type_description(uint16_t code)
{
    const gpt_type_entry_t* e = gpt_type_lookup(code);
    return (e != NULL) ? e->description : NULL;
}

static inline GUID gpt_type_guid(uint16_t code)
{
    return StringToGuid(gpt_type_guid_str(code));
}

// Also redefine the constant GUIDs we use in the application
DEFINE_GUID(PARTITION_GENERIC_ESP, 0xC12A7328, 0xF81F, 0x11D2, 0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B);
DEFINE_GUID(PARTITION_LINUX_DATA, 0x0FC63DAF, 0x8483, 0x4772, 0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4);
DEFINE_GUID(PARTITION_MICROSOFT_DATA, 0xEBD0A0A2, 0xB9E5, 0x4433, 0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7);
DEFINE_GUID(PARTITION_MICROSOFT_RESERVED, 0xE3C9E316, 0x0B5C, 0x4DB8, 0x81, 0x7D, 0xF9, 0x2D, 0xF0, 0x02, 0x15, 0xAE);
