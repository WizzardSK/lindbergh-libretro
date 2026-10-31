/* The loader's system/linuxloader as a zip, carried in the core
 * (linuxloader_libretro.c, install_loader); LR_SYSTEM_ZIP is its path */
#if defined(_WIN32)
    .section .rdata,"dr"
#else
    .section .rodata
#endif
    .global lr_system_zip
    .global lr_system_zip_end
    .balign 16
lr_system_zip:
    .incbin LR_SYSTEM_ZIP
lr_system_zip_end:
    .byte 0
#if !defined(_WIN32)
    .section .note.GNU-stack,"",%progbits
#endif
