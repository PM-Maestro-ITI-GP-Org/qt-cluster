cmake_minimum_required(VERSION 3.16)
cmake_policy(SET CMP0009 NEW)
# deploy_qt.cmake — deploy only the needed Qt + system dependencies

# ============================================
# 1. Qt shared libraries
# ============================================
# Emptied first, so the tree is always built from the Qt that is here NOW.
#
# The recursive copy below skips any soname already present (a cheap
# already-deployed guard), which quietly turns into a trap the moment Qt itself
# changes underneath an existing deploy tree: the stale copy is kept, and only
# libraries that did not exist before get copied in. Rebuilding against a Qt
# newly built with OpenGL produced exactly that -- a no-opengl libQt6Gui sitting
# beside a brand-new libQt6OpenGL, and on the target:
#
#     unknown symbol: _ZN14QOpenGLContext11makeCurrentEP8QSurface
#         referenced from libQt6OpenGL.so.6
#
# forty times over, because the QOpenGL* classes live in QtGui and the old one
# does not export them. The walk has its own _processed visited-list, so
# nothing here needs the filesystem to remember what it did.
file(REMOVE_RECURSE ${DEPLOY_LIB_DIR})
file(MAKE_DIRECTORY ${DEPLOY_LIB_DIR})

# ============================================
# 2. QNX platform plugin
# ============================================
if(DEFINED QT_PLUGIN_DIR)
    file(MAKE_DIRECTORY ${DEPLOY_PLUGIN_DIR}/platforms)
    file(GLOB _plugin "${QT_PLUGIN_DIR}/platforms/libqqnx.so*")
    if(_plugin)
        execute_process(COMMAND cp -a ${_plugin} ${DEPLOY_PLUGIN_DIR}/platforms/)
        message(STATUS "Deployed QNX platform plugin")
    endif()
endif()

# ============================================
# 3. Qt image format plugins (SVG, JPEG, PNG, etc.)
# ============================================
if(DEFINED QT_PLUGIN_DIR)
    file(MAKE_DIRECTORY ${DEPLOY_PLUGIN_DIR})
    file(GLOB _imgplugins "${QT_PLUGIN_DIR}/imageformats/libqsvg.so*")
    if(_imgplugins)
        file(COPY ${QT_PLUGIN_DIR}/imageformats DESTINATION ${DEPLOY_PLUGIN_DIR})
        message(STATUS "Deployed image format plugins (SVG, etc.)")
    endif()
endif()

# ============================================
# 4. QML modules
# ============================================
file(MAKE_DIRECTORY ${DEPLOY_QML_DIR})
foreach(_mod QtQuick QtQuick.Shapes QtQuick.Effects QtQuick.Controls QtQuick.Layouts QtQml)
    if(EXISTS "${QT_QML_DIR}/${_mod}")
        file(COPY "${QT_QML_DIR}/${_mod}" DESTINATION ${DEPLOY_QML_DIR})
        message(STATUS "Deployed QML: ${_mod}")
    endif()
endforeach()

