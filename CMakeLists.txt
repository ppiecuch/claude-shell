cmake_minimum_required(VERSION 3.20)
project(claude-shell VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Version from build.info ---
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/build.info"
)
file(STRINGS "${CMAKE_SOURCE_DIR}/build.info" _build_info_lines)
foreach(_line IN LISTS _build_info_lines)
    if(_line MATCHES "^version=(.+)$")
        set(APP_VERSION "${CMAKE_MATCH_1}")
    elseif(_line MATCHES "^build=(.+)$")
        set(APP_BUILD_NUMBER "${CMAKE_MATCH_1}")
    endif()
endforeach()
set(APP_BUNDLE_ID "com.komsoft.claude-shell")
message(STATUS "Claude Shell ${APP_VERSION} (build ${APP_BUILD_NUMBER})")

# --- FLTK (static, pre-built) ---
set(FLTK_DIR "/Volumes/FastUSB/Sources/fltk-dist/darwin"
    CACHE PATH "FLTK installation prefix")

add_library(fltk STATIC IMPORTED)
set_target_properties(fltk PROPERTIES
    IMPORTED_LOCATION "${FLTK_DIR}/lib/libfltk.a"
    INTERFACE_INCLUDE_DIRECTORIES "${FLTK_DIR}/include"
    INTERFACE_COMPILE_DEFINITIONS
        "_LARGEFILE_SOURCE;_LARGEFILE64_SOURCE;_FILE_OFFSET_BITS=64;_THREAD_SAFE;_REENTRANT"
)

add_library(fltk_images STATIC IMPORTED)
set_target_properties(fltk_images PROPERTIES
    IMPORTED_LOCATION "${FLTK_DIR}/lib/libfltk_images.a"
)

add_library(fltk_png STATIC IMPORTED)
set_target_properties(fltk_png PROPERTIES
    IMPORTED_LOCATION "${FLTK_DIR}/lib/libfltk_png.a"
)

add_library(fltk_jpeg STATIC IMPORTED)
set_target_properties(fltk_jpeg PROPERTIES
    IMPORTED_LOCATION "${FLTK_DIR}/lib/libfltk_jpeg.a"
)

add_library(fltk_z STATIC IMPORTED)
set_target_properties(fltk_z PROPERTIES
    IMPORTED_LOCATION "${FLTK_DIR}/lib/libfltk_z.a"
)

