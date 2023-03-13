/*
 * Copyright (C) 2023 Michael Brown <mbrown@fensystems.co.uk>.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 * You can also choose to distribute this program under the terms of
 * the Unmodified Binary Distribution Licence (as given in the file
 * COPYING.UBDL), provided that you have satisfied its requirements.
 */

FILE_LICENCE ( GPL2_OR_LATER_OR_UBDL );

#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <assert.h>
#include <ipxe/image.h>
#include <ipxe/efi/efi.h>
#include <ipxe/efi/efi_shim.h>
#include <usr/shimmgmt.h>

/** @file
 *
 * EFI shim management
 *
 */

/**
 * Set shim image
 *
 * @v image		Shim image, or NULL to clear shim
 * @v altname		Second stage alternative name, or NULL to use default
 * @ret rc		Return status code
 */
int shim ( struct image *image, const char *altname ) {
	static wchar_t wbootpath[] = EFI_REMOVABLE_MEDIA_FILE_NAME;
	char bootpath[ sizeof ( wbootpath ) / sizeof ( wbootpath[0] ) ];
	char *bootname;
	char *sep;
	int rc;

	/* Clear any existing shim */
	image_put ( efi_shim );
	efi_shim = NULL;

	/* Do nothing more unless a shim is specified */
	if ( ! image )
		return 0;

	/* Construct default second stage alternative name */
	snprintf ( bootpath, sizeof ( bootpath ), "%ls", wbootpath );
	sep = strrchr ( bootpath, '\\' );
	assert ( sep != NULL );
	bootname = ( sep + 1 );
	assert ( strncasecmp ( bootname, "BOOT", 4 ) == 0 );
	memcpy ( bootname, "GRUB", 4 );

	/* Use default second stage alternative name, if not specified */
	if ( ! altname )
		altname = bootname;

	/* Record second stage alternative name, if any */
	if ( ( rc = image_set_cmdline ( image, altname ) ) != 0 )
		return rc;

	/* Record as shim */
	efi_shim = image_get ( image );

	DBGC ( image, "SHIM %s installed (altname %s)\n",
	       image->name, image->cmdline );
	return 0;
}