# ============================================
# 4. Fonts
# ============================================
# These are the FALLBACK fonts -- the system font database the target sees.
# The cluster's own type is not among them any more: Main.qml embeds it through
# fonts.qrc and a FontLoader, precisely so the panel does not depend on what
# lands here. What is deployed here still matters for anything Qt resolves by
# family name rather than by file, so it should not be empty.
#
# And it WAS empty, silently, on every build. FONT_SOURCE_DIR defaults to
# ${CMAKE_SOURCE_DIR}/../qt6-qnx-libs/fonts, a directory that does not exist in
# this checkout, so all four entries in fonts.txt missed and each one produced a
# warning that scrolled past in a build that otherwise succeeded. The tree
# shipped with a fonts.conf and nothing for it to point at, which on a Qt built
# with fontconfig (this one is -- libQt6Gui links libfontconfig.so.1, contrary
# to the old note in run.sh) means a font database with zero families in it.
#
# So: fall back to the host's own font directories when FONT_SOURCE_DIR has
# nothing, and say plainly at the end if the directory is still empty.
if(DEFINED PROJ_SOURCE_DIR)
    file(MAKE_DIRECTORY ${DEPLOY_FONTS_DIR})
    set(_font_search_dirs "")
    if(DEFINED FONT_SOURCE_DIR AND EXISTS "${FONT_SOURCE_DIR}")
        list(APPEND _font_search_dirs "${FONT_SOURCE_DIR}")
    endif()
    # Host fallbacks. The files named in fonts.txt are the standard DejaVu set,
    # which any build host that can run Qt Creator already has.
    foreach(_sysdir "/usr/share/fonts" "/usr/local/share/fonts")
        if(EXISTS "${_sysdir}")
            list(APPEND _font_search_dirs "${_sysdir}")
        endif()
    endforeach()

    set(_fonts_txt "${PROJ_SOURCE_DIR}/fonts.txt")
    set(_fonts_deployed 0)
    if(EXISTS "${_fonts_txt}")
        file(STRINGS "${_fonts_txt}" _font_list)
        foreach(_font ${_font_list})
            set(_matches "")
            foreach(_fd ${_font_search_dirs})
                file(GLOB_RECURSE _matches "${_fd}/${_font}")
                if(_matches)
                    break()
                endif()
            endforeach()
            if(_matches)
                list(GET _matches 0 _match)
                file(COPY ${_match} DESTINATION ${DEPLOY_FONTS_DIR})
                math(EXPR _fonts_deployed "${_fonts_deployed} + 1")
                message(STATUS "Deployed font: ${_font}")
            else()
                message(WARNING "Font not found anywhere: ${_font}\n"
                        "  searched: ${_font_search_dirs}")
            endif()
        endforeach()
    endif()
    if(_fonts_deployed EQUAL 0)
        message(WARNING
            "No fallback fonts deployed to ${DEPLOY_FONTS_DIR}.\n"
            "  run.sh points FONTCONFIG_FILE and QT_QPA_FONTDIR at that directory, so the\n"
            "  target's system font database will be empty. The cluster's own text still\n"
            "  renders -- Main.qml embeds its typeface via fonts.qrc -- but anything that\n"
            "  asks for a font by family name will get nothing. Set FONT_SOURCE_DIR to a\n"
            "  directory holding the files listed in fonts.txt.")
    endif()
    file(WRITE "${DEPLOY_FONTS_DIR}/fonts.conf"
"<?xml version=\"1.0\"?>
<fontconfig>
  <dir prefix=\"relative\">.</dir>
  <cachedir>/tmp/fontconfig-cache</cachedir>
</fontconfig>
")
endif()

# ============================================
# 5. Recursive dependency resolution
# ============================================
if(DEFINED QNX_LIB_DIR)
    set(_search_dirs
        "${QT_LIB_DIR}"
        "${QNX_LIB_DIR}/lib"
        "${QNX_LIB_DIR}/usr/lib"
        "${DEPLOY_LIB_DIR}"
    )

    set(_worklist "")
    if(DEFINED BIN AND EXISTS "${BIN}")
        list(APPEND _worklist "${BIN}")
    endif()
    file(GLOB_RECURSE _seed "${DEPLOY_LIB_DIR}/*.so*")
    list(APPEND _worklist ${_seed})
    file(GLOB_RECURSE _qmlseed "${DEPLOY_QML_DIR}/*.so*")
    list(APPEND _worklist ${_qmlseed})
    file(GLOB_RECURSE _plugseed "${DEPLOY_PLUGIN_DIR}/*.so*")
    list(APPEND _worklist ${_plugseed})

    set(_processed "")
    while(_worklist)
        list(GET _worklist 0 _cur)
        list(REMOVE_AT _worklist 0)
        list(FIND _processed "${_cur}" _already)
        if(_already GREATER -1)
            continue()
        endif()
        list(APPEND _processed "${_cur}")

        execute_process(COMMAND objdump -p "${_cur}"
            OUTPUT_VARIABLE _od ERROR_QUIET)
        if(_od STREQUAL "")
            execute_process(COMMAND readelf -d "${_cur}"
                OUTPUT_VARIABLE _od ERROR_QUIET)
        endif()
        if(_od STREQUAL "")
            continue()
        endif()

        set(_needed "")
        if(_od MATCHES "NEEDED[ \t]+[A-Za-z0-9_.-]+")
            string(REGEX MATCHALL "NEEDED[ \t]+([A-Za-z0-9_.-]+)" _needed "${_od}")
            string(REGEX REPLACE "NEEDED[ \t]+" "" _needed "${_needed}")
        else()
            string(REGEX MATCHALL "\\(NEEDED\\)[ \t]+Shared library:[ \t]+\\[([^]]+)\\]" _needed "${_od}")
            string(REGEX REPLACE "\\(NEEDED\\)[ \t]+Shared library:[ \t]+\\[" "" _needed "${_needed}")
            string(REGEX REPLACE "\\]" "" _needed "${_needed}")
        endif()

        foreach(_soname ${_needed})
            if(EXISTS "${DEPLOY_LIB_DIR}/${_soname}")
                continue()
            endif()
            set(_found "")
            foreach(_sd ${_search_dirs})
                file(GLOB _m "${_sd}/${_soname}*")
                set(_copied "")
                foreach(_f ${_m})
                    if(_f MATCHES "\\.so[0-9.]*$")
                        execute_process(COMMAND cp -a "${_f}" "${DEPLOY_LIB_DIR}/")
                        set(_copied 1)
                    endif()
                endforeach()
                if(_copied)
                    list(APPEND _worklist "${DEPLOY_LIB_DIR}/${_soname}")
                    set(_found 1)
                    break()
                endif()
            endforeach()
            if(NOT _found)
                message(WARNING "Recursive deploy: '${_soname}' (needed by ${_cur}) not found in search paths")
            endif()
        endforeach()
    endwhile()

    execute_process(COMMAND find ${DEPLOY_LIB_DIR} -type l ! -exec test -e {} \; -delete)
endif()

message(STATUS "Deploy complete — only required dependencies")
