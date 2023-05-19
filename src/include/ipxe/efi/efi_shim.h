#ifndef _IPXE_EFI_SHIM_H
#define _IPXE_EFI_SHIM_H

/** @file
 *
 * UEFI shim handling
 *
 */

FILE_LICENCE ( GPL2_OR_LATER_OR_UBDL );

#include <ipxe/image.h>
#include <ipxe/efi/efi.h>

/** A shim unlocker */
struct efi_shim_unlocker {
	/** Protocol installation event */
	EFI_EVENT event;
	/** Protocol notification registration token */
	void *token;
};

extern struct image_tag efi_shim __image_tag;
extern struct image_tag efi_shim_crutch __image_tag;

extern int efi_shim_install ( struct efi_shim_unlocker *unlocker );
extern void efi_shim_uninstall ( struct efi_shim_unlocker *unlocker );

#endif /* _IPXE_EFI_SHIM_H */
