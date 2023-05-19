/*
 * Copyright (C) 2008 Michael Brown <mbrown@fensystems.co.uk>.
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
 */

FILE_LICENCE ( GPL2_OR_LATER );

#include <errno.h>
#include <stdlib.h>
#include <wchar.h>
#include <ipxe/efi/efi.h>
#include <ipxe/efi/efi_snp.h>
#include <ipxe/efi/efi_download.h>
#include <ipxe/efi/efi_file.h>
#include <ipxe/efi/efi_path.h>
#include <ipxe/efi/efi_strings.h>
#include <ipxe/efi/efi_wrap.h>
#include <ipxe/efi/efi_pxe.h>
#include <ipxe/efi/efi_driver.h>
#include <ipxe/efi/efi_image.h>
#include <ipxe/efi/efi_shim.h>
#include <ipxe/image.h>
#include <ipxe/init.h>
#include <ipxe/features.h>
#include <ipxe/uri.h>
#include <ipxe/console.h>

FEATURE ( FEATURE_IMAGE, "EFI", DHCP_EB_FEATURE_EFI, 1 );

/* Disambiguate the various error causes */
#define EINFO_EEFI_LOAD							\
	__einfo_uniqify ( EINFO_EPLATFORM, 0x01,			\
			  "Could not load image" )
#define EINFO_EEFI_LOAD_PROHIBITED					\
	__einfo_platformify ( EINFO_EEFI_LOAD, EFI_SECURITY_VIOLATION,	\
			      "Image prohibited by security policy" )
#define EEFI_LOAD_PROHIBITED						\
	__einfo_error ( EINFO_EEFI_LOAD_PROHIBITED )
#define EEFI_LOAD( efirc ) EPLATFORM ( EINFO_EEFI_LOAD, efirc,		\
				       EEFI_LOAD_PROHIBITED )
#define EINFO_EEFI_START						\
	__einfo_uniqify ( EINFO_EPLATFORM, 0x02,			\
			  "Could not start image" )
#define EEFI_START( efirc ) EPLATFORM ( EINFO_EEFI_START, efirc )

/**
 * Create device path for image
 *
 * @v image		EFI image
 * @v parent		Parent device path
 * @ret path		Device path, or NULL on failure
 *
 * The caller must eventually free() the device path.
 */
static EFI_DEVICE_PATH_PROTOCOL *
efi_image_path ( struct image *image, EFI_DEVICE_PATH_PROTOCOL *parent ) {
	EFI_DEVICE_PATH_PROTOCOL *path;
	FILEPATH_DEVICE_PATH *filepath;
	EFI_DEVICE_PATH_PROTOCOL *end;
	size_t name_len;
	size_t prefix_len;
	size_t filepath_len;
	size_t len;

	/* Calculate device path lengths */
	prefix_len = efi_path_len ( parent );
	name_len = strlen ( image->name );
	filepath_len = ( SIZE_OF_FILEPATH_DEVICE_PATH +
			 ( name_len + 1 /* NUL */ ) * sizeof ( wchar_t ) );
	len = ( prefix_len + filepath_len + sizeof ( *end ) );

	/* Allocate device path */
	path = zalloc ( len );
	if ( ! path )
		return NULL;

	/* Construct device path */
	memcpy ( path, parent, prefix_len );
	filepath = ( ( ( void * ) path ) + prefix_len );
	filepath->Header.Type = MEDIA_DEVICE_PATH;
	filepath->Header.SubType = MEDIA_FILEPATH_DP;
	filepath->Header.Length[0] = ( filepath_len & 0xff );
	filepath->Header.Length[1] = ( filepath_len >> 8 );
	efi_snprintf ( filepath->PathName, ( name_len + 1 /* NUL */ ),
		       "%s", image->name );
	end = ( ( ( void * ) filepath ) + filepath_len );
	efi_path_terminate ( end );

	return path;
}

/**
 * Create command line for image
 *
 * @v image		EFI image
 * @v shim		Shim image, or NULL
 * @ret cmdline		Command line, or NULL on failure
 */
