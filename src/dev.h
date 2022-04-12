/*
 * Rufus: The Reliable USB Formatting Utility
 * Device detection and enumeration
 * Copyright © 2014-2019 Pete Batard <pete@akeo.ie>
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
#include <cfgmgr32.h>

#define USB_SPEED_UNKNOWN			0
#define USB_SPEED_LOW				1
#define USB_SPEED_FULL				2
#define USB_SPEED_HIGH				3
#define USB_SPEED_SUPER				4
#define USB_SPEED_SUPER_PLUS		5
#define USB_SPEED_MAX				6

/* List of the properties we are interested in */
typedef struct usb_device_props {
	uint32_t  vid;
	uint32_t  pid;
	uint32_t  speed;
	uint32_t  lower_speed;
	uint32_t  port;
	BOOLEAN   is_USB;
	BOOLEAN   is_SCSI;
	BOOLEAN   is_CARD;
	BOOLEAN   is_UASP;
	BOOLEAN   is_VHD;
	BOOLEAN   is_Removable;
} usb_device_props;

/*
 * Windows DDK API definitions. Most of it copied from MinGW's includes
 */
typedef DWORD DEVNODE, DEVINST;
typedef DEVNODE *PDEVNODE, *PDEVINST;
typedef DWORD RETURN_TYPE;
typedef RETURN_TYPE CONFIGRET;
typedef CHAR *DEVINSTID_A;

#ifndef CM_GETIDLIST_FILTER_PRESENT
#define CM_GETIDLIST_FILTER_PRESENT                 0x00000100
#endif

#ifndef FILE_DEVICE_USB
#define FILE_DEVICE_USB                         FILE_DEVICE_UNKNOWN
#endif

typedef enum USB_CONNECTION_STATUS {
	NoDeviceConnected,
	DeviceConnected,
	DeviceFailedEnumeration,
	DeviceGeneralFailure,
	DeviceCausedOvercurrent,
	DeviceNotEnoughPower,
	DeviceNotEnoughBandwidth,
	DeviceHubNestedTooDeeply,
	DeviceInLegacyHub
} USB_CONNECTION_STATUS, *PUSB_CONNECTION_STATUS;

typedef enum USB_HUB_NODE {
	UsbHub,
	UsbMIParent
} USB_HUB_NODE;

/* Cfgmgr32.dll interface */
DECLSPEC_IMPORT CONFIGRET WINAPI CM_Get_Device_IDA(DEVINST dnDevInst, CHAR* Buffer, ULONG BufferLen, ULONG ulFlags);
DECLSPEC_IMPORT CONFIGRET WINAPI CM_Get_Device_ID_List_SizeA(PULONG pulLen, PCSTR pszFilter, ULONG ulFlags);
DECLSPEC_IMPORT CONFIGRET WINAPI CM_Get_Device_ID_ListA(PCSTR pszFilter, PCHAR Buffer, ULONG BufferLen, ULONG ulFlags);
DECLSPEC_IMPORT CONFIGRET WINAPI CM_Locate_DevNodeA(PDEVINST pdnDevInst, DEVINSTID_A pDeviceID, ULONG ulFlags);
DECLSPEC_IMPORT CONFIGRET WINAPI CM_Get_Child(PDEVINST pdnDevInst, DEVINST dnDevInst, ULONG ulFlags);
DECLSPEC_IMPORT CONFIGRET WINAPI CM_Get_Parent(PDEVINST pdnDevInst, DEVINST dnDevInst, ULONG ulFlags);
DECLSPEC_IMPORT CONFIGRET WINAPI CM_Get_Sibling(PDEVINST pdnDevInst, DEVINST dnDevInst, ULONG ulFlags);
DECLSPEC_IMPORT CONFIGRET WINAPI CM_Get_DevNode_Status(PULONG pulStatus, PULONG pulProblemNumber, DEVINST dnDevInst, ULONG ulFlags);

