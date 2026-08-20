cmake_minimum_required(VERSION 3.16)
project(Cluster VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# ============================================
# Qt6 Library Path Configuration
# ============================================
# Qt Creator configures with CMAKE_PREFIX_PATH pointing at the kit's Qt and
# never sets Qt6_DIR, so derive it from there when it wasn't passed explicitly.
# Without this the build aborts here before build.ninja is generated. The
# explicit -DQt6_DIR= form is still needed for the QNX cross-build, where
# nothing else points at the right Qt.
if(NOT DEFINED Qt6_DIR)
    foreach(_prefix IN LISTS CMAKE_PREFIX_PATH)
        if(EXISTS "${_prefix}/lib/cmake/Qt6")
            set(Qt6_DIR "${_prefix}/lib/cmake/Qt6" CACHE PATH "Qt6 CMake package directory")
            break()
        endif()
    endforeach()
endif()

if(NOT DEFINED Qt6_DIR)
    message(FATAL_ERROR "Qt6_DIR not set and not derivable from CMAKE_PREFIX_PATH.\n"
            "Provide it via cmake:\n"
            "  cmake -S . -B <build_dir> -DQt6_DIR=<path_to_libs>/lib/cmake/Qt6")
endif()
message(STATUS "Using Qt6_DIR: ${Qt6_DIR}")

find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick)
qt_standard_project_setup(REQUIRES 6.5)

# ─── QNX: strip -lrt and -lpthread from Qt's interface libs ───────────────
if(QNX)
    foreach(_qt_target IN ITEMS
            Qt6::Core Qt6::Gui Qt6::Qml Qt6::QmlModels
            Qt6::QmlWorkerScript Qt6::Network Qt6::Quick)
        if(TARGET ${_qt_target})
            get_target_property(_libs ${_qt_target} INTERFACE_LINK_LIBRARIES)
            if(_libs)
                list(REMOVE_ITEM _libs rt pthread)
                set_target_properties(${_qt_target} PROPERTIES
                    INTERFACE_LINK_LIBRARIES "${_libs}")
            endif()
        endif()
    endforeach()
endif()

qt_add_executable(appCluster
    main.cpp
)
qt_add_qml_module(appCluster
    URI Cluster
    QML_FILES
        Main.qml
    SOURCES
        cluster.h
        cluster.cpp
        SpiReader.h
        SpiReader.cpp
        AiReader.h
        AiReader.cpp
        motor_wire.h
        MotorBlockAnalyzer.h
        NetworkClock.h
        NetworkClock.cpp
    RESOURCES
        images.qrc
        fonts.qrc
)

# ─── Force-disable QML AOT cache generation ────────────────────────────────
set_target_properties(appCluster PROPERTIES
    QT_QML_MODULE_NO_CACHEGEN ON
    QT_QMLCACHEGEN_DIRECT_CALLS OFF
)
set_source_files_properties(Main.qml PROPERTIES
    QT_QML_SKIP_CACHEGEN ON
)