static wchar_t * efi_image_cmdline ( struct image *image,
				     struct image *shim ) {
	const char *arg0;
	const char *arg1;
	const char *args;
	wchar_t *cmdline;
	size_t len;

	/* Select command line components */
	arg0 = image->name;
	arg1 = NULL;
	args = image->cmdline;
	if ( shim ) {
		arg0 = shim->name;
		if ( shim->cmdline ) {
			/* "<shim.efi> <shim explicit cmdline>" */
			args = shim->cmdline;
		} else {
			/* "<shim.efi> <image.efi> <image cmdline>" */
			arg1 = image->name;
		}
	}

	/* Allocate and construct command line */
	len = ( strlen ( arg0 ) +
		( arg1 ? ( 1 /* " " */ + strlen ( arg1 ) ) : 0 ) +
		( args ? ( 1 /* " " */ + strlen ( args ) ) : 0 ) );
	cmdline = zalloc ( ( len + 1 /* NUL */ ) * sizeof ( wchar_t ) );
	if ( ! cmdline )
		return NULL;
	efi_snprintf ( cmdline, ( len + 1 /* NUL */ ), "%s%s%s%s%s", arg0,
		       ( arg1 ? " " : "" ), arg1, ( args ? " " : "" ), args );

	return cmdline;
}

/**
 * Execute EFI image
 *
 * @v image		EFI image
 * @ret rc		Return status code
 */
