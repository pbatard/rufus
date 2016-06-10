/*
 * Rufus: The Reliable USB Formatting Utility
 * SMART HDD vs Flash detection (using ATA over USB, S.M.A.R.T., etc.)
 * Copyright © 2013-2014 Pete Batard <pete@akeo.ie>
 *
 * Based in part on Smartmontools: http://smartmontools.sourceforge.net
 * Copyright © 2006-12 Douglas Gilbert <dgilbert@interlog.com>
 * Copyright © 2009-13 Christian Franke <smartmontools-support@lists.sourceforge.net>
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

// From http://stackoverflow.com/a/9284679
#define COMPILE_TIME_ASSERT(pred)       switch(0) {case 0: case pred:;}

// Official commands
#define ATA_DATA_SET_MANAGEMENT         0x06	// TRIM command for SSDs
#define ATA_READ_LOG_EXT                0x2f
#define ATA_CHECK_POWER_MODE            0xe5
#define ATA_IDENTIFY_DEVICE             0xec
#define ATA_IDENTIFY_PACKET_DEVICE      0xa1
#define ATA_IDLE                        0xe3
#define ATA_SMART_CMD                   0xb0
#define ATA_SECURITY_FREEZE_LOCK        0xf5
#define ATA_SET_FEATURES                0xef
#define ATA_STANDBY_IMMEDIATE           0xe0
#define SAT_ATA_PASSTHROUGH_12          0xa1
// Non official pseudo commands
#define USB_CYPRESS_ATA_PASSTHROUGH     0x24
#define USB_JMICRON_ATA_PASSTHROUGH     0xdf
#define USB_SUNPLUS_ATA_PASSTHROUGH     0xf8

// SMART ATA Subcommands
// Also see https://github.com/gregkh/ndas/blob/master/udev.c
#define ATA_SMART_READ_VALUES           0xd0
#define ATA_SMART_READ_THRESHOLDS       0xd1
#define ATA_SMART_AUTOSAVE              0xd2
#define ATA_SMART_SAVE                  0xd3
#define ATA_SMART_IMMEDIATE_OFFLINE     0xd4
#define ATA_SMART_READ_LOG_SECTOR       0xd5
#define ATA_SMART_WRITE_LOG_SECTOR      0xd6
#define ATA_SMART_WRITE_THRESHOLDS      0xd7
#define ATA_SMART_ENABLE                0xd8
#define ATA_SMART_DISABLE               0xd9
#define ATA_SMART_STATUS                0xda

#define SCSI_IOCTL_DATA_OUT             0
#define SCSI_IOCTL_DATA_IN              1
#define SCSI_IOCTL_DATA_UNSPECIFIED     2

#define ATA_PASSTHROUGH_DATA_OUT        SCSI_IOCTL_DATA_OUT
#define ATA_PASSTHROUGH_DATA_IN         SCSI_IOCTL_DATA_IN
#define ATA_PASSTHROUGH_DATA_NONE       SCSI_IOCTL_DATA_UNSPECIFIED

// Status codes returned by ScsiPassthroughDirect()
#define SPT_SUCCESS                     0
#define SPT_ERROR_CDB_LENGTH            -1
#define SPT_ERROR_BUFFER                -2
#define SPT_ERROR_DIRECTION             -3
#define SPT_ERROR_EXTENDED_CDB          -4
#define SPT_ERROR_CDB_OPCODE            -5
#define SPT_ERROR_TIMEOUT               -6
#define SPT_ERROR_INVALID_PARAMETER     -7
#define SPT_ERROR_CHECK_STATUS          -8
#define SPT_ERROR_UNKNOWN_ERROR         -99

#define SPT_CDB_LENGTH                  16
#define SPT_SENSE_LENGTH                32
#define SPT_TIMEOUT_VALUE               2	// In seconds
#define SECTOR_SIZE_SHIFT_BIT           9	// We use 512 bytes sectors always

#define IOCTL_SCSI_BASE                 FILE_DEVICE_CONTROLLER
#define IOCTL_SCSI_PASS_THROUGH         \
	CTL_CODE(IOCTL_SCSI_BASE, 0x0401, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_SCSI_PASS_THROUGH_DIRECT  \
	CTL_CODE(IOCTL_SCSI_BASE, 0x0405, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)

typedef struct {
	USHORT Length;
	UCHAR  ScsiStatus;
	UCHAR  PathId;
	UCHAR  TargetId;
	UCHAR  Lun;
	UCHAR  CdbLength;
	UCHAR  SenseInfoLength;
	UCHAR  DataIn;
	ULONG  DataTransferLength;
	ULONG  TimeOutValue;
	ULONG_PTR DataBufferOffset;
	ULONG  SenseInfoOffset;
	UCHAR  Cdb[SPT_CDB_LENGTH];
} SCSI_PASS_THROUGH;

typedef struct {
	USHORT Length;
	UCHAR  ScsiStatus;
	UCHAR  PathId;
	UCHAR  TargetId;
	UCHAR  Lun;
	UCHAR  CdbLength;
	UCHAR  SenseInfoLength;
	UCHAR  DataIn;
	ULONG  DataTransferLength;
	ULONG  TimeOutValue;
	PVOID  DataBuffer;
	ULONG  SenseInfoOffset;
	UCHAR  Cdb[SPT_CDB_LENGTH];
} SCSI_PASS_THROUGH_DIRECT;

typedef struct {
	SCSI_PASS_THROUGH_DIRECT sptd;
	ULONG Align;
	UCHAR SenseBuf[SPT_SENSE_LENGTH];
} SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER;

// Custom ATA over USB command
typedef struct {
	uint8_t AtaCmd;			// eg: ATA_SMART_CMD = 0xb0, IDENTIFY = 0xec, etc.
	uint8_t Features;		// SMART subcommand, eg: SMART_ENABLE_OPS = 0xd8, etc.
	uint8_t Device;			// 0x00 for Identify, 0xA0, 0xB0 for JMicron/SAT SMART ops
	uint8_t Align;
	uint8_t Lba_low;		// LBA
	uint8_t Lba_mid;
	uint8_t Lba_high;
	uint8_t Lba_unused;
} ATA_PASSTHROUGH_CMD;

typedef BOOL (*AtaPassthroughFn_t)(
	HANDLE hPhysical,
	ATA_PASSTHROUGH_CMD* Command,
	void* DataBuffer,
	size_t BufLen,
	uint32_t Timeout
);

typedef struct {
	AtaPassthroughFn_t fn;
	const char* type;
} AtaPassThroughType;

// From http://msdn.microsoft.com/en-us/library/windows/hardware/ff559006.aspx
#pragma pack(1)		// Packed as the size must be 512 bytes exactly
typedef struct _IDENTIFY_DEVICE_DATA {
	struct {
		USHORT Reserved1 :1;
		USHORT Retired3 :1;
		USHORT ResponseIncomplete :1;
		USHORT Retired2 :3;
		USHORT FixedDevice :1;
		USHORT RemovableMedia :1;
		USHORT Retired1 :7;
		USHORT DeviceType :1;
	} GeneralConfiguration;
	USHORT NumCylinders;
	USHORT ReservedWord2;
	USHORT NumHeads;
	USHORT Retired1[2];
	USHORT NumSectorsPerTrack;
	USHORT VendorUnique1[3];
	UCHAR  SerialNumber[20];
	USHORT Retired2[2];
	USHORT Obsolete1;
	UCHAR  FirmwareRevision[8];
	UCHAR  ModelNumber[40];
	UCHAR  MaximumBlockTransfer;
	UCHAR  VendorUnique2;
	USHORT ReservedWord48;
	struct {
		UCHAR  ReservedByte49;
		UCHAR  DmaSupported :1;
		UCHAR  LbaSupported :1;
		UCHAR  IordyDisable :1;
		UCHAR  IordySupported :1;
		UCHAR  Reserved1 :1;
		UCHAR  StandybyTimerSupport :1;
		UCHAR  Reserved2 :2;
		USHORT ReservedWord50;
	} Capabilities;
	USHORT ObsoleteWords51[2];
	USHORT TranslationFieldsValid :3;
	USHORT Reserved3 :13;
	USHORT NumberOfCurrentCylinders;
	USHORT NumberOfCurrentHeads;
	USHORT CurrentSectorsPerTrack;
	ULONG  CurrentSectorCapacity;
	UCHAR  CurrentMultiSectorSetting;
	UCHAR  MultiSectorSettingValid :1;
	UCHAR  ReservedByte59 :7;
	ULONG  UserAddressableSectors;
	USHORT ObsoleteWord62;
	USHORT MultiWordDMASupport :8;
	USHORT MultiWordDMAActive :8;
	USHORT AdvancedPIOModes :8;
	USHORT ReservedByte64 :8;
	USHORT MinimumMWXferCycleTime;
	USHORT RecommendedMWXferCycleTime;
	USHORT MinimumPIOCycleTime;
	USHORT MinimumPIOCycleTimeIORDY;
	USHORT ReservedWords69[6];
	USHORT QueueDepth :5;
	USHORT ReservedWord75 :11;
	USHORT ReservedWords76[4];
	USHORT MajorRevision;
	USHORT MinorRevision;
	struct {
		USHORT SmartCommands :1;
		USHORT SecurityMode :1;
		USHORT RemovableMediaFeature :1;
		USHORT PowerManagement :1;
		USHORT Reserved1 :1;
		USHORT WriteCache :1;
		USHORT LookAhead :1;
		USHORT ReleaseInterrupt :1;
		USHORT ServiceInterrupt :1;
		USHORT DeviceReset :1;
		USHORT HostProtectedArea :1;
		USHORT Obsolete1 :1;
		USHORT WriteBuffer :1;
		USHORT ReadBuffer :1;
		USHORT Nop :1;
		USHORT Obsolete2 :1;
		USHORT DownloadMicrocode :1;
		USHORT DmaQueued :1;
		USHORT Cfa :1;
		USHORT AdvancedPm :1;
		USHORT Msn :1;
		USHORT PowerUpInStandby :1;
		USHORT ManualPowerUp :1;
		USHORT Reserved2 :1;
		USHORT SetMax :1;
		USHORT Acoustics :1;
		USHORT BigLba :1;
		USHORT DeviceConfigOverlay :1;
		USHORT FlushCache :1;
		USHORT FlushCacheExt :1;
		USHORT Resrved3 :2;
		USHORT SmartErrorLog :1;
		USHORT SmartSelfTest :1;
		USHORT MediaSerialNumber :1;
		USHORT MediaCardPassThrough :1;
		USHORT StreamingFeature :1;
		USHORT GpLogging :1;
		USHORT WriteFua :1;
		USHORT WriteQueuedFua :1;
		USHORT WWN64Bit :1;
		USHORT URGReadStream :1;
		USHORT URGWriteStream :1;
		USHORT ReservedForTechReport :2;
		USHORT IdleWithUnloadFeature :1;
		USHORT Reserved4 :2;
	} CommandSetSupport;
	struct {
		USHORT SmartCommands :1;
		USHORT SecurityMode :1;
		USHORT RemovableMediaFeature :1;
		USHORT PowerManagement :1;
		USHORT Reserved1 :1;
		USHORT WriteCache :1;
		USHORT LookAhead :1;
		USHORT ReleaseInterrupt :1;
		USHORT ServiceInterrupt :1;
		USHORT DeviceReset :1;
		USHORT HostProtectedArea :1;
		USHORT Obsolete1 :1;
		USHORT WriteBuffer :1;
		USHORT ReadBuffer :1;
		USHORT Nop :1;
		USHORT Obsolete2 :1;
		USHORT DownloadMicrocode :1;
		USHORT DmaQueued :1;
		USHORT Cfa :1;
		USHORT AdvancedPm :1;
		USHORT Msn :1;
		USHORT PowerUpInStandby :1;
		USHORT ManualPowerUp :1;
		USHORT Reserved2 :1;
		USHORT SetMax :1;
		USHORT Acoustics :1;
		USHORT BigLba :1;
		USHORT DeviceConfigOverlay :1;
		USHORT FlushCache :1;
		USHORT FlushCacheExt :1;
		USHORT Resrved3 :2;
		USHORT SmartErrorLog :1;
		USHORT SmartSelfTest :1;
		USHORT MediaSerialNumber :1;
		USHORT MediaCardPassThrough :1;
		USHORT StreamingFeature :1;
		USHORT GpLogging :1;
		USHORT WriteFua :1;
		USHORT WriteQueuedFua :1;
		USHORT WWN64Bit :1;
		USHORT URGReadStream :1;
		USHORT URGWriteStream :1;
		USHORT ReservedForTechReport :2;
		USHORT IdleWithUnloadFeature :1;
		USHORT Reserved4 :2;
	} CommandSetActive;
	USHORT UltraDMASupport :8;
	USHORT UltraDMAActive :8;
	USHORT ReservedWord89[4];
	USHORT HardwareResetResult;
	USHORT CurrentAcousticValue :8;
	USHORT RecommendedAcousticValue :8;
	USHORT ReservedWord95[5];
	ULONG  Max48BitLBA[2];
	USHORT StreamingTransferTime;
	USHORT ReservedWord105;
	struct {
		USHORT LogicalSectorsPerPhysicalSector :4;
		USHORT Reserved0 :8;
		USHORT LogicalSectorLongerThan256Words :1;
		USHORT MultipleLogicalSectorsPerPhysicalSector :1;
		USHORT Reserved1 :2;
	} PhysicalLogicalSectorSize;
	USHORT InterSeekDelay;
	USHORT WorldWideName[4];
	USHORT ReservedForWorldWideName128[4];
	USHORT ReservedForTlcTechnicalReport;
	USHORT WordsPerLogicalSector[2];
	struct {
		USHORT ReservedForDrqTechnicalReport :1;
		USHORT WriteReadVerifySupported :1;
		USHORT Reserved01 :11;
		USHORT Reserved1 :2;
	} CommandSetSupportExt;
	struct {
		USHORT ReservedForDrqTechnicalReport :1;
		USHORT WriteReadVerifyEnabled :1;
		USHORT Reserved01 :11;
		USHORT Reserved1 :2;
	} CommandSetActiveExt;
	USHORT ReservedForExpandedSupportandActive[6];
	USHORT MsnSupport :2;
	USHORT ReservedWord1274 :14;
	struct {
		USHORT SecuritySupported :1;
		USHORT SecurityEnabled :1;
		USHORT SecurityLocked :1;
		USHORT SecurityFrozen :1;
		USHORT SecurityCountExpired :1;
		USHORT EnhancedSecurityEraseSupported :1;
		USHORT Reserved0 :2;
		USHORT SecurityLevel :1;
		USHORT Reserved1 :7;
	} SecurityStatus;
	USHORT ReservedWord129[31];
	struct {
		USHORT MaximumCurrentInMA2 :12;
		USHORT CfaPowerMode1Disabled :1;
		USHORT CfaPowerMode1Required :1;
		USHORT Reserved0 :1;
		USHORT Word160Supported :1;
	} CfaPowerModel;
	USHORT ReservedForCfaWord161[8];
	struct {
		USHORT SupportsTrim :1;
		USHORT Reserved0 :15;
	} DataSetManagementFeature;
	USHORT ReservedForCfaWord170[6];
	USHORT CurrentMediaSerialNumber[30];
	USHORT ReservedWord206;
	USHORT ReservedWord207[2];
	struct {
		USHORT AlignmentOfLogicalWithinPhysical :14;
		USHORT Word209Supported :1;
		USHORT Reserved0 :1;
	} BlockAlignment;
	USHORT WriteReadVerifySectorCountMode3Only[2];
	USHORT WriteReadVerifySectorCountMode2Only[2];
	struct {
		USHORT NVCachePowerModeEnabled :1;
		USHORT Reserved0 :3;
		USHORT NVCacheFeatureSetEnabled :1;
		USHORT Reserved1 :3;
		USHORT NVCachePowerModeVersion :4;
		USHORT NVCacheFeatureSetVersion :4;
	} NVCacheCapabilities;
	USHORT NVCacheSizeLSW;
	USHORT NVCacheSizeMSW;
	USHORT NominalMediaRotationRate;
	USHORT ReservedWord218;
	struct {
		UCHAR NVCacheEstimatedTimeToSpinUpInSeconds;
		UCHAR Reserved;
	} NVCacheOptions;
	USHORT ReservedWord220[35];
	USHORT Signature :8;
	USHORT CheckSum :8;
} IDENTIFY_DEVICE_DATA, *PIDENTIFY_DEVICE_DATA;
#pragma pack()
