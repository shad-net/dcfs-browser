# SPDX-FileCopyrightText: Copyright 2026 dcfs-browser Project
# SPDX-License-Identifier: GPL-2.0-or-later

# lz4
add_library(dcfs_lz4 STATIC lz4/lib/lz4.c lz4/lib/lz4.h)
target_include_directories(dcfs_lz4 PUBLIC lz4/lib)

# zlib
add_library(dcfs_zlib STATIC
    zlib/adler32.c
    zlib/crc32.c
    zlib/inffast.c
    zlib/inflate.c
    zlib/inftrees.c
    zlib/zutil.c
)
target_include_directories(dcfs_zlib PUBLIC zlib)
if(MSVC)
    target_compile_definitions(dcfs_zlib PRIVATE _CRT_SECURE_NO_DEPRECATE _CRT_NONSTDC_NO_DEPRECATE)
else()
    target_compile_definitions(dcfs_zlib PRIVATE HAVE_UNISTD_H)
endif()

# bcdec: BC1-BC7 block decoders for texture previews. Precise mode adds signed BC4/BC5.
add_library(dcfs_bcdec STATIC bcdec.c bcdec/bcdec.h)
target_include_directories(dcfs_bcdec PUBLIC bcdec)
target_compile_definitions(dcfs_bcdec PUBLIC BCDEC_BC4BC5_PRECISE)