static int efi_image_exec ( struct image *image ) {
	EFI_BOOT_SERVICES *bs = efi_systab->BootServices;
	struct efi_snp_device *snpdev;
	EFI_DEVICE_PATH_PROTOCOL *path;
	union {
		EFI_LOADED_IMAGE_PROTOCOL *image;
		void *interface;
	} loaded;
	struct image *shim;
	struct image *exec;
	EFI_HANDLE handle;
	EFI_MEMORY_TYPE type;
	wchar_t *cmdline;
	unsigned int toggle;
	EFI_STATUS efirc;
	int rc;

	/* Find an appropriate device handle to use */
	snpdev = last_opened_snpdev();
	if ( ! snpdev ) {
		DBGC ( image, "EFIIMAGE %s could not identify SNP device\n",
		       image->name );
		rc = -ENODEV;
		goto err_no_snpdev;
	}

	/* Use shim instead of directly executing image if applicable */
	shim = ( efi_can_load ( image ) ?
		 NULL : find_image_tag ( &efi_shim ) );
	exec = ( shim ? shim : image );
	if ( shim ) {
		DBGC ( image, "EFIIMAGE %s executing via %s\n",
		       image->name, shim->name );
	}

	/* Re-register as a hidden image to allow for access via file I/O */
	toggle = ( ~image->flags & IMAGE_HIDDEN );
	image->flags |= IMAGE_HIDDEN;
	if ( ( rc = register_image ( image ) ) != 0 )
		goto err_register_image;

	/* Install file I/O protocols */
	if ( ( rc = efi_file_install ( snpdev->handle ) ) != 0 ) {
		DBGC ( image, "EFIIMAGE %s could not install file protocol: "
		       "%s\n", image->name, strerror ( rc ) );
		goto err_file_install;
	}

	/* Install PXE base code protocol (unless using a shim) */
	if ( ( ! shim ) &&
	     ( rc = efi_pxe_install ( snpdev->handle, snpdev->netdev ) ) != 0 ){
		DBGC ( image, "EFIIMAGE %s could not install PXE protocol: "
		       "%s\n", image->name, strerror ( rc ) );
		goto err_pxe_install;
	}

	/* Install iPXE download protocol */
	if ( ( rc = efi_download_install ( snpdev->handle ) ) != 0 ) {
		DBGC ( image, "EFIIMAGE %s could not install iPXE download "
		       "protocol: %s\n", image->name, strerror ( rc ) );
		goto err_download_install;
	}

	/* Create device path for image */
	path = efi_image_path ( exec, snpdev->path );
	if ( ! path ) {
		DBGC ( image, "EFIIMAGE %s could not create device path\n",
		       image->name );
		rc = -ENOMEM;
		goto err_image_path;
	}

	/* Create command line for image */
	cmdline = efi_image_cmdline ( image, shim );
	if ( ! cmdline ) {
		DBGC ( image, "EFIIMAGE %s could not create command line\n",
		       image->name );
		rc = -ENOMEM;
		goto err_cmdline;
	}

	/* Install shim unlocker (if using a shim) */
	if ( shim && ( ( rc = efi_shim_install() ) != 0 ) ) {
		DBGC ( image, "EFIIMAGE %s could not install shim unlocker: "
		       "%s\n", image->name, strerror ( rc ) );
		goto err_shim_install;
	}

	/* Attempt loading image */
	handle = NULL;
	if ( ( efirc = bs->LoadImage ( FALSE, efi_image_handle, path,
				       user_to_virt ( exec->data, 0 ),
				       exec->len, &handle ) ) != 0 ) {
		/* Not an EFI image */
		rc = -EEFI_LOAD ( efirc );
		DBGC ( image, "EFIIMAGE %s could not load: %s\n",
		       image->name, strerror ( rc ) );
		if ( efirc == EFI_SECURITY_VIOLATION ) {
			goto err_load_image_security_violation;
		} else {
			goto err_load_image;
		}
	}

	/* Get the loaded image protocol for the newly loaded image */
	efirc = bs->OpenProtocol ( handle, &efi_loaded_image_protocol_guid,
				   &loaded.interface, efi_image_handle,
				   NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL );
	if ( efirc ) {
		/* Should never happen */
		rc = -EEFI ( efirc );
		goto err_open_protocol;
	}

	/* Some EFI 1.10 implementations seem not to fill in DeviceHandle */
	if ( loaded.image->DeviceHandle == NULL ) {
		DBGC ( image, "EFIIMAGE %s filling in missing DeviceHandle\n",
		       image->name );
		loaded.image->DeviceHandle = snpdev->handle;
	}

	/* Sanity checks */
	assert ( loaded.image->ParentHandle == efi_image_handle );
	assert ( loaded.image->DeviceHandle == snpdev->handle );
	assert ( loaded.image->LoadOptionsSize == 0 );
	assert ( loaded.image->LoadOptions == NULL );

	/* Record image code type */
	type = loaded.image->ImageCodeType;

	/* Set command line */
	loaded.image->LoadOptions = cmdline;
	loaded.image->LoadOptionsSize =
		( ( wcslen ( cmdline ) + 1 /* NUL */ ) * sizeof ( wchar_t ) );

	/* Release network devices for use via SNP */
	efi_snp_release();

	/* Wrap calls made by the loaded image (for debugging) */
	efi_wrap ( handle );

	///
	DBG ( "***** loaded image systab %p\n", loaded.image->SystemTable );

	/* Reset console since image will probably use it */
	console_reset();

	/* Start the image */
	if ( ( efirc = bs->StartImage ( handle, NULL, NULL ) ) != 0 ) {
		rc = -EEFI_START ( efirc );
		DBGC ( image, "EFIIMAGE %s could not start (or returned with "
		       "error): %s\n", image->name, strerror ( rc ) );
		goto err_start_image;
	}

	/* If image was a driver, connect it up to anything available */
	if ( type == EfiBootServicesCode ) {
		DBGC ( image, "EFIIMAGE %s connecting drivers\n", image->name );
		efi_driver_reconnect_all();
	}

	/* Success */
	rc = 0;

 err_start_image:
	efi_snp_claim();
 err_open_protocol:
	/* If there was no error, then the image must have been
	 * started and returned successfully.  It either unloaded
	 * itself, or it intended to remain loaded (e.g. it was a
	 * driver).  We therefore do not unload successful images.
	 *
	 * If there was an error, attempt to unload the image.  This
	 * may not work.  In particular, there is no way to tell
	 * whether an error returned from StartImage() was due to
	 * being unable to start the image (in which case we probably
	 * should call UnloadImage()), or due to the image itself
	 * returning an error (in which case we probably should not
	 * call UnloadImage()).  We therefore ignore any failures from
	 * the UnloadImage() call itself.
	 */
 err_load_image_security_violation:
	if ( rc != 0 )
		bs->UnloadImage ( handle );
 err_load_image:
	if ( shim )
		efi_shim_uninstall();
 err_shim_install:
	free ( cmdline );
 err_cmdline:
	free ( path );
 err_image_path:
	efi_download_uninstall ( snpdev->handle );
 err_download_install:
	if ( ! shim )
		efi_pxe_uninstall ( snpdev->handle );
 err_pxe_install:
	efi_file_uninstall ( snpdev->handle );
 err_file_install:
	unregister_image ( image );
 err_register_image:
	image->flags ^= toggle;
 err_no_snpdev:
	return rc;
}