#define USB_HUB_CYCLE_PORT                        273
#define USB_GET_NODE_CONNECTION_INFORMATION_EX    274
#define USB_GET_NODE_CONNECTION_INFORMATION_EX_V2 279

#define IOCTL_USB_HUB_CYCLE_PORT \
  CTL_CODE(FILE_DEVICE_USB, USB_HUB_CYCLE_PORT, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX \
  CTL_CODE(FILE_DEVICE_USB, USB_GET_NODE_CONNECTION_INFORMATION_EX, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX_V2 \
  CTL_CODE(FILE_DEVICE_USB, USB_GET_NODE_CONNECTION_INFORMATION_EX_V2, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* Most of the structures below need to be packed */
#pragma pack(push, 1)

typedef struct _USB_DEVICE_DESCRIPTOR {
	UCHAR  bLength;
	UCHAR  bDescriptorType;
	USHORT bcdUSB;
	UCHAR  bDeviceClass;
	UCHAR  bDeviceSubClass;
	UCHAR  bDeviceProtocol;
	UCHAR  bMaxPacketSize0;
	USHORT idVendor;
	USHORT idProduct;
	USHORT bcdDevice;
	UCHAR  iManufacturer;
	UCHAR  iProduct;
	UCHAR  iSerialNumber;
	UCHAR  bNumConfigurations;
} USB_DEVICE_DESCRIPTOR, *PUSB_DEVICE_DESCRIPTOR;

typedef struct USB_NODE_CONNECTION_INFORMATION_EX {
	ULONG ConnectionIndex;
	USB_DEVICE_DESCRIPTOR DeviceDescriptor;
	UCHAR CurrentConfigurationValue;
	UCHAR Speed;
	BOOLEAN DeviceIsHub;
	USHORT DeviceAddress;
	ULONG NumberOfOpenPipes;
	USB_CONNECTION_STATUS ConnectionStatus;
//	USB_PIPE_INFO PipeList[0];
} USB_NODE_CONNECTION_INFORMATION_EX, *PUSB_NODE_CONNECTION_INFORMATION_EX;

typedef union _USB_PROTOCOLS {
	ULONG  ul;
	struct {
		ULONG Usb110:1;
		ULONG Usb200:1;
		ULONG Usb300:1;
		ULONG ReservedMBZ:29;
	};
} USB_PROTOCOLS, *PUSB_PROTOCOLS;

typedef union _USB_NODE_CONNECTION_INFORMATION_EX_V2_FLAGS {
	ULONG ul;
	struct {
		ULONG DeviceIsOperatingAtSuperSpeedOrHigher:1;
		ULONG DeviceIsSuperSpeedCapableOrHigher:1;
		ULONG DeviceIsOperatingAtSuperSpeedPlusOrHigher : 1;
		ULONG DeviceIsSuperSpeedPlusCapableOrHigher : 1;
		ULONG ReservedMBZ:28;
	};
} USB_NODE_CONNECTION_INFORMATION_EX_V2_FLAGS, *PUSB_NODE_CONNECTION_INFORMATION_EX_V2_FLAGS;

typedef struct _USB_NODE_CONNECTION_INFORMATION_EX_V2 {
	ULONG ConnectionIndex;
	ULONG Length;
	USB_PROTOCOLS SupportedUsbProtocols;
	USB_NODE_CONNECTION_INFORMATION_EX_V2_FLAGS Flags;
} USB_NODE_CONNECTION_INFORMATION_EX_V2, *PUSB_NODE_CONNECTION_INFORMATION_EX_V2;

typedef struct {
	ULONG ConnectionIndex;
	ULONG StatusReturned;
} USB_CYCLE_PORT_PARAMS;

#pragma pack(pop)

const GUID GUID_DEVINTERFACE_USB_HUB =
	{ 0xf18a0e88L, 0xc30c, 0x11d0, {0x88, 0x15, 0x00, 0xa0, 0xc9, 0x06, 0xbe, 0xd8} };

#define DEVID_HTAB_SIZE		257
