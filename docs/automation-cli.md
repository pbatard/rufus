# Rufus automation command-line interface

This document specifies the opt-in automation interface proposed for Rufus.
Normal GUI operation and the existing `-i`/`--iso` startup option are
unchanged.

## Disk inventory

`rufus.exe --list-disks` starts Rufus's normal device discovery and writes a
tab-separated inventory to standard output:

```text
PhysicalDrive  label  model  size_bytes
PhysicalDrive3 USB    Example Flash Disk  16008609792
```

The inventory contains only devices Rufus would present in its device list.
Use `--force --list-disks` to include fixed USB disks. The command never
writes to a disk and exits with status `0` after discovery.

## Flash operation

An unattended flash requires all of the following:

```text
rufus.exe --auto --image IMAGE --disk SELECTOR --yes [OPTIONS]
```

`SELECTOR` is a physical disk number (`3`) or a physical-drive name
(`PhysicalDrive3`). The target must be present in Rufus's device list. The
`--yes` option is intentionally mandatory for a real unattended write; use
`--dry-run` instead when validating a job without writing.

Options:

| Option | Meaning |
| --- | --- |
| `--image PATH`, `--iso PATH` | Image to validate and flash |
| `--disk SELECTOR` | Physical disk selected after discovery |
| `--mode iso\|dd` | Select Rufus image mode |
| `--filesystem NAME` | Preselect a filesystem when formatting |
| `--label TEXT` | Set the target volume label |
| `--quiet` | Hide the GUI while the job runs |
| `--dry-run` | Validate the image and target without writing |
| `--force` | Include fixed USB disks and disable the image-size guard |
| `--yes` | Accept the destructive-operation confirmations |

`--force` does not bypass Rufus image validation, device discovery, write
errors, or other safety checks. Callers should obtain the inventory first and
verify the selected physical drive before passing `--yes`.

## Configuration files

`--config PATH` loads the existing Rufus INI-style `key=value` format. It
uses Rufus's existing token parser, supports the same text encodings as the
rest of the application, and accepts the keys `image`, `disk`, `auto`,
`yes`, `quiet`, `dry-run`, `force`, `list-disks`, `mode`,
`filesystem`, and `label`.

Command-line options are applied after the file and therefore take precedence.
For example:

```text
image=C:\\images\\bootstrap.img.gz
disk=3
auto=true
yes=true
mode=dd
quiet=true
```

```text
rufus.exe --config flash.conf
```

Exit status:

| Status | Meaning |
| --- | --- |
| `0` | Flash completed, or validation/listing completed successfully |
| `1` | Image validation, target selection, or flash operation failed |
| `2` | Automation arguments or configuration could not be accepted |

## Release packaging

The normal Rufus release workflow remains authoritative for release builds.
When a maintainer pushes a version tag, it produces the standalone x86, x64,
and ARM64 executables using the repository's existing naming and compression
steps. A fork build is for testing and must not be presented as an official
Rufus release.
