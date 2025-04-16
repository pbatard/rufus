/*
 * security_descriptor.h - declarations for Windows security descriptor format
 *
 * Copyright 2022 Eric Biggers
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef _WIMLIB_SECURITY_DESCRIPTOR_H
#define _WIMLIB_SECURITY_DESCRIPTOR_H

#include "wimlib/compiler.h"
#include "wimlib/types.h"

/* Note: the data types in this header are prefixed with wimlib_ to avoid
 * conflicts with the same types being defined in the libntfs-3g headers.  */

/* Windows NT security descriptor, in self-relative format  */
typedef struct {
	/* Security descriptor revision; should be 1  */
	u8 revision;

	/* Padding  */
	u8 sbz1;

	/* Bitwise OR of flags defined below, such as SE_DACL_PRESENT  */
	le16 control;

	/* Offset of owenr SID structure in the security descriptor  */
	le32 owner_offset;

	/* Offset of group SID structure in the security descriptor  */
	le32 group_offset;

	/* Offset of System Access Control List (SACL) in security descriptor,
	 * or 0 if no SACL is present  */
	le32 sacl_offset;

	/* Offset of Discretionary Access Control List (DACL) in security
	 * descriptor, or 0 if no DACL is present  */
	le32 dacl_offset;
} __attribute__((packed)) wimlib_SECURITY_DESCRIPTOR_RELATIVE;

#define wimlib_SE_OWNER_DEFAULTED		0x0001
#define wimlib_SE_GROUP_DEFAULTED		0x0002
#define wimlib_SE_DACL_PRESENT			0x0004
#define wimlib_SE_DACL_DEFAULTED		0x0008
#define wimlib_SE_SACL_PRESENT			0x0010
#define wimlib_SE_SACL_DEFAULTED		0x0020
#define wimlib_SE_DACL_AUTO_INHERIT_REQ		0x0100
#define wimlib_SE_SACL_AUTO_INHERIT_REQ		0x0200
#define wimlib_SE_DACL_AUTO_INHERITED		0x0400
#define wimlib_SE_SACL_AUTO_INHERITED		0x0800
#define wimlib_SE_DACL_PROTECTED		0x1000
#define wimlib_SE_SACL_PROTECTED		0x2000
#define wimlib_SE_RM_CONTROL_VALID		0x4000
#define wimlib_SE_SELF_RELATIVE			0x8000

/* Windows NT security identifier (user or group)  */
typedef struct {

	u8  revision;
	u8  sub_authority_count;

	/* Identifies the authority that issued the SID  */
	u8  identifier_authority[6];

	le32 sub_authority[];
} __attribute__((packed)) wimlib_SID;

/* Header of a Windows NT access control list  */
typedef struct {
	/* ACL_REVISION or ACL_REVISION_DS */
	u8 revision;

	/* padding  */
	u8 sbz1;

	/* Total size of the ACL, including all access control entries  */
	le16 acl_size;

	/* Number of access control entry structures that follow the ACL
	 * structure  */
	le16 ace_count;

	/* padding  */
	le16 sbz2;
} __attribute__((packed)) wimlib_ACL;

#define wimlib_ACCESS_ALLOWED_ACE_TYPE		0
#define wimlib_ACCESS_DENIED_ACE_TYPE		1
#define wimlib_SYSTEM_AUDIT_ACE_TYPE		2

/* Header of a Windows NT access control entry  */
typedef struct {
	/* Type of ACE  */
	u8 type;

	/* Bitwise OR of inherit ACE flags  */
	u8 flags;

	/* Size of the access control entry, including this header  */
	le16 size;
} __attribute__((packed)) wimlib_ACE_HEADER;

/* Windows NT access control entry to grant rights to a user or group  */
typedef struct {
	wimlib_ACE_HEADER hdr;
	le32 mask;
	wimlib_SID sid;
} __attribute__((packed)) wimlib_ACCESS_ALLOWED_ACE;

/* Windows NT access control entry to deny rights to a user or group  */
typedef struct {
	wimlib_ACE_HEADER hdr;
	le32 mask;
	wimlib_SID sid;
} __attribute__((packed)) wimlib_ACCESS_DENIED_ACE;

/* Windows NT access control entry to audit access to the object  */
typedef struct {
	wimlib_ACE_HEADER hdr;
	le32 mask;
	wimlib_SID sid;
} __attribute__((packed)) wimlib_SYSTEM_AUDIT_ACE;

#endif /* _WIMLIB_SECURITY_DESCRIPTOR_H */
