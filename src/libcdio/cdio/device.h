/* -*- c -*-

    Copyright (C) 2005-2006, 2008-2013 Rocky Bernstein <rocky@gnu.org>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/**
 *  \file device.h
 *
 *  \brief C header for driver- or device-related libcdio
 *          calls.  ("device" includes CD-image reading devices).
 */
#ifndef CDIO_DEVICE_H_
#define CDIO_DEVICE_H_

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <cdio/types.h>
#include <cdio/cdio.h>

  /** The type of an drive capability bit mask. See below for values*/
  typedef uint32_t cdio_drive_read_cap_t;
  typedef uint32_t cdio_drive_write_cap_t;
  typedef uint32_t cdio_drive_misc_cap_t;

  /**
    \brief Drive capability bits returned by cdio_get_drive_cap()
    NOTE: Setting a bit here means the presence of a capability.
  */

  /** Miscellaneous capabilities. */
  typedef enum {
    CDIO_DRIVE_CAP_ERROR             = 0x40000, /**< Error */
    CDIO_DRIVE_CAP_UNKNOWN           = 0x80000, /**< Dunno. It can be on if we
                                                have only partial information
                                                or are not completely certain
                                                */
    CDIO_DRIVE_CAP_MISC_CLOSE_TRAY   = 0x00001, /**< caddy systems can't
                                                     close... */
    CDIO_DRIVE_CAP_MISC_EJECT        = 0x00002, /**< but can eject.  */
    CDIO_DRIVE_CAP_MISC_LOCK         = 0x00004, /**< disable manual eject */
    CDIO_DRIVE_CAP_MISC_SELECT_SPEED = 0x00008, /**< programmable speed */
    CDIO_DRIVE_CAP_MISC_SELECT_DISC  = 0x00010, /**< select disc from
                                                      juke-box */
    CDIO_DRIVE_CAP_MISC_MULTI_SESSION= 0x00020, /**< read sessions>1 */
    CDIO_DRIVE_CAP_MISC_MEDIA_CHANGED= 0x00080, /**< media changed */
    CDIO_DRIVE_CAP_MISC_RESET        = 0x00100, /**< hard reset device */
    CDIO_DRIVE_CAP_MISC_FILE         = 0x20000 /**< drive is really a file,
                                                  i.e a CD file image */
  } cdio_drive_cap_misc_t;

  /** Reading masks.. */
  typedef enum {
    CDIO_DRIVE_CAP_READ_AUDIO        = 0x00001, /**< drive can play CD audio */
    CDIO_DRIVE_CAP_READ_CD_DA        = 0x00002, /**< drive can read CD-DA */
    CDIO_DRIVE_CAP_READ_CD_G         = 0x00004, /**< drive can read CD+G  */
    CDIO_DRIVE_CAP_READ_CD_R         = 0x00008, /**< drive can read CD-R  */
    CDIO_DRIVE_CAP_READ_CD_RW        = 0x00010, /**< drive can read CD-RW */
    CDIO_DRIVE_CAP_READ_DVD_R        = 0x00020, /**< drive can read DVD-R */
    CDIO_DRIVE_CAP_READ_DVD_PR       = 0x00040, /**< drive can read DVD+R */
    CDIO_DRIVE_CAP_READ_DVD_RAM      = 0x00080, /**< drive can read DVD-RAM */
    CDIO_DRIVE_CAP_READ_DVD_ROM      = 0x00100, /**< drive can read DVD-ROM */
    CDIO_DRIVE_CAP_READ_DVD_RW       = 0x00200, /**< drive can read DVD-RW  */
    CDIO_DRIVE_CAP_READ_DVD_RPW      = 0x00400, /**< drive can read DVD+RW  */
    CDIO_DRIVE_CAP_READ_C2_ERRS      = 0x00800, /**< has C2 error correction */
    CDIO_DRIVE_CAP_READ_MODE2_FORM1  = 0x01000, /**< can read mode 2 form 1 */
    CDIO_DRIVE_CAP_READ_MODE2_FORM2  = 0x02000, /**< can read mode 2 form 2 */
    CDIO_DRIVE_CAP_READ_MCN          = 0x04000, /**< can read MCN      */
    CDIO_DRIVE_CAP_READ_ISRC         = 0x08000 /**< can read ISRC     */
  } cdio_drive_cap_read_t;

  /** Writing masks.. */
  typedef enum {
    CDIO_DRIVE_CAP_WRITE_CD_R        = 0x00001, /**< drive can write CD-R */
    CDIO_DRIVE_CAP_WRITE_CD_RW       = 0x00002, /**< drive can write CD-RW */
    CDIO_DRIVE_CAP_WRITE_DVD_R       = 0x00004, /**< drive can write DVD-R */
    CDIO_DRIVE_CAP_WRITE_DVD_PR      = 0x00008, /**< drive can write DVD+R */
    CDIO_DRIVE_CAP_WRITE_DVD_RAM     = 0x00010, /**< drive can write DVD-RAM */
    CDIO_DRIVE_CAP_WRITE_DVD_RW      = 0x00020, /**< drive can write DVD-RW */
    CDIO_DRIVE_CAP_WRITE_DVD_RPW     = 0x00040, /**< drive can write DVD+RW */
    CDIO_DRIVE_CAP_WRITE_MT_RAINIER  = 0x00080, /**< Mount Rainier           */
    CDIO_DRIVE_CAP_WRITE_BURN_PROOF  = 0x00100, /**< burn proof */
    CDIO_DRIVE_CAP_WRITE_CD =
    (CDIO_DRIVE_CAP_WRITE_CD_R | CDIO_DRIVE_CAP_WRITE_CD_RW),
    /**< Has some sort of CD writer ability */

    CDIO_DRIVE_CAP_WRITE_DVD =
    (CDIO_DRIVE_CAP_WRITE_DVD_R | CDIO_DRIVE_CAP_WRITE_DVD_PR
     | CDIO_DRIVE_CAP_WRITE_DVD_RAM | CDIO_DRIVE_CAP_WRITE_DVD_RW
     | CDIO_DRIVE_CAP_WRITE_DVD_RPW ),
    /**< Has some sort of DVD writer ability */

    CDIO_DRIVE_CAP_WRITE =
    (CDIO_DRIVE_CAP_WRITE_CD | CDIO_DRIVE_CAP_WRITE_DVD)
    /**< Has some sort of DVD or CD writing ability */
  } cdio_drive_cap_write_t;

