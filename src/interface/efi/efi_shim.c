/*
 * Copyright (C) 2022 Michael Brown <mbrown@fensystems.co.uk>.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
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
#include <errno.h>
#include <ipxe/image.h>
#include <ipxe/efi/efi.h>
#include <ipxe/efi/efi_shim.h>
#include <ipxe/efi/Protocol/ShimLock.h>

/** @file
 *
 * UEFI shim handling
 *
 */

FILE_LICENCE ( GPL2_OR_LATER_OR_UBDL );

/** UEFI shim image */
struct image_tag efi_shim __image_tag = {
	.name = "SHIM",
};

/** UEFI shim crutch image */
struct image_tag efi_shim_crutch __image_tag = {
	.name = "SHIMCRUTCH",
};

/** Original GetMemoryMap() function */
static EFI_GET_MEMORY_MAP efi_shim_orig_map;

/**
 * Unlock UEFI shim
 *
 * @v len		Memory map size
 * @v map		Memory map
 * @v key		Memory map key
 * @v desclen		Descriptor size
 * @v descver		Descriptor version
 * @ret efirc		EFI status code
 *
 * The UEFI shim is gradually becoming less capable of directly
 * executing a kernel image, due to an ever increasing list of
 * assumptions that it will only ever be used in conjunction with a
 * second stage loader such as GRUB.
 *
 * For example: shim will erroneously complain if the image that it
 * loads and executes does not call in to the "shim lock protocol"
 * before calling GetMemoryMap(), even if there is no valid reason
 * for it to have done so.
 *
 * Reduce the Secure Boot attack surface by removing, where possible,
 * this spurious requirement for the use of an additional second stage
 * loader.
 */
static EFIAPI EFI_STATUS efi_shim_unlock ( UINTN *len,
					   EFI_MEMORY_DESCRIPTOR *map,
					   UINTN *key, UINTN *desclen,
					   UINT32 *descver ) {
	EFI_BOOT_SERVICES *bs = efi_systab->BootServices;
	uint8_t empty[0];
	union {
		EFI_SHIM_LOCK_PROTOCOL *lock;
		void *interface;
	} u;
	EFI_STATUS efirc;

	/* Locate shim lock protocol */
	if ( ( efirc = bs->LocateProtocol ( &efi_shim_lock_protocol_guid,
					    NULL, &u.interface ) ) == 0 ) {
		u.lock->Verify ( empty, sizeof ( empty ) );
		DBGC ( u.lock, "SHIM unlocked %p\n", u.lock );
	}

	/* Hand off to original GetMemoryMap() */
	return efi_shim_orig_map ( len, map, key, desclen, descver );
}

/**
 * Install UEFI shim unlocker
 *
 * @ret rc		Return status code
 */
int efi_shim_install (  ) {
	EFI_BOOT_SERVICES *bs = efi_systab->BootServices;

	/* Intercept GetMemoryMap() via boot services table */
	efi_shim_orig_map = bs->GetMemoryMap;
	bs->GetMemoryMap = efi_shim_unlock;

	return 0;
}

/**
 * Uninstall UEFI shim unlocker
 *
 */
void efi_shim_uninstall ( void ) {
	EFI_BOOT_SERVICES *bs = efi_systab->BootServices;

	/* Restore original GetMemoryMap() */
	bs->GetMemoryMap = efi_shim_orig_map;
}