# --- nlohmann/json (header-only) ---
include(FetchContent)
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# --- Sources ---
file(GLOB_RECURSE SOURCES src/*.cpp)

# --- macOS App Bundle ---
if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 17)

    # Objective-C++ sources
    file(GLOB_RECURSE OBJCXX_SOURCES src/*.mm)

    set(ICON_FILE "${CMAKE_SOURCE_DIR}/icon.icns")

    add_executable(claude-shell MACOSX_BUNDLE ${SOURCES} ${OBJCXX_SOURCES})

    # Icon as bundle resource
    if(EXISTS "${ICON_FILE}")
        target_sources(claude-shell PRIVATE "${ICON_FILE}")
        set_source_files_properties("${ICON_FILE}" PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources"
        )
    endif()

    set_target_properties(claude-shell PROPERTIES
        OUTPUT_NAME "ClaudeShell"
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Claude Shell"
        MACOSX_BUNDLE_BUNDLE_VERSION "${APP_BUILD_NUMBER}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${APP_VERSION}"
        MACOSX_BUNDLE_GUI_IDENTIFIER "${APP_BUNDLE_ID}"
        MACOSX_BUNDLE_ICON_FILE "icon.icns"
        MACOSX_BUNDLE_COPYRIGHT "Copyright © 2026 KomSoft Oprogramowanie"
        MACOSX_BUNDLE_INFO_STRING "Claude Shell — Claude session sharing manager v${APP_VERSION}"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/Resources/Info.plist.in"
        XCODE_ATTRIBUTE_INSTALL_PATH "/Applications"
        XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT "dwarf-with-dsym"
        XCODE_ATTRIBUTE_GCC_GENERATE_DEBUGGING_SYMBOLS "YES"
    )

    # Post-build: copy Credits.rtf
    add_custom_command(TARGET claude-shell POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/Resources/Credits.rtf"
            "$<TARGET_BUNDLE_DIR:claude-shell>/Contents/Resources/Credits.rtf"
        COMMENT "Copying Credits.rtf to bundle"
    )

    # Help Book — generated whenever any input changes (template, css,
    # images, build.info, configure script). A stamp file drives the
    # dependency tracking so plain `cmake --build` regenerates correctly.
    # CFBundleHelpBookFolder = "HelpDocs" — macOS resolves via English.lproj/
    set(HELP_SRC "${CMAKE_SOURCE_DIR}/Resources/English.lproj/HelpDocs")
    set(HELP_DST "$<TARGET_BUNDLE_DIR:claude-shell>/Contents/Resources/English.lproj/HelpDocs")
    set(HELP_STAMP "${CMAKE_BINARY_DIR}/CMakeFiles/help.stamp")
    set(LSREGISTER "/System/Library/Frameworks/CoreServices.framework/Versions/A/Frameworks/LaunchServices.framework/Versions/A/Support/lsregister")

    add_custom_command(
        OUTPUT "${HELP_STAMP}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${HELP_DST}"
        COMMAND ${CMAKE_COMMAND}
            -DAPP_VERSION=${APP_VERSION}
            -DAPP_BUILD_NUMBER=${APP_BUILD_NUMBER}
            -DTEMPLATE_IN=${HELP_SRC}/index.html.in
            -DTEMPLATE_OUT=${HELP_DST}/index.html
            -P ${CMAKE_SOURCE_DIR}/cmake/configure-help.cmake
        COMMAND ${CMAKE_COMMAND} -E copy
            "${HELP_SRC}/style.css"
            "${HELP_SRC}/logo.png"
            "${HELP_SRC}/komsoft.png"
            "${HELP_DST}/"
        COMMAND hiutil -Caf
            "${HELP_DST}/HelpDocs.helpindex"
            "${HELP_DST}"
        # Refresh macOS Help Viewer so it picks up the new content.
        # helpd caches help books in memory keyed by CFBundleHelpBookName,
        # so re-registering with lsregister alone is not enough — helpd
        # must be killed (it auto-respawns) and any open Help Viewer
        # window closed. Failures are non-fatal.
        COMMAND ${CMAKE_COMMAND} -E touch "$<TARGET_BUNDLE_DIR:claude-shell>"
        COMMAND sh -c "'${LSREGISTER}' -f '$<TARGET_BUNDLE_DIR:claude-shell>' >/dev/null 2>&1 || true"
        COMMAND sh -c "killall -9 helpd 'Help Viewer' >/dev/null 2>&1 || true"
        COMMAND sh -c "rm -rf \"$ENV{HOME}/Library/Caches/com.apple.help\"* \"$ENV{HOME}/Library/Caches/com.apple.helpd\" \"$ENV{HOME}/Library/HTTPStorages/com.apple.helpd\" \"$ENV{HOME}/Library/Group Containers/group.com.apple.helpviewer.content/Library/Caches/${APP_BUNDLE_ID}.\"*.help 2>/dev/null || true"
        COMMAND ${CMAKE_COMMAND} -E touch "${HELP_STAMP}"
        DEPENDS
            "${HELP_SRC}/index.html.in"
            "${HELP_SRC}/style.css"
            "${HELP_SRC}/logo.png"
            "${HELP_SRC}/komsoft.png"
            "${CMAKE_SOURCE_DIR}/build.info"
            "${CMAKE_SOURCE_DIR}/cmake/configure-help.cmake"
        COMMENT "Installing Help Book and refreshing macOS Help cache"
        VERBATIM
    )

    add_custom_target(claude-shell-help ALL DEPENDS "${HELP_STAMP}")
    add_dependencies(claude-shell-help claude-shell)

    # Manual full-refresh target (also unregisters stale bundle paths
    # left by Xcode archives etc.):  cmake --build build --target help-refresh
    add_custom_target(help-refresh
        COMMAND "${CMAKE_SOURCE_DIR}/_scripts/refresh-help.sh"
            "$<TARGET_BUNDLE_DIR:claude-shell>"
        DEPENDS claude-shell-help
        COMMENT "Forcing full Help Viewer cache refresh"
        VERBATIM
    )

    # Compile definitions for version info and incbin paths
    target_compile_definitions(claude-shell PRIVATE
        BUILD_VERSION="${APP_VERSION}"
        BUILD_NUMBER=${APP_BUILD_NUMBER}
        SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )

    # App Store build disables doctest
    option(BUILD_APPSTORE "Build for App Store (disables tests)" OFF)
    if(BUILD_APPSTORE)
        target_compile_definitions(claude-shell PRIVATE DOCTEST_CONFIG_DISABLE)
    endif()

else()
    add_executable(claude-shell ${SOURCES})
endif()

target_include_directories(claude-shell PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(claude-shell PRIVATE
    fltk
    fltk_images
    fltk_png
    fltk_jpeg
    fltk_z
    nlohmann_json::nlohmann_json
    "-framework Cocoa"
    "-framework ServiceManagement"
    "-weak_framework UniformTypeIdentifiers"
    -lpthread
    -lm
)
