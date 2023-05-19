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

/** EFI shim image */
struct image_tag efi_shim __image_tag = {
	.name = "SHIM",
};

/** EFI shim crutch image */
struct image_tag efi_shim_crutch __image_tag = {
	.name = "SHIMCRUTCH",
};

/**
 * Unlock UEFI shim
 *
 * @v event		Event
 * @v context		Event context
 *
 * The UEFI shim is gradually becoming less capable of directly
 * executing a kernel image, due to an ever increasing list of
 * assumptions that it will only ever be used in conjunction with a
 * second stage loader such as GRUB.
 *
 * For example: shim will erroneously complain if the image that it
 * loads and executes does not call in to the "shim lock protocol"
 * before calling ExitBootServices(), even if there is no valid reason
 * for it to have done so.
 *
 * Reduce the Secure Boot attack surface by removing, where possible,
 * this spurious requirement for the use of an additional second stage
 * loader.
 */
static EFIAPI void efi_shim_unlock ( EFI_EVENT event __unused, void *context ) {
	EFI_BOOT_SERVICES *bs = efi_systab->BootServices;
	EFI_GUID *protocol = &efi_shim_lock_protocol_guid;
	struct efi_shim_unlocker *unlocker = context;
	union {
		EFI_SHIM_LOCK_PROTOCOL *lock;
		void *interface;
	} u;
	uint8_t empty[0];
	EFI_STATUS efirc;

	/* Process all new instances of the shim lock protocol */
	while ( 1 ) {

		/* Get next instance */
		if ( ( efirc = bs->LocateProtocol ( protocol, unlocker->token,
						    &u.interface ) ) != 0 )
			break;

		/* Call shim lock protocol with empty buffer */
		u.lock->Verify ( empty, sizeof ( empty ) );
		DBGC ( unlocker, "SHIM unlocked %p\n", u.interface );
	}
}

/**
 * Install UEFI shim unlocker
 *
 * @v unlocker		Shim unlocker
 * @ret rc		Return status code
 */
int efi_shim_install ( struct efi_shim_unlocker *unlocker ) {
	EFI_BOOT_SERVICES *bs = efi_systab->BootServices;
	EFI_GUID *protocol = &efi_shim_lock_protocol_guid;
	EFI_STATUS efirc;
	int rc;

	/* Create event */
	if ( ( efirc = bs->CreateEvent ( EVT_NOTIFY_SIGNAL, TPL_CALLBACK,
					 efi_shim_unlock, unlocker,
					 &unlocker->event ) ) != 0 ) {
		rc = -EEFI ( efirc );
		DBGC ( unlocker, "SHIM could not create event: %s\n",
		       strerror ( rc ) );
		goto err_create_event;
	}

	/* Register for protocol installations */
	if ( ( efirc = bs->RegisterProtocolNotify ( protocol, unlocker->event,
						    &unlocker->token ) ) != 0){
		rc = -EEFI ( efirc );
		DBGC ( unlocker, "SHIM could not register for protocols: %s\n",
		       strerror ( rc ) );
		goto err_register_notify;
	}

	return 0;

 err_register_notify:
	bs->CloseEvent ( unlocker->event );
 err_create_event:
	return rc;
}

/**
 * Uninstall UEFI shim unlocker
 *
 * @v unlocker		Shim unlocker
 */
void efi_shim_uninstall ( struct efi_shim_unlocker *unlocker ) {
	EFI_BOOT_SERVICES *bs = efi_systab->BootServices;

	bs->CloseEvent ( unlocker->event );
}
