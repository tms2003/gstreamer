# SPDX-FileCopyrightText: 2024 L. E. Segovia <amy@centricular.com>
# SPDX-License-Ref: LGPL-2.1-or-later

#[=======================================================================[.rst:
FindGStreamer
-------

Finds the GStreamer library.

Configuration
^^^^^^^^^^^^^

This module can be configured with the following variables:

``GStreamer_STATIC``
  Link against GStreamer statically (see below).

Imported Targets
^^^^^^^^^^^^^^^^

This module defines the following :prop_tgt:`IMPORTED` targets:

``GStreamer::GStreamer``
  The GStreamer library.

This module defines the following :prop_tgt:`SHARED` targets:

``GStreamer::Mobile``
  A target that will build the shared library consisting of GStreamer plus all the selected plugin components.

Result Variables
^^^^^^^^^^^^^^^^

This will define the following variables:

``GStreamer_FOUND``
  True if the system has the GStreamer library.
``GStreamer_VERSION``
  The version of the GStreamer library which was found.
``GStreamer_INCLUDE_DIRS``
  Include directories needed to use GStreamer.
``GStreamer_LIBRARIES``
  Libraries needed to link to GStreamer.

Cache Variables
^^^^^^^^^^^^^^^

The following cache variables may also be set:

``GStreamer_INCLUDE_DIR``
  The directory containing ``gst/gstversion.h``.
``GStreamer_LIBRARY``
  The path to the Foo library.

Configuration Variables
^^^^^^^^^^^^^^^

Setting the following variables is required, depending on the operating system:

``GStreamer_ROOT_DIR``
  Installation prefix of the Cerbero SDK.

