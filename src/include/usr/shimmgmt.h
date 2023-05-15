#ifndef _USR_SHIMMGMT_H
#define _USR_SHIMMGMT_H

/** @file
 *
 * EFI shim management
 *
 */

FILE_LICENCE ( GPL2_OR_LATER_OR_UBDL );

#include <ipxe/image.h>

extern int shim ( struct image *image );

#endif /* _USR_SHIMMGMT_H */
