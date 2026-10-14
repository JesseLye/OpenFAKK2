if(NOT USE_OPENAL)
    return()
endif()

if(NOT BUILD_CLIENT)
    return()
endif()

set(INTERNAL_OPENAL_DIR ${SOURCE_DIR}/thirdparty/openal-soft-${OPENAL_VERSION})

find_package(OpenAL QUIET)

if(NOT OpenAL_FOUND)
    set(OPENAL_DEFINITIONS USE_INTERNAL_OPENAL_HEADERS)
    set(OPENAL_INCLUDE_DIR ${INTERNAL_OPENAL_DIR}/include)
    set(OPENAL_LIBRARY openal)
endif()

list(APPEND CLIENT_DEFINITIONS ${OPENAL_DEFINITIONS} USE_OPENAL)
list(APPEND CLIENT_INCLUDE_DIRS ${OPENAL_INCLUDE_DIR})

if(USE_OPENAL_DLOPEN)
    list(APPEND CLIENT_DEFINITIONS USE_OPENAL_DLOPEN)

    # OpenFAKK2: ship OpenAL Soft's DLL (its soft_oal.dll, renamed as its readme
    # says) next to the executable, so the OpenAL backend runs everywhere rather
    # than only where another game installed an OpenAL32.dll. LGPL 2.1, see
    # ${INTERNAL_OPENAL_DIR}/COPYING.
    if(WIN32)
        include(utils/arch)
        if(ARCH STREQUAL "x86_64")
            list(APPEND CLIENT_DEPLOY_LIBRARIES ${SOURCE_DIR}/thirdparty/libs/win64/OpenAL32.dll)
        elseif(ARCH STREQUAL "x86")
            list(APPEND CLIENT_DEPLOY_LIBRARIES ${SOURCE_DIR}/thirdparty/libs/win32/OpenAL32.dll)
        endif()
    endif()
else()
    find_package(Threads REQUIRED)
    list(APPEND CLIENT_LIBRARIES Threads::Threads ${OPENAL_LIBRARY})
endif()
