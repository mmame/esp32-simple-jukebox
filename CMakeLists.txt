cmake_minimum_required(VERSION 3.12)

# Build stamp: build_release.ps1 / build_variant.ps1 write the current date and time to build_stamp.txt before each
# build. It becomes the application version (shown on the upload web page and in the boot log); changing the file
# triggers a reconfigure.
set(BUILD_STAMP_FILE "${CMAKE_CURRENT_LIST_DIR}/build_stamp.txt")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${BUILD_STAMP_FILE}")
set(BUILD_STAMP "unknown")
if(EXISTS "${BUILD_STAMP_FILE}")
    file(STRINGS "${BUILD_STAMP_FILE}" BUILD_STAMP LIMIT_COUNT 1)
endif()
set(PROJECT_VER "${BUILD_STAMP}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32-cyd-musicplayer)