/**
 * Probe EFI image
 *
 * @v image		EFI file
 * @ret rc		Return status code
 */
static int efi_image_probe ( struct image *image ) {
	EFI_BOOT_SERVICES *bs = efi_systab->BootServices;
	static EFI_DEVICE_PATH_PROTOCOL empty_path = {
		.Type = END_DEVICE_PATH_TYPE,
		.SubType = END_ENTIRE_DEVICE_PATH_SUBTYPE,
		.Length[0] = sizeof ( empty_path ),
	};
	EFI_HANDLE handle;
	EFI_STATUS efirc;
	int rc;

	/* Attempt loading image */
	handle = NULL;
	if ( ( efirc = bs->LoadImage ( FALSE, efi_image_handle, &empty_path,
				       user_to_virt ( image->data, 0 ),
				       image->len, &handle ) ) != 0 ) {
		/* Not an EFI image */
		rc = -EEFI_LOAD ( efirc );
		DBGC ( image, "EFIIMAGE %s could not load: %s\n",
		       image->name, strerror ( rc ) );
		if ( efirc == EFI_SECURITY_VIOLATION ) {
			goto err_load_image_security_violation;
		} else {
			goto err_load_image;
		}
	}

	/* Unload the image.  We can't leave it loaded, because we
	 * have no "unload" operation.
	 */
	bs->UnloadImage ( handle );

	return 0;

 err_load_image_security_violation:
	bs->UnloadImage ( handle );
 err_load_image:
	return rc;
}

/**
 * Probe EFI PE image
 *
 * @v image		EFI file
 * @ret rc		Return status code
 *
 * The extremely broken UEFI Secure Boot model provides no way for us
 * to unambiguously determine that a valid EFI executable image was
 * rejected by LoadImage() because it failed signature verification.
 * We must therefore use heuristics to guess whether not an image that
 * was rejected by LoadImage() could still be loaded via a separate PE
 * loader such as the UEFI shim.
 */
static int efi_pe_image_probe ( struct image *image ) {
	const UINT16 magic = ( ( sizeof ( UINTN ) == sizeof ( uint32_t ) ) ?
			       EFI_IMAGE_NT_OPTIONAL_HDR32_MAGIC :
			       EFI_IMAGE_NT_OPTIONAL_HDR64_MAGIC );
	union {
		EFI_IMAGE_DOS_HEADER dos;
		EFI_IMAGE_OPTIONAL_HEADER_UNION pe;
	} u;

	/* Check for existence of DOS header */
	if ( image->len < sizeof ( u.dos ) ) {
		DBGC ( image, "EFIIMAGE %s too short for DOS header\n",
		       image->name );
		return -ENOEXEC;
	}
	copy_from_user ( &u.dos, image->data, 0, sizeof ( u.dos ) );
	if ( u.dos.e_magic != EFI_IMAGE_DOS_SIGNATURE ) {
		DBGC ( image, "EFIIMAGE %s missing MZ signature\n",
		       image->name );
		return -ENOEXEC;
	}

	/* Check for existence of PE header */
	if ( ( image->len < u.dos.e_lfanew ) ||
	     ( ( image->len - u.dos.e_lfanew ) < sizeof ( u.pe ) ) ) {
		DBGC ( image, "EFIIMAGE %s too short for PE header\n",
		       image->name );
		return -ENOEXEC;
	}
	copy_from_user ( &u.pe, image->data, u.dos.e_lfanew, sizeof ( u.pe ) );
	if ( u.pe.Pe32.Signature != EFI_IMAGE_NT_SIGNATURE ) {
		DBGC ( image, "EFIIMAGE %s missing PE signature\n",
		       image->name );
		return -ENOEXEC;
	}

	/* Check PE header magic */
	if ( u.pe.Pe32.OptionalHeader.Magic != magic ) {
		DBGC ( image, "EFIIMAGE %s incorrect magic %04x\n",
		       image->name, u.pe.Pe32.OptionalHeader.Magic );
		return -ENOEXEC;
	}

	return 0;
}

/** EFI image types */
struct image_type efi_image_type[] __image_type ( PROBE_NORMAL ) = {
	{
		.name = "EFI",
		.probe = efi_image_probe,
		.exec = efi_image_exec,
	},
	{
		.name = "EFIPE",
		.probe = efi_pe_image_probe,
		.exec = efi_image_exec,
	},
};
