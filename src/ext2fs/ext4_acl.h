/*
 * Ext4's on-disk acl format.  From linux/fs/ext4/acl.h
 */

#define EXT4_ACL_VERSION        0x0001

/* 23.2.5 acl_tag_t values */

#define ACL_UNDEFINED_TAG	(0x00)
#define ACL_USER_OBJ		(0x01)
#define ACL_USER		(0x02)
#define ACL_GROUP_OBJ		(0x04)
#define ACL_GROUP		(0x08)
#define ACL_MASK		(0x10)
#define ACL_OTHER		(0x20)

/* 23.3.6 acl_type_t values */

#define ACL_TYPE_ACCESS		(0x8000)
#define ACL_TYPE_DEFAULT	(0x4000)

/* 23.2.7 ACL qualifier constants */

#define ACL_UNDEFINED_ID	((id_t)-1)

typedef struct {
        __le16          e_tag;
        __le16          e_perm;
        __le32          e_id;
 } ext4_acl_entry;
 
typedef struct {
        __le16          e_tag;
        __le16          e_perm;
} ext4_acl_entry_short;

typedef struct {
         __le32          a_version;
} ext4_acl_header;


/* Supported ACL a_version fields */
 #define POSIX_ACL_XATTR_VERSION 0x0002

typedef struct {
        __le16                  e_tag;
        __le16                  e_perm;
        __le32                  e_id;
} posix_acl_xattr_entry;

typedef struct {
        __le32                  a_version;
#if __GNUC_PREREQ (4, 8)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
        posix_acl_xattr_entry   a_entries[0];
#if __GNUC_PREREQ (4, 8)
#pragma GCC diagnostic pop
#endif
} posix_acl_xattr_header;

