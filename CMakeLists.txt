# SPDX-FileCopyrightText: Copyright 2026 dcfs-browser Project
# SPDX-License-Identifier: GPL-2.0-or-later

cmake_minimum_required(VERSION 3.19)
project(dcfs-browser VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets)

if(MSVC)
    cmake_policy(SET CMP0091 NEW)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    add_compile_options(/utf-8)
endif()

foreach(dep bcdec lz4 zlib)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/externals/${dep}/README.md"
       AND NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/externals/${dep}/README")
        message(FATAL_ERROR "externals/${dep} is empty: run `git submodule update --init --recursive`")
    endif()
endforeach()
add_subdirectory(externals)

# The archive code (no widgets), so a command line tool or tests can reuse it.
add_library(evofs STATIC
    src/binary_reader.h
    src/evo_data_file.cpp src/evo_data_file.h
    src/evo_file_system.cpp src/evo_file_system.h
    src/evo_index.cpp src/evo_index.h
    src/evo_reader.cpp src/evo_reader.h
    src/file_list.cpp src/file_list.h
    src/resource_export.cpp src/resource_export.h
    src/resource_pack.cpp src/resource_pack.h
    src/texture.cpp src/texture.h
    src/text_table.cpp src/text_table.h
    src/texture_batch.cpp src/texture_batch.h
    src/version.cpp src/version.h
)
target_include_directories(evofs PUBLIC src)
target_link_libraries(evofs PUBLIC Qt6::Core Qt6::Gui PRIVATE dcfs_bcdec dcfs_lz4 dcfs_zlib)

add_executable(dcfs-browser WIN32 MACOSX_BUNDLE
    gui/archive_scanner.cpp gui/archive_scanner.h
    gui/extract_job.cpp gui/extract_job.h
    gui/file_tree_model.cpp gui/file_tree_model.h
    gui/gallery_window.cpp gui/gallery_window.h
    gui/main.cpp
    gui/main_window.cpp gui/main_window.h
    gui/pack_window.cpp gui/pack_window.h
    gui/texture_export_job.cpp gui/texture_export_job.h
    gui/texture_previewer.cpp gui/texture_previewer.h
    gui/thumbnail_loader.cpp gui/thumbnail_loader.h
)
target_link_libraries(dcfs-browser PRIVATE evofs Qt6::Widgets)
set_target_properties(dcfs-browser PROPERTIES AUTOMOC ON)

foreach(target evofs dcfs-browser)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
endforeach()

if(WIN32)
    option(DCFS_DEPLOY_QT "Run windeployqt after building" ON)
    if(DCFS_DEPLOY_QT)
        find_program(WINDEPLOYQT_EXECUTABLE windeployqt
            HINTS "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}" "${Qt6_DIR}/../../../bin")
        if(WINDEPLOYQT_EXECUTABLE)
            add_custom_command(TARGET dcfs-browser POST_BUILD
                COMMAND "${WINDEPLOYQT_EXECUTABLE}" --no-translations --no-system-d3d-compiler --no-opengl-sw
                        "$<TARGET_FILE:dcfs-browser>"
                COMMENT "Deploying Qt next to dcfs-browser.exe"
                VERBATIM)
        else()
            message(WARNING "windeployqt not found in Qt's bin folder: Qt DLLs will not be copied")
        endif()
    endif()
endif()

install(TARGETS dcfs-browser RUNTIME DESTINATION bin BUNDLE DESTINATION .)