/** Size of fields returned by an INQUIRY command */
  typedef enum {
    CDIO_MMC_HW_VENDOR_LEN   =  8, /**< length of vendor field */
    CDIO_MMC_HW_MODEL_LEN    = 16, /**< length of model field */
    CDIO_MMC_HW_REVISION_LEN =  4  /**< length of revision field */
  } cdio_mmc_hw_len_t;


  /** \brief Structure to return CD vendor, model, and revision-level
      strings obtained via the INQUIRY command  */
  typedef struct cdio_hwinfo
  {
    char psz_vendor  [CDIO_MMC_HW_VENDOR_LEN+1];
    char psz_model   [CDIO_MMC_HW_MODEL_LEN+1];
    char psz_revision[CDIO_MMC_HW_REVISION_LEN+1];
  } cdio_hwinfo_t;


  /** Flags specifying the category of device to open or is opened. */
  typedef enum {
    CDIO_SRC_IS_DISK_IMAGE_MASK = 0x0001, /**< Read source is a CD image. */
    CDIO_SRC_IS_DEVICE_MASK     = 0x0002, /**< Read source is a CD device. */
    CDIO_SRC_IS_SCSI_MASK       = 0x0004, /**< Read source SCSI device. */
    CDIO_SRC_IS_NATIVE_MASK     = 0x0008
  } cdio_src_category_mask_t;


  /**
   * The driver_id_t enumerations may be used to tag a specific driver
   * that is opened or is desired to be opened. Note that this is
   * different than what is available on a given host.
   *
   * Order should not be changed lightly because it breaks the ABI.
   * One is not supposed to iterate over the values, but iterate over the
   * cdio_drivers and cdio_device_drivers arrays.
   *
   * NOTE: IF YOU MODIFY ENUM MAKE SURE INITIALIZATION IN CDIO.C AGREES.
   *
   */
  typedef enum  {
    DRIVER_UNKNOWN, /**< Used as input when we don't care what kind
                         of driver to use. */
    DRIVER_AIX,     /**< AIX driver */
    DRIVER_FREEBSD, /**< FreeBSD driver - includes CAM and ioctl access */
    DRIVER_NETBSD,  /**< NetBSD Driver. */
    DRIVER_LINUX,   /**< GNU/Linux Driver */
    DRIVER_SOLARIS, /**< Sun Solaris Driver */
    DRIVER_OSX,     /**< Apple OSX (or MacOS) Driver */
    DRIVER_WIN32,   /**< Microsoft Windows Driver. Includes ASPI and
                         ioctl access. */
    DRIVER_CDRDAO,  /**< cdrdao format CD image. This is listed
                         before BIN/CUE, to make the code prefer cdrdao
                         over BIN/CUE when both exist. */
    DRIVER_BINCUE,  /**< CDRWIN BIN/CUE format CD image. This is
                         listed before NRG, to make the code prefer
                         BIN/CUE over NRG when both exist. */
    DRIVER_NRG,     /**< Nero NRG format CD image. */
    DRIVER_DEVICE   /**< Is really a set of the above; should come last */
  } driver_id_t;

  /**
      A null-terminated (that is DRIVER_UNKNOWN-terminated) ordered (in
      order of preference) array of drivers.
  */
  extern const driver_id_t cdio_drivers[];

  /**
     A null-terminated (that is DRIVER_UNKNOWN-terminated) ordered (in
     order of preference) array of device drivers.
  */
  extern const driver_id_t cdio_device_drivers[];

  /**
      There will generally be only one hardware for a given
      build/platform from the list above. You can use the variable
      below to determine which you've got. If the build doesn't make an
      hardware driver, then the value will be DRIVER_UNKNOWN.
  */
  extern const driver_id_t cdio_os_driver;


  /**
      The following are status codes for completion of a given cdio
      operation. By design 0 is successful completion and -1 is error
      completion. This is compatable with ioctl so those routines that
      call ioctl can just pass the value the get back (cast as this
      enum). Also, by using negative numbers for errors, the
      enumeration values below can be used in places where a positive
      value is expected when things complete successfully. For example,
      get_blocksize returns the blocksize, but on error uses the error
      codes below. So note that this enumeration is often cast to an
      integer.  C seems to tolerate this.
  */
  typedef enum  {
    DRIVER_OP_SUCCESS        =  0, /**< in cases where an int is
                                    returned, like cdio_set_speed,
                                    more the negative return codes are
                                    for errors and the positive ones
                                    for success. */
    DRIVER_OP_ERROR          = -1, /**< operation returned an error */
    DRIVER_OP_UNSUPPORTED    = -2, /**< returned when a particular driver
                                      doesn't support a particular operation.
                                      For example an image driver which doesn't
                                      really "eject" a CD.
                                   */
    DRIVER_OP_UNINIT         = -3, /**< returned when a particular driver
                                      hasn't been initialized or a null
                                      pointer has been passed.
                                   */
    DRIVER_OP_NOT_PERMITTED  = -4, /**< Operation not permitted.
                                      For example might be a permission
                                      problem.
                                   */
    DRIVER_OP_BAD_PARAMETER  = -5, /**< Bad parameter passed  */
    DRIVER_OP_BAD_POINTER    = -6, /**< Bad pointer to memory area  */
    DRIVER_OP_NO_DRIVER      = -7, /**< Operation called on a driver
                                      not available on this OS  */
    DRIVER_OP_MMC_SENSE_DATA = -8, /**< MMC operation returned sense data,
                                      but no other error above recorded. */
  } driver_return_code_t;

  /**
    Close media tray in CD drive if there is a routine to do so.

    @param psz_drive the name of CD-ROM to be closed. If NULL, we will
    use the default device.
    @param p_driver_id is the driver to be used or that got used if
    it was DRIVER_UNKNOWN or DRIVER_DEVICE; If this is NULL, we won't
    report back the driver used.
  */
  driver_return_code_t cdio_close_tray (const char *psz_drive,
                                        /*in/out*/ driver_id_t *p_driver_id);

  /**
    @param drc the return code you want interpreted.
    @return the string information about drc
  */
  const char *cdio_driver_errmsg(driver_return_code_t drc);

  /**
    Eject media in CD drive if there is a routine to do so.

    @param p_cdio the CD object to be acted upon.
    If the CD is ejected *p_cdio is free'd and p_cdio set to NULL.
  */
  driver_return_code_t cdio_eject_media (CdIo_t **p_cdio);

  /**
    Eject media in CD drive if there is a routine to do so.

    @param psz_drive the name of the device to be acted upon.
    If NULL is given as the drive, we'll use the default driver device.
  */
  driver_return_code_t cdio_eject_media_drive (const char *psz_drive);

  /**
    Free device list returned by cdio_get_devices or
    cdio_get_devices_with_cap.

    @param device_list list returned by cdio_get_devices or
    cdio_get_devices_with_cap

    @see cdio_get_devices, cdio_get_devices_with_cap

  */
  void cdio_free_device_list (char * device_list[]);

  /**
    Get the default CD device.
    if p_cdio is NULL (we haven't initialized a specific device driver),
    then find a suitable one and return the default device for that.

    @param p_cdio the CD object queried
    @return a string containing the default CD device or NULL is
    if we couldn't get a default device.

    In some situations of drivers or OS's we can't find a CD device if
    there is no media in it and it is possible for this routine to return
    NULL even though there may be a hardware CD-ROM.
  */
  char * cdio_get_default_device (const CdIo_t *p_cdio);

  /**
    Return a string containing the default CD device if none is specified.
    if p_driver_id is DRIVER_UNKNOWN or DRIVER_DEVICE
    then find a suitable one set the default device for that.

    NULL is returned if we couldn't get a default device.
  */
  char * cdio_get_default_device_driver (/*in/out*/ driver_id_t *p_driver_id);

  /** Return an array of device names. If you want a specific
    devices for a driver, give that device. If you want hardware
    devices, give DRIVER_DEVICE and if you want all possible devices,
    image drivers and hardware drivers give DRIVER_UNKNOWN.

    NULL is returned if we couldn't return a list of devices.

    In some situations of drivers or OS's we can't find a CD device if
    there is no media in it and it is possible for this routine to return
    NULL even though there may be a hardware CD-ROM.
  */
  char ** cdio_get_devices (driver_id_t driver_id);

  /**
     Get an array of device names in search_devices that have at least
     the capabilities listed by the capabities parameter.  If
     search_devices is NULL, then we'll search all possible CD drives.

     Capabilities have two parts to them, a "filesystem" part and an
     "analysis" part.

     The filesystem part is mutually exclusive. For example either the
     filesystem is at most one of the High-Sierra, UFS, or HFS,
     ISO9660, fileystems. Valid combinations of say HFS and ISO9660
     are specified as a separate "filesystem".

     Capabilities on the other hand are not mutually exclusive. For
     example a filesystem may have none, either, or both of the XA or
     Rock-Ridge extension properties.

     If "b_any" is set false then every capability listed in the
     analysis portion of capabilities (i.e. not the basic filesystem)
     must be satisified. If no analysis capabilities are specified,
     that's a match.

     If "b_any" is set true, then if any of the analysis capabilities
     matches, we call that a success.

     In either case, in the filesystem portion different filesystem
     either specify 0 to match any filesystem or the specific
     filesystem type.

     To find a CD-drive of any type, use the mask CDIO_FS_MATCH_ALL.

     @return the array of device names or NULL if we couldn't get a
     default device.  It is also possible to return a non NULL but
     after dereferencing the the value is NULL. This also means nothing
     was found.
  */
  char ** cdio_get_devices_with_cap (/*in*/ char *ppsz_search_devices[],
                                     cdio_fs_anal_t capabilities, bool b_any);

  /**
     Like cdio_get_devices_with_cap but we return the driver we found
     as well. This is because often one wants to search for kind of drive
     and then *open* it afterwards. Giving the driver back facilitates this,
     and speeds things up for libcdio as well.
  */
  char ** cdio_get_devices_with_cap_ret (/*in*/ char* ppsz_search_devices[],
                                         cdio_fs_anal_t capabilities,
                                         bool b_any,
                                         /*out*/ driver_id_t *p_driver_id);

  /**
     Like cdio_get_devices, but we may change the p_driver_id if we
     were given DRIVER_DEVICE or DRIVER_UNKNOWN. This is because often
     one wants to get a drive name and then *open* it
     afterwards. Giving the driver back facilitates this, and speeds
     things up for libcdio as well.
   */

  char ** cdio_get_devices_ret (/*in/out*/ driver_id_t *p_driver_id);

  /**
     Get the what kind of device we've got.

     @param p_cdio the CD object queried
     @param p_read_cap pointer to return read capabilities
     @param p_write_cap pointer to return write capabilities
     @param p_misc_cap pointer to return miscellaneous other capabilities

     In some situations of drivers or OS's we can't find a CD device if
     there is no media in it. In this situation capabilities will show up as
     NULL even though there isa hardware CD-ROM.
  */
  void cdio_get_drive_cap (const CdIo_t *p_cdio,
                           cdio_drive_read_cap_t  *p_read_cap,
                           cdio_drive_write_cap_t *p_write_cap,
                           cdio_drive_misc_cap_t  *p_misc_cap);

  /**
     Get the drive capabilities for a specified device.

     Return a list of device capabilities.

     In some situations of drivers or OS's we can't find a CD device if
     there is no media in it. In this situation capabilities will show up as
     NULL even though there isa hardware CD-ROM.
  */
  void cdio_get_drive_cap_dev (const char *device,
                               cdio_drive_read_cap_t  *p_read_cap,
                               cdio_drive_write_cap_t *p_write_cap,
                               cdio_drive_misc_cap_t  *p_misc_cap);

  /**
     Get a string containing the name of the driver in use.

     @return a string with driver name or NULL if CdIo_t is NULL (we
     haven't initialized a specific device.
  */
  const char * cdio_get_driver_name (const CdIo_t *p_cdio);

  /**
     Return a string containing the name of the driver in use from the driver_id.
     if CdIo is NULL (we haven't initialized a specific device driver),
     then return NULL.
  */
  const char * cdio_get_driver_name_from_id (driver_id_t driver_id);


  /**
     Get the driver id.
     if CdIo_t is NULL (we haven't initialized a specific device driver),
     then return DRIVER_UNKNOWN.

     @return the driver id..
  */
  driver_id_t cdio_get_driver_id (const CdIo_t *p_cdio);

  /**
    Get the CD-ROM hardware info via a SCSI MMC INQUIRY command.
    False is returned if we had an error getting the information.
  */
  bool cdio_get_hwinfo ( const CdIo_t *p_cdio,
                         /*out*/ cdio_hwinfo_t *p_hw_info );


  /**
     Get the LSN of the first track of the last session of
     on the CD.

     @param p_cdio the CD object to be acted upon.
     @param i_last_session pointer to the session number to be returned.
  */
  driver_return_code_t cdio_get_last_session (CdIo_t *p_cdio,
                                              /*out*/ lsn_t *i_last_session);

  /**
      Find out if media has changed since the last call.
      @param p_cdio the CD object to be acted upon.
      @return 1 if media has changed since last call, 0 if not. Error
      return codes are the same as driver_return_code_t
   */
  int cdio_get_media_changed(CdIo_t *p_cdio);

  /** True if CD-ROM understand ATAPI commands. */
  bool_3way_t cdio_have_atapi (CdIo_t *p_cdio);

  /** Like cdio_have_xxx but uses an enumeration instead. */
  bool cdio_have_driver (driver_id_t driver_id);

  /**
     Free any resources associated with p_cdio. Call this when done
     using p_cdio and using CD reading/control operations.

    @param p_cdio the CD object to eliminated.
   */
  void cdio_destroy (CdIo_t *p_cdio);

  /**
    Get a string decribing driver_id.

    @param driver_id the driver you want the description for
    @return a string of driver description
  */
  const char *cdio_driver_describe (driver_id_t driver_id);

  /**
     Sets up to read from place specified by psz_source and
     driver_id. This or cdio_open_* should be called before using any
     other routine, except cdio_init or any routine that accesses the
     CD-ROM drive by name. cdio_open will call cdio_init, if that
     hasn't been done previously.

     @return the cdio object or NULL on error or no device.  If NULL
     is given as the source, we'll use the default driver device.
  */
  CdIo_t * cdio_open (const char *psz_source, driver_id_t driver_id);

  /**
     Sets up to read from place specified by psz_source, driver_id and
     access mode. This or cdio_open* should be called before using any
     other routine, except cdio_init or any routine that accesses the
     CD-ROM drive by name. This will call cdio_init, if that hasn't
     been done previously.

     If NULL is given as the source, we'll use the default driver device.

     @return the cdio object or NULL on error or no device.
  */
  CdIo_t * cdio_open_am (const char *psz_source,
                         driver_id_t driver_id, const char *psz_access_mode);

  /**
     Set up BIN/CUE CD disk-image for reading. Source is the .bin or
     .cue file

     @return the cdio object or NULL on error or no device.
   */
  CdIo_t * cdio_open_bincue (const char *psz_cue_name);

  /**
     Set up BIN/CUE CD disk-image for reading. Source is the .bin or
     .cue file

     @return the cdio object or NULL on error or no device..
   */
  CdIo_t * cdio_open_am_bincue (const char *psz_cue_name,
                                const char *psz_access_mode);

  /**
     Set up cdrdao CD disk-image for reading. Source is the .toc file

     @return the cdio object or NULL on error or no device.
   */
  CdIo_t * cdio_open_cdrdao (const char *psz_toc_name);

  /**
     Set up cdrdao CD disk-image for reading. Source is the .toc file

     @return the cdio object or NULL on error or no device..
  */
  CdIo_t * cdio_open_am_cdrdao (const char *psz_toc_name,
                                const char *psz_access_mode);

  /**
     Return a string containing the default CUE file that would
     be used when none is specified.

     @return the cdio object or NULL on error or no device.
  */
  char * cdio_get_default_device_bincue(void);

  char **cdio_get_devices_bincue(void);

  /**
     @return string containing the default CUE file that would be
     used when none is specified. NULL is returned on error or there
     is no device.
   */
  char * cdio_get_default_device_cdrdao(void);

  char **cdio_get_devices_cdrdao(void);

  /**
     Set up CD-ROM for reading. The device_name is
     the some sort of device name.

     @return the cdio object for subsequent operations.
     NULL on error or there is no driver for a some sort of hardware CD-ROM.
  */
  CdIo_t * cdio_open_cd (const char *device_name);

  /**
     Set up CD-ROM for reading. The device_name is
     the some sort of device name.

     @return the cdio object for subsequent operations.
     NULL on error or there is no driver for a some sort of hardware CD-ROM.
  */
  CdIo_t * cdio_open_am_cd (const char *psz_device,
                            const char *psz_access_mode);

  /**
     CDRWIN BIN/CUE CD disc-image routines. Source is the .cue file

     @return the cdio object for subsequent operations.
     NULL on error.
   */
  CdIo_t * cdio_open_cue (const char *cue_name);

  /**
     Set up CD-ROM for reading using the AIX driver. The device_name is
     the some sort of device name.

     @return the cdio object for subsequent operations.
     NULL on error or there is no AIX driver.

     @see cdio_open
   */
  CdIo_t * cdio_open_am_aix (const char *psz_source,
                             const char *psz_access_mode);

  /**
     Set up CD-ROM for reading using the AIX driver. The device_name is
     the some sort of device name.

     @return the cdio object for subsequent operations.
     NULL on error or there is no AIX driver.

     @see cdio_open
   */
  CdIo_t * cdio_open_aix (const char *psz_source);

  /**
     Return a string containing the default device name that the AIX
     driver would use when none is specified.

     @return the cdio object for subsequent operations.
     NULL on error or there is no AIX driver.

     @see cdio_open_cd, cdio_open
   */
  char * cdio_get_default_device_aix(void);

  /**
     Return a list of all of the CD-ROM devices that the AIX driver
     can find.

     In some situations of drivers or OS's we can't find a CD device
     if there is no media in it and it is possible for this routine to
     return NULL even though there may be a hardware CD-ROM.
   */
  char **cdio_get_devices_aix(void);

  /**
     Set up CD-ROM for reading using the BSDI driver. The device_name
     is the some sort of device name.

     @param psz_source the name of the device to open
     @return the cdio object for subsequent operations.
     NULL on error or there is no BSDI driver.

     @see cdio_open
   */
  CdIo_t * cdio_open_bsdi (const char *psz_source);

  /**
     Set up CD-ROM for reading using the BSDI driver. The device_name
     is the some sort of device name.

     @return the cdio object for subsequent operations.
     NULL on error or there is no BSDI driver.

     @see cdio_open
   */
  CdIo_t * cdio_open_am_bsdi (const char *psz_source,
                              const char *psz_access_mode);

  /**
     Return a string containing the default device name that the BSDI
     driver would use when none is specified.

     @return the cdio object for subsequent operations.
     NULL on error or there is no BSDI driver.

     @see cdio_open_cd, cdio_open
   */
  char * cdio_get_default_device_bsdi(void);

  /**
     Return a list of all of the CD-ROM devices that the BSDI driver
     can find.

     In some situations of drivers or OS's we can't find a CD device
     if there is no media in it and it is possible for this routine to
     return NULL even though there may be a hardware CD-ROM.
   */
  char **cdio_get_devices_bsdi(void);

  /**
     Set up CD-ROM for reading using the FreeBSD driver. The
     device_name is the some sort of device name.

     NULL is returned on error or there is no FreeBSD driver.

     @see cdio_open_cd, cdio_open
   */
  CdIo_t * cdio_open_freebsd (const char *paz_psz_source);

  /**
     Set up CD-ROM for reading using the FreeBSD driver. The
     device_name is the some sort of device name.

     NULL is returned on error or there is no FreeBSD driver.

     @see cdio_open_cd, cdio_open
   */
  CdIo_t * cdio_open_am_freebsd (const char *psz_source,
                                 const char *psz_access_mode);

  /**
     Return a string containing the default device name that the
     FreeBSD driver would use when none is specified.

     NULL is returned on error or there is no CD-ROM device.
   */
  char * cdio_get_default_device_freebsd(void);

  /**
     Return a list of all of the CD-ROM devices that the FreeBSD
     driver can find.
   */
  char **cdio_get_devices_freebsd(void);

  /**
     Set up CD-ROM for reading using the GNU/Linux driver. The
     device_name is the some sort of device name.

     @return the cdio object for subsequent operations.
     NULL on error or there is no GNU/Linux driver.

     In some situations of drivers or OS's we can't find a CD device if
     there is no media in it and it is possible for this routine to return
     NULL even though there may be a hardware CD-ROM.
   */
  CdIo_t * cdio_open_linux (const char *psz_source);

  /**
     Set up CD-ROM for reading using the GNU/Linux driver. The
     device_name is the some sort of device name.

     @return the cdio object for subsequent operations.
     NULL on error or there is no GNU/Linux driver.
   */
  CdIo_t * cdio_open_am_linux (const char *psz_source,
                               const char *access_mode);

  /**
     Return a string containing the default device name that the
     GNU/Linux driver would use when none is specified. A scan is made
     for CD-ROM drives with CDs in them.

     NULL is returned on error or there is no CD-ROM device.

     In some situations of drivers or OS's we can't find a CD device if
     there is no media in it and it is possible for this routine to return
     NULL even though there may be a hardware CD-ROM.

     @see cdio_open_cd, cdio_open
   */
  char * cdio_get_default_device_linux(void);

  /**
     Return a list of all of the CD-ROM devices that the GNU/Linux
     driver can find.
   */
  char **cdio_get_devices_linux(void);

  /**
     Set up CD-ROM for reading using the Sun Solaris driver. The
     device_name is the some sort of device name.

     @return the cdio object for subsequent operations.
     NULL on error or there is no Solaris driver.
   */
  CdIo_t * cdio_open_solaris (const char *psz_source);

  /**
     Set up CD-ROM for reading using the Sun Solaris driver. The
     device_name is the some sort of device name.

     @return the cdio object for subsequent operations.
     NULL on error or there is no Solaris driver.
   */
  CdIo_t * cdio_open_am_solaris (const char *psz_source,
                                 const char *psz_access_mode);

  /**
     Return a string containing the default device name that the
     Solaris driver would use when none is specified. A scan is made
     for CD-ROM drives with CDs in them.

     NULL is returned on error or there is no CD-ROM device.

     In some situations of drivers or OS's we can't find a CD device if
     there is no media in it and it is possible for this routine to return
     NULL even though there may be a hardware CD-ROM.

     @see cdio_open_cd, cdio_open
   */
  char * cdio_get_default_device_solaris(void);

  /**
     Return a list of all of the CD-ROM devices that the Solaris
     driver can find.
   */
  char **cdio_get_devices_solaris(void);

  /**
     Set up CD-ROM for reading using the Apple OSX driver. The
     device_name is the some sort of device name.

     NULL is returned on error or there is no OSX driver.

     In some situations of drivers or OS's we can't find a CD device if
     there is no media in it and it is possible for this routine to return
     NULL even though there may be a hardware CD-ROM.

     @see cdio_open_cd, cdio_open
   */
  CdIo_t * cdio_open_osx (const char *psz_source);

  /**
     Set up CD-ROM for reading using the Apple OSX driver. The
     device_name is the some sort of device name.

     NULL is returned on error or there is no OSX driver.

     @see cdio_open_cd, cdio_open
   */
  CdIo_t * cdio_open_am_osx (const char *psz_source,
                             const char *psz_access_mode);

  /**
     Return a string containing the default device name that the OSX
     driver would use when none is specified. A scan is made for
     CD-ROM drives with CDs in them.

     In some situations of drivers or OS's we can't find a CD device if
     there is no media in it and it is possible for this routine to return
     NULL even though there may be a hardware CD-ROM.
   */
  char * cdio_get_default_device_osx(void);

  /**
     Return a list of all of the CD-ROM devices that the OSX driver
     can find.
   */
  char **cdio_get_devices_osx(void);

  /**
     Set up CD-ROM for reading using the Microsoft Windows driver. The
     device_name is the some sort of device name.

     In some situations of drivers or OS's we can't find a CD device if
     there is no media in it and it is possible for this routine to return
     NULL even though there may be a hardware CD-ROM.
   */
  CdIo_t * cdio_open_win32 (const char *psz_source);

  /**
     Set up CD-ROM for reading using the Microsoft Windows driver. The
     device_name is the some sort of device name.

     NULL is returned on error or there is no Microsof Windows driver.
   */
  CdIo_t * cdio_open_am_win32 (const char *psz_source,
                               const char *psz_access_mode);

  /**
     Return a string containing the default device name that the
     Win32 driver would use when none is specified. A scan is made
     for CD-ROM drives with CDs in them.

     In some situations of drivers or OS's we can't find a CD device
     if there is no media in it and it is possible for this routine to
     return NULL even though there may be a hardware CD-ROM.

     @see cdio_open_cd, cdio_open
   */
  char * cdio_get_default_device_win32(void);

  char **cdio_get_devices_win32(void);

  /**
     Set up CD-ROM for reading using the IBM OS/2 driver. The
     device_name is the some sort of device name.

     NULL is returned on error or there is no OS/2 driver.

     In some situations of drivers or OS's we can't find a CD device if
     there is no media in it and it is possible for this routine to return
     NULL even though there may be a hardware CD-ROM.

     @see cdio_open_cd, cdio_open
   */
  CdIo_t * cdio_open_os2 (const char *psz_source);

  /**
     Set up CD-ROM for reading using the IBM OS/2 driver. The
     device_name is the some sort of device name.

     NULL is returned on error or there is no OS/2 driver.

     @see cdio_open_cd, cdio_open
   */
  CdIo_t * cdio_open_am_os2 (const char *psz_source,
                            const char *psz_access_mode);

  /**
     Return a string containing the default device name that the OS/2
     driver would use when none is specified. A scan is made for
     CD-ROM drives with CDs in them.

     In some situations of drivers or OS's we can't find a CD device
     if there is no media in it and it is possible for this routine to
     return NULL even though there may be a hardware CD-ROM.
   */
  char * cdio_get_default_device_os2(void);

  /**
Return a list of all of the CD-ROM devices that the OS/2 driver
      can find.
   */
  char **cdio_get_devices_os2(void);

  /**
     Set up CD-ROM for reading using the Nero driver. The device_name
     is the some sort of device name.

     @return true on success; NULL on error or there is no Nero driver.
   */
  CdIo_t * cdio_open_nrg (const char *psz_source);

  /**
     Set up CD-ROM for reading using the Nero driver. The device_name
     is the some sort of device name.

     @return true on success; NULL on error or there is no Nero driver.
   */
  CdIo_t * cdio_open_am_nrg (const char *psz_source,
                             const char *psz_access_mode);

  /**
     Get a string containing the default device name that the NRG
     driver would use when none is specified. A scan is made for NRG
     disk images in the current directory.

     @return string containing the default device. NULL on error or
     there is no CD-ROM device.
   */
  char * cdio_get_default_device_nrg(void);

  char **cdio_get_devices_nrg(void);

  /**

     Determine if bin_name is the bin file part of  a CDRWIN CD disk image.

     @param bin_name location of presumed CDRWIN bin image file.
     @return the corresponding CUE file if bin_name is a BIN file or
     NULL if not a BIN file.
  */
  char *cdio_is_binfile(const char *bin_name);

  /**
     Determine if cue_name is the cue sheet for a CDRWIN CD disk image.

     @return corresponding BIN file if cue_name is a CDRWIN cue file or
     NULL if not a CUE file.
  */
  char *cdio_is_cuefile(const char *cue_name);

  /**
    Determine if psg_nrg is a Nero CD disc image.

    @param psz_nrg location of presumed NRG image file.
    @return true if psz_nrg is a Nero NRG image or false
    if not a NRG image.
  */
  bool cdio_is_nrg(const char *psz_nrg);

  /**
     Determine if psz_toc is a TOC file for a cdrdao CD disc image.

     @param psz_toc location of presumed TOC image file.
     @return true if toc_name is a cdrdao TOC file or false
     if not a TOC file.
  */
  bool cdio_is_tocfile(const char *psz_toc);

  /**
     Determine if psz_source refers to a real hardware CD-ROM.

     @param psz_source location name of object
     @param driver_id   driver for reading object. Use DRIVER_UNKNOWN if you
     don't know what driver to use.
     @return true if psz_source is a device; If false is returned we
     could have a CD disk image.
  */
  bool cdio_is_device(const char *psz_source, driver_id_t driver_id);

  /**
    Set the blocksize for subsequent reads.
  */
  driver_return_code_t cdio_set_blocksize ( const CdIo_t *p_cdio,
                                            int i_blocksize );

  /**
     Set the drive speed.

     @param p_cdio          CD structure set by cdio_open().

    @param i_drive_speed speed in CD-ROM speed units. Note this not
                         Kbs as would be used in the MMC spec or in
                         mmc_set_speed(). To convert CD-ROM speed
                         units to Kbs, multiply the number by 176 (for
                         raw data) and by 150 (for filesystem
                         data). On many CD-ROM drives, specifying a
                         value too large will result in using the
                         fastest speed.

      @see mmc_set_speed and mmc_set_drive_speed
  */
  driver_return_code_t cdio_set_speed ( const CdIo_t *p_cdio,
                                        int i_drive_speed );

  /**
     Get the value associatied with key.

     @param p_cdio the CD object queried
     @param key the key to retrieve
     @return the value associatd with "key" or NULL if p_cdio is NULL
     or "key" does not exist.
  */
  const char * cdio_get_arg (const CdIo_t *p_cdio,  const char key[]);

  /**
     Set the arg "key" with "value" in "p_cdio".

     @param p_cdio the CD object to set
     @param key the key to set
     @param value the value to assocaiate with key
  */
  driver_return_code_t cdio_set_arg (CdIo_t *p_cdio, const char key[],
                                     const char value[]);

  /**
    Initialize CD Reading and control routines. Should be called first.
  */
  bool cdio_init(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

/**
   The below variables are trickery to force the above enum symbol
   values to be recorded in debug symbol tables. They are used to
   allow one to refer to the enumeration value names in the typedefs
   above in a debugger and debugger expressions.  */
extern cdio_drive_cap_misc_t          debug_cdio_drive_cap_misc;
extern cdio_drive_cap_read_t          debug_cdio_drive_cap_read_t;
extern cdio_drive_cap_write_t         debug_drive_cap_write_t;
extern cdio_mmc_hw_len_t              debug_cdio_mmc_hw_len;
extern cdio_src_category_mask_t       debug_cdio_src_category_mask;

#endif /* CDIO_DEVICE_H_ */
