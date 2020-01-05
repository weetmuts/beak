SET(CMAKE_SYSTEM_NAME Windows)
SET(CMAKE-SYSTEM_VERSION 1)

if(DEFINED ENV{MINGW_ARCH})
        set(MINGW_ARCH              "$ENV{MINGW_ARCH}")
else()
        set(MINGW_ARCH              "64")
endif()

if(${MINGW_ARCH} STREQUAL "32")
        set(CMAKE_SYSTEM_PROCESSOR  "i686")
elseif(${MINGW_ARCH} STREQUAL "64")
        set(CMAKE_SYSTEM_PROCESSOR  "x86_64")
else()
        message(FATAL_ERROR         "Unknown system architecture specified")
endif()

if(${MINGW_ARCH} STREQUAL "32")
        set(MINGW_TOOL_PREFIX               ${MINGW_PREFIX}/bin/i686-w64-mingw32-)
elseif(${MINGW_ARCH} STREQUAL "64")
        set(MINGW_TOOL_PREFIX               ${MINGW_PREFIX}/bin/x86_64-w64-mingw32-)
else()
        message(FATAL_ERROR         "Unknown system architecture specified")
endif()

set(CMAKE_FIND_ROOT_PATH            ${MINGW_PREFIX})
set(CMAKE_INSTALL_PREFIX            ${MINGW_PREFIX})

set(CMAKE_C_COMPILER                ${MINGW_TOOL_PREFIX}gcc)
set(CMAKE_CXX_COMPILER              ${MINGW_TOOL_PREFIX}g++)
set(CMAKE_RC_COMPILER               ${MINGW_TOOL_PREFIX}windres)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

set(WASAPI_INCLUDE_DIR              ${MINGW_PREFIX}/include)

if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE        "Release" CACHE STRING "Defaulting to Release build type for mingw")
endif()
