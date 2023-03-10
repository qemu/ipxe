#ifndef _IPXE_EFI_FILE_H
#define _IPXE_EFI_FILE_H

/** @file
 *
 * EFI file protocols
 *
 */

FILE_LICENCE ( GPL2_OR_LATER_OR_UBDL );

struct image;

extern int efi_file_install ( EFI_HANDLE handle, struct image *second,
			      const char *altname );
extern void efi_file_uninstall ( EFI_HANDLE handle );

#endif /* _IPXE_EFI_FILE_H */
