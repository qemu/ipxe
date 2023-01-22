/* -*- sh -*- */

/*
 * Linker script for x86_64 Linux images
 *
 */

OUTPUT_FORMAT ( "elf64-x86-64", "elf64-x86-64", "elf64-x86-64" )
OUTPUT_ARCH ( i386:x86-64 )

INCLUDE scripts/linux.lds
