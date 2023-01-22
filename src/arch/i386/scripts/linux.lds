/* -*- sh -*- */

/*
 * Linker script for i386 Linux images
 *
 */

OUTPUT_FORMAT ( "elf32-i386", "elf32-i386", "elf32-i386" )
OUTPUT_ARCH ( i386 )

INCLUDE scripts/linux.lds
