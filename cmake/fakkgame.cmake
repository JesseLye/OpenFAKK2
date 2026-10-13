# Heavy Metal: F.A.K.K. 2 game modules, built from Ritual's 1.02 SDK sources.
#
# These are native libraries loaded through GetGameAPI / GetCGameAPI; FAKK2 has
# no QVM path. On 32-bit Windows they are ABI compatible with the retail
# engine and are named to match (gamex86.dll, cgamex86.dll).

if(NOT BUILD_FAKK_GAME)
    return()
endif()

include(utils/arch)
include(utils/set_output_dirs)

enable_language(CXX)

set(FAKK_DIR ${SOURCE_DIR}/fakk)

if(ARCH STREQUAL "x86")
    set(FAKK_MODULE_ARCH x86)
else()
    set(FAKK_MODULE_ARCH ${ARCH})
endif()

set(FAKK_SHARED_SOURCES
    ${FAKK_DIR}/fgame/q_math.c
    ${FAKK_DIR}/fgame/q_mathsys.c
    ${FAKK_DIR}/fgame/q_shared.c
    ${FAKK_DIR}/fgame/bg_misc.c
    ${FAKK_DIR}/fgame/bg_pmove.c
    ${FAKK_DIR}/win32/win_bounds.cpp
)

file(GLOB FAKK_FGAME_SOURCES CONFIGURE_DEPENDS
    ${FAKK_DIR}/fgame/*.cpp)

# On disk in the SDK but absent from Ritual's fgame.dsp; they target older
# engine interfaces and were not part of the shipped 1.02 build.
list(REMOVE_ITEM FAKK_FGAME_SOURCES
    ${FAKK_DIR}/fgame/b_files.cpp
    ${FAKK_DIR}/fgame/b_nav.cpp
    ${FAKK_DIR}/fgame/b_navgen.cpp
    ${FAKK_DIR}/fgame/bullet.cpp
    ${FAKK_DIR}/fgame/container.cpp
    ${FAKK_DIR}/fgame/deadbody.cpp
    ${FAKK_DIR}/fgame/fists.cpp
    ${FAKK_DIR}/fgame/testweapon.cpp
)

set(FAKK_CGAME_SOURCES
    ${FAKK_DIR}/cgame/cg_beam.cpp
    ${FAKK_DIR}/cgame/cg_commands.cpp
    ${FAKK_DIR}/cgame/cg_consolecmds.c
    ${FAKK_DIR}/cgame/cg_drawtools.cpp
    ${FAKK_DIR}/cgame/cg_ents.c
    ${FAKK_DIR}/cgame/cg_lightstyles.cpp
    ${FAKK_DIR}/cgame/cg_main.c
    ${FAKK_DIR}/cgame/cg_marks.c
    ${FAKK_DIR}/cgame/cg_modelanim.c
    ${FAKK_DIR}/cgame/cg_player.cpp
    ${FAKK_DIR}/cgame/cg_playerstate.c
    ${FAKK_DIR}/cgame/cg_predict.c
    ${FAKK_DIR}/cgame/cg_servercmds.c
    ${FAKK_DIR}/cgame/cg_snapshot.c
    ${FAKK_DIR}/cgame/cg_sound.cpp
    ${FAKK_DIR}/cgame/cg_specialfx.cpp
    ${FAKK_DIR}/cgame/cg_swipe.cpp
    ${FAKK_DIR}/cgame/cg_testemitter.cpp
    ${FAKK_DIR}/cgame/cg_view.c
    ${FAKK_DIR}/cgame/class.cpp
    ${FAKK_DIR}/cgame/listener.cpp
    ${FAKK_DIR}/cgame/script.cpp
    ${FAKK_DIR}/cgame/str.cpp
)

function(fakk_module TARGET OUTPUT_NAME DEF_FILE)
    set_target_properties(${TARGET} PROPERTIES
        OUTPUT_NAME ${OUTPUT_NAME}${FAKK_MODULE_ARCH}
        PREFIX ""
        # The SDK predates C99/C++98 conformance; the engine's stricter warning
        # baseline does not apply to it.
        C_STANDARD 90
        CXX_STANDARD 98
        INTERPROCEDURAL_OPTIMIZATION FALSE)

    if(MSVC)
        target_sources(${TARGET} PRIVATE ${DEF_FILE})
        target_compile_options(${TARGET} PRIVATE /W3 /Zc:forScope- /permissive /EHsc)
        target_compile_definitions(${TARGET} PRIVATE WIN32 _WINDOWS)
        target_link_libraries(${TARGET} PRIVATE winmm)
    endif()

    set_output_dirs(${TARGET} SUBDIRECTORY fakk)
endfunction()

add_library(fakk_game SHARED ${FAKK_FGAME_SOURCES} ${FAKK_SHARED_SOURCES})
target_compile_definitions(fakk_game PRIVATE GAME_DLL)
fakk_module(fakk_game game ${FAKK_DIR}/fgame/game.def)

add_library(fakk_cgame SHARED ${FAKK_CGAME_SOURCES} ${FAKK_SHARED_SOURCES})
target_compile_definitions(fakk_cgame PRIVATE CGAME_DLL)
fakk_module(fakk_cgame cgame ${FAKK_DIR}/cgame/cgame.def)