set_target_properties(appCluster PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(appCluster
    PRIVATE Qt6::Quick
)

target_include_directories(appCluster PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# ============================================
# Deploy paths (derive from Qt6_DIR)
# ============================================
get_filename_component(QT6_PREFIX "${Qt6_DIR}/../../.." ABSOLUTE)
set(QT6_LIB_DIR "${QT6_PREFIX}/lib" CACHE PATH "Qt6 shared libraries")
set(QT6_QML_DIR "${QT6_PREFIX}/qml" CACHE PATH "Qt6 QML imports")
set(QT6_PLUGIN_DIR "${QT6_PREFIX}/plugins" CACHE PATH "Qt6 plugins directory")

set(QT_DEPLOY_DIR "${CMAKE_BINARY_DIR}/deploy" CACHE PATH "Deploy output dir")
set(DEPLOY_LIB_DIR "${QT_DEPLOY_DIR}/lib")
set(DEPLOY_QML_DIR "${QT_DEPLOY_DIR}/qml")
set(DEPLOY_PLUGIN_DIR "${QT_DEPLOY_DIR}/plugins")
set(DEPLOY_FONTS_DIR "${QT_DEPLOY_DIR}/lib/fonts")

set(QNX_TARGET_ARCH "gcc_ntoaarch64le" CACHE STRING "QNX qcc target variant")
set(QNX_TARGET "$ENV{QNX_TARGET}" CACHE PATH "QNX target sysroot")
set(QNX_ARCH "aarch64le" CACHE STRING "QNX target architecture")
set(QNX_LIB_DIR "${QNX_TARGET}/${QNX_ARCH}" CACHE PATH "QNX arch root directory")
set(FONT_SOURCE_DIR "${CMAKE_SOURCE_DIR}/../qt6-qnx-libs/fonts" CACHE PATH "Directory containing font files")

# ============================================
# Runtime output configuration
# ============================================
set_target_properties(appCluster PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${QT_DEPLOY_DIR}"
    INSTALL_RPATH "$ORIGIN/../lib"
    BUILD_RPATH "${QT6_LIB_DIR}"
)

# ============================================
# Deploy: copy only needed Qt + system deps
# ============================================
add_custom_command(TARGET appCluster POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        -DBIN=$<TARGET_FILE:appCluster>
        -DPROJ_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DQT_LIB_DIR=${QT6_LIB_DIR}
        -DDEPLOY_LIB_DIR=${DEPLOY_LIB_DIR}
        -DQT_QML_DIR=${QT6_QML_DIR}
        -DDEPLOY_QML_DIR=${DEPLOY_QML_DIR}
        -DQT_PLUGIN_DIR=${QT6_PLUGIN_DIR}
        -DDEPLOY_PLUGIN_DIR=${DEPLOY_PLUGIN_DIR}
        -DDEPLOY_FONTS_DIR=${DEPLOY_FONTS_DIR}
        -DFONT_SOURCE_DIR=${FONT_SOURCE_DIR}
        -DQNX_LIB_DIR=${QNX_LIB_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/deploy_qt.cmake
    COMMENT "Deploying minimal Qt dependencies to ${QT_DEPLOY_DIR}"
)

# ============================================
# Scene graph backend: decided here, not asserted
# ============================================
# This one line is the difference between the cluster and a stripped version of
# it, so it is worth being explicit about why it is computed rather than fixed.
#
# The Qt Quick software adaptation supports neither item layers nor shader
# effects. Under it EVERY MultiEffect in Main.qml draws nothing -- and each one
# is fed by a `visible: false` source that exists only to be its input, so what
# disappears is not an effect over a visible item, it IS the item. Lost that
# way: the neon ring baseline, the speed-driven ring glow, both status bands,
# their icons, and the motor fault lamp. What survives is the flat bezel PNG,
# the road, the car and the text. That is the whole "it doesn't look like the
# desktop on the target" symptom, and it is reproducible on a desktop with
# QT_QUICK_BACKEND=software ./appCluster.
#
# So software rendering is never what we WANT. But it was also not a mistake to
# force it: a Qt cross-built without OpenGL cannot do anything else, and on one
# such build (see the commit that added the forcing) Qt Quick default-selected
# the Vulkan RHI backend, which QQnxIntegration::createPlatformWindow() has no
# case for -- qFatal("unsupported window API") on every launch, before the
# window ever showed. Forcing software was the only way that binary started.
#
# Both facts are true; which one applies depends entirely on the Qt in
# Qt6_DIR. Hardcoding either answer breaks the other machine, and this project
# gets built against more than one Qt-for-QNX tree. QT_FEATURE_opengl is set by
# Qt's own CMake package for the Qt actually being used, so let it decide.
if(QT_FEATURE_opengl)
    set(CLUSTER_BACKEND_BLOCK
"# Left unset on purpose: this Qt HAS OpenGL (QT_FEATURE_opengl was ON at
# configure time), so Qt picks the RHI OpenGL ES backend and every MultiEffect
# in Main.qml renders. Forcing \"software\" here is what strips the ring, the
# status bands and the fault lamp off the panel -- see CMakeLists.txt.
#
# QT_QUICK_BACKEND=software ./run.sh still forces the raster path, which is a
# way to prove the binary runs if the target's GLES driver is not up. Expect
# the cluster to look wrong there. QSG_INFO=1 prints which backend was chosen.")
    message(STATUS "Scene graph: RHI/OpenGL (QT_FEATURE_opengl is ON) -- effects will render")
else()
    set(CLUSTER_BACKEND_BLOCK
"# Forced, because this Qt was cross-built WITHOUT OpenGL (QT_FEATURE_opengl was
# OFF at configure time). Qt Quick would default-select the Vulkan RHI backend,
# which the QNX platform plugin has no window case for, and qFatal before the
# window ever shows. Software rendering selects RasterSurface, which it handles.
#
# This is a degraded panel, not a working one: the software adaptation drops
# every MultiEffect in Main.qml, taking the neon ring, both status bands, their
# icons and the motor fault lamp with it. Rebuilding Qt for QNX with OpenGL ES
# is what fixes that -- this line then disappears on its own.
export QT_QUICK_BACKEND=\"\${QT_QUICK_BACKEND:-software}\"")
    message(WARNING
        "Qt6 at ${Qt6_DIR} has no OpenGL (QT_FEATURE_opengl is OFF).\n"
        "  run.sh will force QT_QUICK_BACKEND=software, which is the only backend this\n"
        "  Qt can start on -- but the software adaptation supports neither item layers\n"
        "  nor shader effects, so every MultiEffect in Main.qml will draw NOTHING.\n"
        "  The neon ring, both status bands, their icons and the motor fault lamp will\n"
        "  be missing from the panel. Rebuild Qt for QNX with OpenGL ES to get them.")
endif()

# Generate a launcher
file(GENERATE
    OUTPUT ${QT_DEPLOY_DIR}/run.sh
    CONTENT
"#!/bin/sh
DIR=\"$(cd \"$(dirname \"$0\")\" && pwd)\"
export LD_LIBRARY_PATH=\"$DIR/lib:$LD_LIBRARY_PATH\"
export QML2_IMPORT_PATH=\"$DIR/qml\"
# QT_PLUGIN_PATH is the root Qt appends a category to (plugins/imageformats,
# plugins/platforms); QT_QPA_PLATFORM_PLUGIN_PATH is the platforms directory
# itself, not its parent. Setting only the latter, to the parent, found nothing:
#   qt.qpa.plugin: Could not find the Qt platform plugin \"xcb\" in \"/qt-cluster/plugins\"
export QT_PLUGIN_PATH=\"$DIR/plugins\"
export QT_QPA_PLATFORM_PLUGIN_PATH=\"$DIR/plugins/platforms\"
# Without this Qt falls back to its compiled-in default, which is xcb -- an X11
# plugin that is neither deployed here nor meaningful on QNX. The QNX platform
# plugin is libqqnx.so and talks to Screen. Overridable for a host-side run.
export QT_QPA_PLATFORM=\"\${QT_QPA_PLATFORM:-qnx}\"
# Scene graph backend. This block is generated: which of the two forms lands
# here is decided at configure time from QT_FEATURE_opengl, because the right
# answer differs per Qt-for-QNX build. See the reasoning in CMakeLists.txt.
${CLUSTER_BACKEND_BLOCK}
# 7\" panel: ~154x87mm active area at 1024x600.
export QQNX_PHYSICAL_SCREEN_SIZE=\"\${QQNX_PHYSICAL_SCREEN_SIZE:-154,87}\"
# Target panel size. NOTE: nothing reads these yet -- Main.qml sets its own
# Window size. They are kept in step so they do not mislead.
export APP_WIDTH=\"\${APP_WIDTH:-1024}\"
export APP_HEIGHT=\"\${APP_HEIGHT:-600}\"
# Which of these two Qt reads depends on how it was built, so both are set.
# The Qt in ~/qt6-qnx IS built with fontconfig -- readelf -d on libQt6Gui.so.6
# lists libfontconfig.so.1 -- so FONTCONFIG_FILE is the one that takes effect
# and QT_QPA_FONTDIR is the fallback for a basic-font-database build. (An older
# note here claimed the opposite; it was written against a different qtbase.)
#
# Either way this points the target's font database at deploy/lib/fonts, and
# NOTHING ELSE -- so whatever deploy_qt.cmake puts there is the entire set of
# families the target can resolve by name. It was shipping that directory empty
# for a long time. The cluster's own type no longer depends on it: Main.qml
# embeds the typeface through fonts.qrc and loads it with FontLoader, which is
# what makes the panel and the desktop draw the same glyphs.
export QT_QPA_FONTDIR=\"$DIR/lib/fonts\"
export FONTCONFIG_FILE=\"$DIR/lib/fonts/fonts.conf\"
exec \"$DIR/appCluster\" \"\$@\"
"
    FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
)

message(STATUS "Build complete!")
message(STATUS "Binary: ${QT_DEPLOY_DIR}/appCluster")
message(STATUS "Run with: ${QT_DEPLOY_DIR}/run.sh")

# ─── QNX-specific ─────────────────────────────────────────────────────────
if(QNX)
    target_compile_definitions(appCluster PRIVATE __QNXNTO__)
    target_compile_options(appCluster PRIVATE
        -V${QNX_TARGET_ARCH}
        -Wc,-std=c++17
    )
    target_link_libraries(appCluster PRIVATE
        -lsocket
    )
    install(TARGETS appCluster
        RUNTIME DESTINATION /apps/cluster
    )
else()
    install(TARGETS appCluster
        BUNDLE  DESTINATION .
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()
