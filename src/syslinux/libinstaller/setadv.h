#ifndef _H_SET_ADV_
#define _H_SET_ADV_

/* ADV information */
#define ADV_SIZE	512	/* Total size */
#define ADV_LEN		(ADV_SIZE-3*4)	/* Usable data size */

extern unsigned char syslinux_adv[2 * ADV_SIZE];

int syslinux_setadv(int tag, size_t size, const void *data);
void syslinux_reset_adv(unsigned char *advbuf);
int syslinux_validate_adv(unsigned char *advbuf);
int read_adv(const char *path, const char *cfg);
int write_adv(const char *path, const char *cfg);

#endif
