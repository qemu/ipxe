#ifndef _IPXE_EFI_SHIM_H
#define _IPXE_EFI_SHIM_H

/** @file
 *
 * EFI shim
 *
 */

FILE_LICENCE ( GPL2_OR_LATER_OR_UBDL );

#include <ipxe/image.h>

extern struct image *efi_shim;

/**
 * Set shim image
 *
 * @v image		Shim image, or NULL to clear shim
 */
static inline void efi_set_shim ( struct image *image ) {

	/* Clear any existing shim */
	image_put ( efi_shim );

	/* Record as shim */
	efi_shim = image_get ( image );
}

#endif /* _IPXE_EFI_SHIM_H */
