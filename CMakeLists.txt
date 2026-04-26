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
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Claude Shell"
        MACOSX_BUNDLE_BUNDLE_VERSION "${APP_BUILD_NUMBER}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${APP_VERSION}"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.komsoft.claude-shell"
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

    # Post-build: install Help Book and generate help index
    # CFBundleHelpBookFolder = "HelpDocs" — macOS resolves via English.lproj/
    set(HELP_SRC "${CMAKE_SOURCE_DIR}/Resources/English.lproj/HelpDocs")
    set(HELP_DST "$<TARGET_BUNDLE_DIR:claude-shell>/Contents/Resources/English.lproj/HelpDocs")
    add_custom_command(TARGET claude-shell POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${HELP_DST}"
        COMMAND ${CMAKE_COMMAND} -E copy
            "${HELP_SRC}/index.html" "${HELP_SRC}/style.css"
            "${HELP_DST}/"
        COMMAND hiutil -Caf
            "${HELP_DST}/HelpDocs.helpindex"
            "${HELP_DST}"
        COMMENT "Installing Help Book and generating help index"
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
