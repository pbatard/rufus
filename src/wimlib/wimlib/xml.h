#ifndef _WIMLIB_XML_H
#define _WIMLIB_XML_H

#include "wimlib/types.h"

/*****************************************************************************/

struct wim_xml_info;

struct wim_xml_info *
xml_new_info_struct(void);

void
xml_free_info_struct(struct wim_xml_info *info);

/*****************************************************************************/

int
xml_get_image_count(const struct wim_xml_info *info);

u64
xml_get_total_bytes(const struct wim_xml_info *info);

u64
xml_get_image_total_bytes(const struct wim_xml_info *info, int image);

u64
xml_get_image_hard_link_bytes(const struct wim_xml_info *info, int image);

bool
xml_get_wimboot(const struct wim_xml_info *info, int image);

u64
xml_get_windows_build_number(const struct wim_xml_info *info, int image);

int
xml_set_wimboot(struct wim_xml_info *info, int image);

/*****************************************************************************/

int
xml_update_image_info(WIMStruct *wim, int image);

int
xml_add_image(struct wim_xml_info *info, const tchar *name);

int
xml_export_image(const struct wim_xml_info *src_info, int src_image,
		 struct wim_xml_info *dest_info, const tchar *dest_image_name,
		 const tchar *dest_image_description, bool wimboot);

void
xml_delete_image(struct wim_xml_info *info, int image);


void
xml_print_image_info(struct wim_xml_info *info, int image);

/*****************************************************************************/

struct wim_reshdr;

#define WIM_TOTALBYTES_USE_EXISTING  ((u64)(-1))
#define WIM_TOTALBYTES_OMIT          ((u64)(-2))

int
read_wim_xml_data(WIMStruct *wim);

int
write_wim_xml_data(WIMStruct *wim, int image,
		   u64 total_bytes, struct wim_reshdr *out_reshdr,
		   int write_resource_flags);

#endif /* _WIMLIB_XML_H */