``GStreamer_USE_STATIC_LIBS`
  Set to ON to force the use of the static libraries. Default is OFF.

``GStreamer_ASSETS_DIR``
  Target directory for deploying Android assets to.

``GStreamer_JAVA_SRC_DIR``
  Target directory for deploying the selected plugins' Android Java classfiles to.

``GSTREAMER_MOBILE_MODULE_NAME``
  Chosen name for the GStreamer::Mobile shared library e.g. ``gstreamer_android``.

``G_IO_MODULES``
  Set this to the GIO modules you need, additional to any GStreamer plugins. (Usually set to ``gnutls`` or ``openssl``)

#]=======================================================================]

if (GStreamer_FOUND)
    return()
endif()

#####################
#  Setup variables  #
#####################

if (NOT DEFINED GStreamer_ROOT_DIR AND DEFINED GSTREAMER_ROOT)
    set(GStreamer_ROOT_DIR ${GSTREAMER_ROOT})
endif()

if (NOT GStreamer_ROOT_DIR)
    set(GStreamer_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/../../")
endif()

if (NOT EXISTS "${GStreamer_ROOT_DIR}")
    message(FATAL_ERROR "The directory GStreamer_ROOT_DIR=${GStreamer_ROOT_DIR} does not exist")
endif()

if (ANDROID OR APPLE)
    set(GSTREAMER_IS_MOBILE ON)
else()
    set(GSTREAMER_IS_MOBILE OFF)
endif()

# Block shared GStreamer on mobile
if (GSTREAMER_IS_MOBILE)
    if ((DEFINED GStreamer_USE_STATIC_LIBS) AND (NOT GStreamer_USE_STATIC_LIBS))
        message(WARNING "Shared library GStreamer is not supported, falling back to static")
        set(GStreamer_USE_STATIC_LIBS ON)
    endif()
endif()

# Set up output variables for Android
if(ANDROID)
    if (NOT DEFINED GStreamer_JAVA_SRC_DIR AND DEFINED GSTREAMER_JAVA_SRC_DIR)
        set(GStreamer_JAVA_SRC_DIR ${GSTREAMER_JAVA_SRC_DIR})
    elseif(NOT DEFINED GStreamer_JAVA_SRC_DIR)
        set(GStreamer_JAVA_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src/")
    else()
        # Gradle does not let us access the root of the subproject
        # so we implement the ndk-build assumption ourselves
        set(GSTREAMER_JAVA_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../${GSTREAMER_JAVA_SRC_DIR}")
    endif()

    if(NOT DEFINED GStreamer_ASSETS_DIR AND DEFINED GSTREAMER_ASSETS_DIR)
        set(GStreamer_ASSETS_DIR ${GSTREAMER_ASSETS_DIR})
    elseif(NOT DEFINED GStreamer_ASSETS_DIR)
        set(GSTREAMER_ASSETS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src/main/assets/")
    else()
        # Same as above
        set(GSTREAMER_ASSETS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../${GSTREAMER_ASSETS_DIR}")
    endif()
elseif(IOS)
    set(GSTREAMER_JAVA_SRC_DIR "${CMAKE_CURRENT_BINARY_DIR}")

    if(NOT DEFINED GStreamer_ASSETS_DIR AND DEFINED GSTREAMER_ASSETS_DIR)
        set(GStreamer_ASSETS_DIR ${GSTREAMER_ASSETS_DIR})
    elseif(NOT DEFINED GStreamer_ASSETS_DIR)
        set(GSTREAMER_ASSETS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src/main/assets/")
    else()
        # Same as above
        set(GSTREAMER_ASSETS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../${GSTREAMER_ASSETS_DIR}")
    endif()
endif()

if(NOT DEFINED GSTREAMER_MOBILE_MODULE_NAME)
    if (DEFINED GSTREAMER_ANDROID_MODULE_NAME)
        set(GSTREAMER_MOBILE_MODULE_NAME gstreamer_android)
    elseif(ANDROID)
        set(GSTREAMER_MOBILE_MODULE_NAME gstreamer_android)
    else()
        set(GSTREAMER_MOBILE_MODULE_NAME gstreamer_mobile)
    endif()
endif()

# Set the environment for pkg-config
if (WIN32)
    set(ENV{PKG_CONFIG_PATH} "${GStreamer_ROOT_DIR}/lib/pkgconfig;${GStreamer_ROOT_DIR}/lib/gstreamer-1.0/pkgconfig;${GStreamer_ROOT_DIR}/lib/gio/modules/pkgconfig")
else()
    set(ENV{PKG_CONFIG_PATH} "${GStreamer_ROOT_DIR}/lib/pkgconfig:${GStreamer_ROOT_DIR}/lib/gstreamer-1.0/pkgconfig:${GStreamer_ROOT_DIR}/lib/gio/modules/pkgconfig")
endif()
# Path for the static GIO modules
set(G_IO_MODULES_PATH "${GStreamer_ROOT}/lib/gio/modules")

function(_gst_find_library LOCAL_LIB GST_LOCAL_LIB)
    if (DEFINED ${GST_LOCAL_LIB})
        return()
    endif()

    if (APPLE)
        set(_gst_names ${LOCAL_LIB}.a lib${LOCAL_LIB}.a ${LOCAL_LIB}.so lib${LOCAL_LIB}.so ${LOCAL_LIB}.dylib lib${LOCAL_LIB}.dylib ${LOCAL_LIB}.tbd lib${LOCAL_LIB}.tbd)
    elseif (UNIX)
        set(_gst_names ${LOCAL_LIB}.a lib${LOCAL_LIB}.a ${LOCAL_LIB}.so lib${LOCAL_LIB}.so)
    else()
        set(_gst_names ${LOCAL_LIB}.a lib${LOCAL_LIB}.a ${LOCAL_LIB}.lib lib${LOCAL_LIB}.lib)
    endif()

    if ("${LOCAL_LIB}" IN_LIST _gst_IGNORED_SYSTEM_LIBRARIES)
        set(${GST_LOCAL_LIB} ${LOCAL_LIB} PARENT_SCOPE)
    else()
        find_library(${GST_LOCAL_LIB}
            NAMES ${_gst_names}
            HINTS ${ARGN}
            NO_DEFAULT_PATH
            REQUIRED
        )
        set(${GST_LOCAL_LIB} "${${GST_LOCAL_LIB}}" PARENT_SCOPE)
        if (NOT ${GST_LOCAL_LIB})
            message(FATAL_ERROR "${LOCAL_LIB} was unexpectedly not found.")
        endif()
    endif()
endfunction()

macro(_gst_apply_link_libraries PC_LIBRARIES PC_HINTS GST_TARGET)
    foreach(LOCAL_LIB IN LISTS ${PC_LIBRARIES})
    if (LOCAL_LIB MATCHES "${_gst_SRT_REGEX_PATCH}")
        string(REGEX REPLACE "${_gst_SRT_REGEX_PATCH}" "\\1" LOCAL_LIB "${LOCAL_LIB}")
    endif()
    string(MAKE_C_IDENTIFIER "_gst_${LOCAL_LIB}" GST_LOCAL_LIB)
    if (NOT ${GST_LOCAL_LIB})
        _gst_find_library(${LOCAL_LIB} ${GST_LOCAL_LIB} ${${PC_HINTS}})
    endif()
    target_link_libraries(${GST_TARGET} INTERFACE
        ${${GST_LOCAL_LIB}})
    endforeach()
endmacro()

################################
#      Set up the targets      #
################################

# for setting the default GTlsDatabase
if(mobile IN_LIST GStreamer_FIND_COMPONENTS)
    list(APPEND GSTREAMER_EXTRA_DEPS gio-2.0)
endif()

# Prepare Android hotfixes for x264
if(ANDROID_ABI STREQUAL "armeabi")
    set(NEEDS_NOTEXT_FIX TRUE)
    set(NEEDS_BSYMBOLIC_FIX TRUE)
elseif(ANDROID_ABI STREQUAL "x86")
    set(NEEDS_NOTEXT_FIX TRUE)
    set(NEEDS_BSYMBOLIC_FIX TRUE)
elseif(ANDROID_ABI STREQUAL "x86_64")
    set(NEEDS_BSYMBOLIC_FIX TRUE)
endif()

find_package(PkgConfig REQUIRED)

# GStreamer's pkg-config modules are a MUST -- but we'll test them below
pkg_check_modules(PC_GStreamer gstreamer-1.0 ${GSTREAMER_EXTRA_DEPS})
# Simulate the list that'll be wholearchive'd.
# Unfortunately, this uses an option only available with pkgconf.
# set(_old_pkg_config_executable "${PKG_CONFIG_EXECUTABLE}")
# set(PKG_CONFIG_EXECUTABLE ${PKG_CONFIG_EXECUTABLE} --maximum-traverse-depth=1)
# pkg_check_modules(PC_GStreamer_NoDeps QUIET REQUIRED gstreamer-1.0 ${GSTREAMER_EXTRA_DEPS})
# set(PKG_CONFIG_EXECUTABLE "${_old_pkg_config_executable}")

set(GStreamer_VERSION "${PC_GStreamer_VERSION}")

# Test validity of the paths
find_path(GStreamer_INCLUDE_DIR
    NAMES gst/gstversion.h
    PATHS ${PC_GStreamer_INCLUDE_DIRS}
    PATH_SUFFIXES gstreamer-1.0
    NO_DEFAULT_PATH
)

find_library(GStreamer_LIBRARY
    NAMES gstreamer-1.0
    PATHS ${PC_GStreamer_LIBRARY_DIRS}
    NO_DEFAULT_PATH
)

# Android: Ignore these libraries when constructing the IMPORTED_LOCATION
set(_gst_IGNORED_SYSTEM_LIBRARIES c unwind m dl)
if (ANDROID)
    list(APPEND _gst_IGNORED_SYSTEM_LIBRARIES log GLESv2 EGL OpenSLES)
elseif(APPLE)
    list(APPEND _gst_IGNORED_SYSTEM_LIBRARIES iconv resolv)
endif()

# Normalize library flags coming from srt/haisrt
# https://github.com/Haivision/srt/commit/b90b64d26f850fb0efcc4cdd8b31cbf74bd4db0c
set(_gst_SRT_REGEX_PATCH "^:lib.+\\.(a|so|lib|dylib)$")

if(PC_GStreamer_FOUND AND (NOT TARGET GStreamer::GStreamer))
    # This is not UNKNOWN but INTERFACE, as we only intend to
    # make a target suitable for downstream consumption.
    # FindPkgConfig already takes care of things, however it is totally unable
    # to discern between shared and static libraries when populating
    # xxx_STATIC_LINK_LIBRARIES, so we need to populate them manually.
    add_library(GStreamer::GStreamer INTERFACE IMPORTED)

    if (GStreamer_USE_STATIC_LIBS)
        set_target_properties(GStreamer::GStreamer PROPERTIES
            INTERFACE_COMPILE_OPTIONS "${PC_GStreamer_STATIC_CFLAGS_OTHER}"
            INTERFACE_INCLUDE_DIRECTORIES "${PC_GStreamer_STATIC_INCLUDE_DIRS}"
        )
        if (APPLE)
            # LDFLAGS_OTHER may include framework linkage. Because CMake
            # iterates over arguments separated by spaces, it doesn't realise
            # that those arguments must not be split.
            set(new_ldflags)
            set(assemble_framework FALSE)
            foreach(_arg IN LISTS PC_GStreamer_STATIC_LDFLAGS_OTHER)
                if (assemble_framework)
                    set(assemble_framework FALSE)
                    find_library(GST_${_arg}_LIB ${_arg} REQUIRED)
                    target_link_libraries(GStreamer::GStreamer
                        INTERFACE
                            "${GST_${_arg}_LIB}"
                    )
                elseif (_arg STREQUAL "-framework")
                    set(assemble_framework TRUE)
                else()
                    set(assemble_framework FALSE)
                    list(APPEND new_ldflags "${_arg}")
                endif()
            endforeach()
            set_target_properties(GStreamer::GStreamer PROPERTIES
                INTERFACE_LINK_OPTIONS "${new_ldflags}"
            )
        else()
            set_target_properties(GStreamer::GStreamer PROPERTIES
                INTERFACE_LINK_OPTIONS "${PC_GStreamer_STATIC_LDFLAGS_OTHER}"
            )
        endif()
    else()
        set_target_properties(GStreamer::GStreamer PROPERTIES
            INTERFACE_COMPILE_OPTIONS "${PC_GStreamer_CFLAGS_OTHER}"
            INTERFACE_INCLUDE_DIRECTORIES "${PC_GStreamer_INCLUDE_DIRS}"
            INTERFACE_LINK_OPTIONS "${PC_GStreamer_LDFLAGS_OTHER}"
        )
    endif()

    if (ANDROID)
        target_link_options(GStreamerAndroid INTERFACE -fuse-ld=lld)
    endif()

    if (NOT GStreamer_USE_STATIC_LIBS)
        set_target_properties(GStreamer::GStreamer PROPERTIES
            INTERFACE_LINK_LIBRARIES "${PC_GStreamer_LINK_LIBRARIES}"
        )
        # We're done
    else()
        # Handle all libraries, even those specified with -l:libyadda.a (srt)
        # Due to the unavailability of pkgconf's option globally, I attempt to
        # simulate it through the shared libraries listing.
        # If pkgconf is available, replace all PC_GStreamer_ entries with
        # PC_GStreamer_NoDeps and uncomment the code block above.
        foreach(LOCAL_LIB IN LISTS PC_GStreamer_LIBRARIES)
            # list(TRANSFORM REPLACE) is of no use here
            # https://gitlab.kitware.com/cmake/cmake/-/issues/16899
            if (LOCAL_LIB MATCHES "${_gst_SRT_REGEX_PATCH}")
                string(REGEX REPLACE "${_gst_SRT_REGEX_PATCH}" "\\1" LOCAL_LIB "${LOCAL_LIB}")
            endif()
            string(MAKE_C_IDENTIFIER "_gst_${LOCAL_LIB}" GST_LOCAL_LIB)
            if (NOT ${GST_LOCAL_LIB})
                _gst_find_library(${LOCAL_LIB} ${GST_LOCAL_LIB} ${PC_GStreamer_STATIC_LIBRARY_DIRS})
            endif()
            if ("${${GST_LOCAL_LIB}}" IN_LIST _gst_IGNORED_SYSTEM_LIBRARIES)
                target_link_libraries(GStreamer::GStreamer INTERFACE
                    "${${GST_LOCAL_LIB}}"
                )
            elseif (MSVC)
                target_link_libraries(GStreamer::GStreamer INTERFACE
                    "/WHOLEARCHIVE:${${GST_LOCAL_LIB}}"
                )
            elseif(APPLE)
                target_link_libraries(GStreamer::GStreamer INTERFACE
                "-Wl,-force_load,${${GST_LOCAL_LIB}}"
            )
            else()
                target_link_libraries(GStreamer::GStreamer INTERFACE
                    "-Wl,--whole-archive,${${GST_LOCAL_LIB}},--no-whole-archive"
                )
            endif()
        endforeach()

        _gst_apply_link_libraries(PC_GStreamer_STATIC_LIBRARIES PC_GStreamer_STATIC_LIBRARY_DIRS GStreamer::GStreamer)
    endif()
endif()

# Now, let's set up targets for each of the components supplied
set(_gst_CUSTOM_TARGETS mobile ca_certificates fonts)
if (PC_GStreamer_FOUND)
    # These are the required plugins
    set(GSTREAMER_PLUGINS ${GStreamer_FIND_COMPONENTS})
    list(REMOVE_DUPLICATES GSTREAMER_PLUGINS)
    # These are custom handled targets, and must be skipped from the loop
    list(REMOVE_ITEM GSTREAMER_PLUGINS ${_gst_CUSTOM_TARGETS})
    # These are the API packages
    set(GSTREAMER_APIS ${GSTREAMER_PLUGINS})
    list(FILTER GSTREAMER_APIS INCLUDE REGEX "^api_")
    # Filter them out, although they're handled the same
    # they cannot be considered for the purposes of initialization
    list(FILTER GSTREAMER_PLUGINS EXCLUDE REGEX "^api_")
endif()

if (PC_GStreamer_FOUND AND GSTREAMER_IS_MOBILE AND (mobile IN_LIST GStreamer_FIND_COMPONENTS) AND (NOT TARGET GStreamer::Mobile))
    # Generate the plugins' declaration strings
    # (don't append a semicolon, CMake does it as part of the list)
    list(TRANSFORM GSTREAMER_PLUGINS
        PREPEND "\nGST_PLUGIN_STATIC_DECLARE\("
        OUTPUT_VARIABLE PLUGINS_DECLARATION
    )
    list(TRANSFORM PLUGINS_DECLARATION
        APPEND "\)"
        OUTPUT_VARIABLE PLUGINS_DECLARATION
    )
    if(PLUGINS_DECLARATION)
        set(PLUGINS_DECLARATION "${PLUGINS_DECLARATION};")
    endif()

    # Generate the plugins' registration strings
    list(TRANSFORM GSTREAMER_PLUGINS
        PREPEND "\nGST_PLUGIN_STATIC_REGISTER\("
        OUTPUT_VARIABLE PLUGINS_REGISTRATION
    )
    list(TRANSFORM PLUGINS_REGISTRATION
        APPEND "\)"
        OUTPUT_VARIABLE PLUGINS_REGISTRATION
    )
    if(PLUGINS_REGISTRATION)
        set(PLUGINS_REGISTRATION "${PLUGINS_REGISTRATION};")
    endif()

    # Generate list of gio modules
    if (NOT G_IO_MODULES)
        message(WARNING "G_IO_MODULES is not set, assuming OpenSSL as TLS backend.")
        set (G_IO_MODULES openssl)
    endif()

    list(TRANSFORM G_IO_MODULES
        PREPEND "gio"
        OUTPUT_VARIABLE G_IO_MODULES_LIBS
    )
    list(TRANSFORM G_IO_MODULES
        PREPEND "\nGST_G_IO_MODULE_DECLARE\("
        OUTPUT_VARIABLE G_IO_MODULES_DECLARE
    )
    list(TRANSFORM G_IO_MODULES_DECLARE
        APPEND "\);"
        OUTPUT_VARIABLE G_IO_MODULES_DECLARE
    )
    if(G_IO_MODULES_DECLARE)
        set(G_IO_MODULES_DECLARE "${G_IO_MODULES_DECLARE};")
    endif()
    list(TRANSFORM G_IO_MODULES
        PREPEND "\nGST_G_IO_MODULE_LOAD\("
        OUTPUT_VARIABLE G_IO_MODULES_LOAD
    )
    list(TRANSFORM G_IO_MODULES_LOAD
        APPEND "\)"
        OUTPUT_VARIABLE G_IO_MODULES_LOAD
    )
    if(G_IO_MODULES_LOAD)
        set(G_IO_MODULES_LOAD "${G_IO_MODULES_LOAD};")
    endif()

    # Generates a source file that declares and registers all the required plugins
    if (ANDROID)
        configure_file(
            "${CMAKE_CURRENT_LIST_DIR}/gstreamer_android-1.0.c.in"
            "${GSTREAMER_MOBILE_MODULE_NAME}.c"
        )
    else()
        configure_file(
            "${CMAKE_CURRENT_LIST_DIR}/gst_ios_init.m.in"
            "${GSTREAMER_MOBILE_MODULE_NAME}.m"
        )
    endif()

    # Creates a shared library including gstreamer, its plugins and all the dependencies
    if (ANDROID)
        add_library(GStreamerMobile
            SHARED
                "${GSTREAMER_MOBILE_MODULE_NAME}.c"
        )
    else()
        add_library(GStreamerMobile SHARED)
        enable_language(OBJC OBJCXX)
        target_sources(GStreamerMobile
            PRIVATE
                "${GSTREAMER_MOBILE_MODULE_NAME}.m"
        )
        set_source_files_properties("${GSTREAMER_MOBILE_MODULE_NAME}.m"
            PROPERTIES
                LANGUAGE OBJC
        )
        find_library(Foundation_LIB Foundation REQUIRED)
        target_link_libraries(GStreamerMobile
            PRIVATE
                ${Foundation_LIB}
        )
    endif()
    add_library(GStreamer::Mobile ALIAS GStreamerMobile)

    # Assume it's C++ for the sake of gstsoundtouch
    if (APPLE)
        set_target_properties(
            GStreamerMobile
            PROPERTIES
                LINKER_LANGUAGE OBJCXX
        )
    else()
        set_target_properties(
            GStreamerMobile
            PROPERTIES
                LINKER_LANGUAGE CXX
        )
    endif()
    set_target_properties(
        GStreamerMobile
        PROPERTIES
            NO_SONAME TRUE
            LIBRARY_OUTPUT_NAME ${GSTREAMER_MOBILE_MODULE_NAME}
            FRAMEWORK TRUE
            FRAMEWORK_VERSION C
            MACOSX_FRAMEWORK_IDENTIFIER org.gstreamer.GStreamerMobile
            VERSION ${PC_GStreamer_VERSION}
            SOVERSION ${PC_GStreamer_VERSION}
    )

    # Compartmentalize the Android module...
    target_link_libraries(
        GStreamerMobile
        PRIVATE
            GStreamer::GStreamer
    )

    # But allow downstream consumers to call GStreamer up
    target_include_directories(
        GStreamerMobile
        INTERFACE
            $<TARGET_PROPERTY:GStreamer::GStreamer,INTERFACE_INCLUDE_DIRECTORIES>
    )

    # Text relocations are required for all 32-bit objects. We
    # must disable the warning to allow linking with lld. Unlike gold, ld which
    # will silently allow text relocations, lld support must be explicit.
    #
    # See https://crbug.com/911658#c19 for more information. See also
    # https://trac.ffmpeg.org/ticket/7878
    if(DEFINED NEEDS_NOTEXT_FIX)
        target_link_options(
            GStreamerMobile
            PRIVATE
                "-Wl,-z,notext"
        )
    endif()

    # resolve textrels in the x86 asm
    if(DEFINED NEEDS_BSYMBOLIC_FIX)
        target_link_options(
            GStreamerMobile
            PRIVATE
                "-Wl,-Bsymbolic"
        )
    endif()

    if (ANDROID)
        # Collect all Java-based initializer classes
        set(GSTREAMER_PLUGINS_CLASSES)
        foreach(LOCAL_PLUGIN IN LISTS GSTREAMER_PLUGINS)
            file(GLOB_RECURSE
                LOCAL_PLUGIN_CLASS
                FOLLOW_SYMLINKS
                RELATIVE "${GSTREAMER_NDK_BUILD_PATH}"
                "${GSTREAMER_NDK_BUILD_PATH}/${LOCAL_PLUGIN}/*.java"
            )
            list(APPEND GSTREAMER_PLUGINS_CLASSES ${LOCAL_PLUGIN_CLASS})
        endforeach()

        # Same as above, but collect the plugins themselves
        set(GSTREAMER_PLUGINS_WITH_CLASSES)
        foreach(LOCAL_PLUGIN IN LISTS GSTREAMER_PLUGINS)
            if(EXISTS "${GSTREAMER_NDK_BUILD_PATH}/${LOCAL_PLUGIN}/")
                list(APPEND GSTREAMER_PLUGINS_WITH_CLASSES ${LOCAL_PLUGIN})
            endif()
        endforeach()

        add_custom_target(
            "copyjavasource_${ANDROID_ABI}"
        )

        foreach(LOCAL_FILE IN LISTS GSTREAMER_PLUGINS_CLASSES)
            string(MAKE_C_IDENTIFIER "cp_${LOCAL_FILE}" COPYJAVASOURCE_TGT)
            add_custom_target(
                ${COPYJAVASOURCE_TGT}
                COMMAND
                    "${CMAKE_COMMAND}" -E copy
                    "${GSTREAMER_NDK_BUILD_PATH}/${LOCAL_FILE}"
                    "${GSTREAMER_JAVA_SRC_DIR}/org/freedesktop/gstreamer/"
                BYPRODUCTS
                    "${GSTREAMER_JAVA_SRC_DIR}/org/freedesktop/gstreamer/${LOCAL_FILE}"
            )
            add_dependencies(copyjavasource_${ANDROID_ABI} ${COPYJAVASOURCE_TGT})
        endforeach()

        # FIXME: Make GStreamer.java configure friendly
        file(READ "${GSTREAMER_NDK_BUILD_PATH}/GStreamer.java" JAVA_INPUT)
        if(GSTREAMER_INCLUDE_FONTS)
            string(REPLACE "//copyCaCertificates" "copyCaCertificates" JAVA_INPUT "${JAVA_INPUT}")
        endif()
        if(GSTREAMER_INCLUDE_CA_CERTIFICATES)
            string(REPLACE "//copyFonts" "copyFonts" JAVA_INPUT "${JAVA_INPUT}")
        endif()
        file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/GStreamer.java" "${JAVA_INPUT}")

        add_custom_target(
            enable_includes_in_gstreamer_java
            COMMAND
                "${CMAKE_COMMAND}" -E copy
                "${CMAKE_CURRENT_BINARY_DIR}/GStreamer.java"
                "${GSTREAMER_JAVA_SRC_DIR}/org/freedesktop/gstreamer/GStreamer.java"
            BYPRODUCTS
                "${GSTREAMER_JAVA_SRC_DIR}/org/freedesktop/gstreamer/GStreamer.java"
        )
        add_dependencies(copyjavasource_${ANDROID_ABI} enable_includes_in_gstreamer_java)
        add_dependencies(GStreamerMobile copyjavasource_${ANDROID_ABI})
    endif()

    # And, finally, set the GIO modules up
    pkg_check_modules(PC_GStreamerGioModules REQUIRED ${G_IO_MODULES_LIBS})
    add_library(GStreamer::gio_modules INTERFACE IMPORTED)
    set_target_properties(
        GStreamer::gio_modules
        PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${PC_GStreamerGioModules_STATIC_INCLUDE_DIRS}"
            INTERFACE_COMPILE_OPTIONS "${PC_GStreamerGioModules_STATIC_CFLAGS_OTHER}"
            INTERFACE_LINK_OPTIONS "${PC_GStreamerGioModules_STATIC_LDFLAGS_OTHER}"
    )
    _gst_apply_link_libraries(PC_GStreamerGioModules_STATIC_LIBRARIES PC_GStreamerGioModules_STATIC_LIBRARY_DIRS GStreamer::gio_modules)
    target_link_libraries(
        GStreamerMobile
        PRIVATE
            GStreamer::gio_modules
    )
    set(GStreamer_mobile_FOUND TRUE)
endif()

if(PC_GStreamer_FOUND AND (fonts IN_LIST GStreamer_FIND_COMPONENTS))
    if (ANDROID AND (NOT TARGET copyfontsres_${ANDROID_ABI}))
        add_custom_target(
            copyfontsres_${ANDROID_ABI}
            ${CMAKE_COMMAND} -E copy
                "${CMAKE_CURRENT_LIST_DIR}/fontconfig/fonts/Ubuntu-R.ttf"
                "${GSTREAMER_ASSETS_DIR}/fontconfig/fonts/truetype/"
            COMMAND ${CMAKE_COMMAND} -E copy
                "${CMAKE_CURRENT_LIST_DIR}/fontconfig/fonts.conf"
                "${GSTREAMER_ASSETS_DIR}/fontconfig/"
            BYPRODUCTS
                "${GSTREAMER_ASSETS_DIR}/fontconfig/fonts/truetype/Ubuntu-R.ttf"
                "${GSTREAMER_ASSETS_DIR}/fontconfig/fonts.conf"
        )

        if (TARGET GStreamerMobile)
            add_dependencies(GStreamerMobile copyfontsres_${ANDROID_ABI})
        endif()
    elseif(IOS)
        list(APPEND GSTREAMER_RESOURCES "${GSTREAMER_NDK_BUILD_PATH}/fontconfig/fonts.conf")
        list(APPEND GSTREAMER_RESOURCES "${GSTREAMER_ASSETS_DIR}/fontconfig/fonts/truetype/Ubuntu-R.ttf")
    endif()
endif()

if(PC_GStreamer_FOUND AND (ca_certificates IN_LIST GStreamer_FIND_COMPONENTS))
    if (ANDROID AND (NOT TARGET copycacertificatesres_${ANDROID_ABI}))
        add_custom_target(
            copycacertificatesres_${ANDROID_ABI}
            ${CMAKE_COMMAND} -E copy
                "${GSTREAMER_ROOT}/etc/ssl/certs/ca-certificates.crt"
                "${GSTREAMER_ASSETS_DIR}/ssl/certs/"
            BYPRODUCTS "${GSTREAMER_ASSETS_DIR}/ssl/certs/ca-certificates.crt"
        )

        if (TARGET GStreamerMobile)
            add_dependencies(GStreamerMobile copycacertificatesres_${ANDROID_ABI})
        endif()
    elseif (IOS)
        list(APPEND GSTREAMER_RESOURCES "${GSTREAMER_ROOT}/etc/ssl/certs/ca-certificates.crt")
    endif()
endif()

foreach(_gst_PLUGIN IN LISTS GSTREAMER_PLUGINS)
    # Safety valve for the custom targets above
    if ("${_gst_plugin}" IN_LIST _gst_CUSTOM_TARGETS)
        continue()
    endif()

    if (TARGET GStreamer::${_gst_PLUGIN})
        continue()
    endif()

    if (GStreamer_FIND_REQUIRED_${_gst_PLUGIN})
        set(_gst_PLUGIN_REQUIRED REQUIRED)
    else()
        set(_gst_PLUGIN_REQUIRED)
    endif()

    pkg_check_modules(PC_GStreamer_${_gst_PLUGIN} "gst${_gst_PLUGIN}")

    set(GStreamer_${_gst_PLUGIN}_FOUND "${PC_GStreamer_${_gst_PLUGIN}_FOUND}")
    if (NOT GStreamer_${_gst_PLUGIN}_FOUND)
        continue()
    endif()

    add_library(GStreamer::${_gst_PLUGIN} INTERFACE IMPORTED)
    set_target_properties(GStreamer::${_gst_PLUGIN} PROPERTIES
        INTERFACE_COMPILE_OPTIONS "${PC_GStreamer_${_gst_PLUGIN}_CFLAGS_OTHER}"
        INTERFACE_INCLUDE_DIRECTORIES "${PC_GStreamer_${_gst_PLUGIN}_INCLUDE_DIRS}"
    )
    if (GStreamer_USE_STATIC_LIBS)
        if (APPLE)
            # LDFLAGS_OTHER may include framework linkage. Because CMake
            # iterates over arguments separated by spaces, it doesn't realise
            # that those arguments must not be split.
            set(new_ldflags)
            set(assemble_framework FALSE)
            foreach(_arg IN LISTS PC_GStreamer_${_gst_PLUGIN}_STATIC_LDFLAGS_OTHER)
                if (assemble_framework)
                    set(assemble_framework FALSE)
                    find_library(GST_${_arg}_LIB ${_arg} REQUIRED)
                    target_link_libraries(GStreamer::${_gst_PLUGIN}
                        INTERFACE
                            "${GST_${_arg}_LIB}"
                    )
                elseif (_arg STREQUAL "-framework")
                    set(assemble_framework TRUE)
                else()
                    set(assemble_framework FALSE)
                    list(APPEND new_ldflags "${_arg}")
                endif()
            endforeach()
            set_target_properties(GStreamer::${_gst_PLUGIN} PROPERTIES
                INTERFACE_LINK_OPTIONS "${new_ldflags}"
            )
        else()
            set_target_properties(GStreamer::${_gst_PLUGIN} PROPERTIES
                INTERFACE_LINK_OPTIONS "${PC_GStreamer_${_gst_PLUGIN}_STATIC_LDFLAGS_OTHER}"
            )
        endif()
    else()
        set_target_properties(GStreamer::${_gst_PLUGIN} PROPERTIES
            INTERFACE_LINK_OPTIONS "${PC_GStreamer_${_gst_PLUGIN}_LDFLAGS_OTHER}"
            INTERFACE_LINK_LIBRARIES "${PC_GStreamer_${_gst_PLUGIN}_LINK_LIBRARIES}"
        )
        # We're done
        continue()
    endif()

    # Handle all libraries, even those specified with -l:libyadda.a (srt)
    _gst_apply_link_libraries(PC_GStreamer_${_gst_PLUGIN}_STATIC_LIBRARIES PC_GStreamer_${_gst_PLUGIN}_STATIC_LIBRARY_DIRS GStreamer::${_gst_PLUGIN})

    if (TARGET GStreamerMobile)
        target_link_libraries(
            GStreamerMobile
            PRIVATE
                GStreamer::${_gst_PLUGIN}
        )
    endif()
endforeach()

foreach(_gst_PLUGIN IN LISTS GSTREAMER_APIS)
    # Safety valve for the custom targets above
    if ("${_gst_plugin}" IN_LIST _gst_CUSTOM_TARGETS)
        continue()
    endif()

    if (TARGET GStreamer::${_gst_PLUGIN})
        continue()
    endif()

    if (GStreamer_FIND_REQUIRED_${_gst_PLUGIN})
        set(_gst_PLUGIN_REQUIRED REQUIRED)
    else()
        set(_gst_PLUGIN_REQUIRED)
    endif()

    string(REGEX REPLACE "^api_(.+)" "\\1" _gst_PLUGIN_PC "${_gst_PLUGIN}")
    string(REPLACE "_" "-" _gst_PLUGIN_PC "${_gst_PLUGIN_PC}")

    pkg_check_modules(PC_GStreamer_${_gst_PLUGIN} "gstreamer-${_gst_PLUGIN_PC}-1.0")

    set(GStreamer_${_gst_PLUGIN}_FOUND "${PC_GStreamer_${_gst_PLUGIN}_FOUND}")
    if (NOT GStreamer_${_gst_PLUGIN}_FOUND)
        continue()
    endif()

    add_library(GStreamer::${_gst_PLUGIN} INTERFACE IMPORTED)
    set_target_properties(GStreamer::${_gst_PLUGIN} PROPERTIES
        INTERFACE_COMPILE_OPTIONS "${PC_GStreamer_${_gst_PLUGIN}_CFLAGS_OTHER}"
        INTERFACE_INCLUDE_DIRECTORIES "${PC_GStreamer_${_gst_PLUGIN}_INCLUDE_DIRS}"
        INTERFACE_LINK_OPTIONS "${PC_GStreamer_${_gst_PLUGIN}_LDFLAGS_OTHER}"
    )
    if (GStreamer_USE_STATIC_LIBS)
        if (APPLE)
            # LDFLAGS_OTHER may include framework linkage. Because CMake
            # iterates over arguments separated by spaces, it doesn't realise
            # that those arguments must not be split.
            set(new_ldflags)
            set(assemble_framework FALSE)
            foreach(_arg IN LISTS PC_GStreamer_${_gst_PLUGIN}_STATIC_LDFLAGS_OTHER)
                if (assemble_framework)
                    set(assemble_framework FALSE)
                    find_library(GST_${_arg}_LIB ${_arg} REQUIRED)
                    target_link_libraries(GStreamer::${_gst_PLUGIN}
                        INTERFACE
                            "${GST_${_arg}_LIB}"
                    )
                elseif (_arg STREQUAL "-framework")
                    set(assemble_framework TRUE)
                else()
                    set(assemble_framework FALSE)
                    list(APPEND new_ldflags "${_arg}")
                endif()
            endforeach()
            set_target_properties(GStreamer::${_gst_PLUGIN} PROPERTIES
                INTERFACE_LINK_OPTIONS "${new_ldflags}"
            )
        else()
            set_target_properties(GStreamer::${_gst_PLUGIN} PROPERTIES
                INTERFACE_LINK_OPTIONS "${PC_GStreamer_${_gst_PLUGIN}_STATIC_LDFLAGS_OTHER}"
            )
        endif()
    else()
        set_target_properties(GStreamer::${_gst_PLUGIN} PROPERTIES
            INTERFACE_LINK_OPTIONS "${PC_GStreamer_${_gst_PLUGIN}_LDFLAGS_OTHER}"
            INTERFACE_LINK_LIBRARIES "${PC_GStreamer_${_gst_PLUGIN}_LINK_LIBRARIES}"
        )
        # We're done
        continue()
    endif()

    # Handle all libraries, even those specified with -l:libyadda.a (srt)

    _gst_apply_link_libraries(PC_GStreamer_${_gst_PLUGIN}_STATIC_LIBRARIES PC_GStreamer_${_gst_PLUGIN}_STATIC_LIBRARY_DIRS GStreamer::${_gst_PLUGIN})

    if (TARGET GStreamerMobile)
        target_link_libraries(
            GStreamerMobile
            PRIVATE
                GStreamer::${_gst_PLUGIN}
        )
    endif()
endforeach()

if (TARGET GStreamerMobile AND GSTREAMER_RESOURCES)
    set_target_properties(
        GStreamerMobile
        PROPERTIES
            RESOURCE "${GSTREAMER_RESOURCES}"
    )
endif()

# Perform final validation
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(GStreamer
    FOUND_VAR GStreamer_FOUND
    REQUIRED_VARS
        GStreamer_LIBRARY
        GStreamer_INCLUDE_DIR
    VERSION_VAR GStreamer_VERSION
    HANDLE_VERSION_RANGE
    HANDLE_COMPONENTS
)
