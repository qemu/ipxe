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

#include <getopt.h>
#include <ipxe/command.h>
#include <ipxe/parseopt.h>
#include <ipxe/efi/efi_shim.h>
#include <usr/imgmgmt.h>

/** @file
 *
 * EFI shim command
 *
 */

/** "shim" options */
struct shim_options {
	/** Keep original image */
	int keep;
	/** Download timeout */
	unsigned long timeout;
	/** Second stage alternative name */
	char *altname;
};

/** "shim" option list */
static struct option_descriptor shim_opts[] = {
	OPTION_DESC ( "keep", 'k', no_argument,
		      struct shim_options, keep, parse_flag ),
	OPTION_DESC ( "timeout", 't', required_argument,
		      struct shim_options, timeout, parse_timeout ),
	OPTION_DESC ( "altname", 'a', required_argument,
		      struct shim_options, altname, parse_string ),
};

/** "shim" command descriptor */
static struct command_descriptor shim_cmd =
	COMMAND_DESC ( struct shim_options, shim_opts, 0, 1, NULL );

/**
 * The "shim" command
 *
 * @v argc		Argument count
 * @v argv		Argument list
 * @ret rc		Return status code
 */
static int shim_exec ( int argc, char **argv ) {
	struct shim_options opts;
	struct image *image = NULL;
	char *name_uri = NULL;
	int rc;

	/* Parse options */
	if ( ( rc = parse_options ( argc, argv, &shim_cmd, &opts ) ) != 0 )
		goto err_parse;

	/* Parse name/URI string */
	name_uri = argv[optind];

	/* Acquire image, if applicable */
	if ( name_uri &&
	     ( rc = imgacquire ( name_uri, opts.timeout, &image ) ) != 0 ) {
		goto err_acquire;
	}

	/* Record second stage alternative name, if any */
	if ( image && ( rc = image_set_cmdline ( image, opts.altname ) ) != 0 )
		goto err_cmdline;

	/* (Un)register as shim */
	efi_set_shim ( image );

	/* Success */
	rc = 0;

	/* Unregister original image unless --keep was specified */
	if ( image && ( ! opts.keep ) )
		unregister_image ( image );
 err_cmdline:
 err_acquire:
 err_parse:
	return rc;
}

/** Shim commands */
struct command shim_commands[] __command = {
	{
		.name = "shim",
		.exec = shim_exec,
	},
};
