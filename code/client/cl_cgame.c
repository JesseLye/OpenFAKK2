/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA
===========================================================================
*/
// cl_cgame.c  -- client system interaction with client game
//
// FAKK2's client game module is a native library (cgamex86.dll) that exports
// GetCGameAPI, which takes nothing and returns the module's entry points;
// the engine then hands CG_Init a table of its own functions
// (clientGameImport_t). There is no system call interface. The table's order
// comes from the SDK's cg_public.h and matches the retail engine's
// (fakk2.exe 0x41ce90 fills 124 slots; two of them, NumSkins and
// Tag_OrientationEx, it leaves NULL).

#include "client.h"
#include "../sys/sys_local.h"
#include "../sys/sys_loadlib.h"
#include "../tiki/tiki.h"
#include "../qcommon/alias.h"
#include "../server/server.h"

#ifdef USE_MUMBLE
#include "libmumblelink.h"
#endif

static void					*cgameLibrary;
static clientGameExport_t	*cge;

/*
====================
CL_GetGameState
====================
*/
void CL_GetGameState( gameState_t *gs ) {
	*gs = cl.gameState;
}

/*
====================
CL_GetGlconfig
====================
*/
void CL_GetGlconfig( glconfig_t *glconfig ) {
	*glconfig = cls.glconfig;
}


/*
====================
CL_GetUserCmd
====================
*/
qboolean CL_GetUserCmd( int cmdNumber, usercmd_t *ucmd ) {
	// cmds[cmdNumber] is the last properly generated command

	// can't return anything that we haven't created yet
	if ( cmdNumber > cl.cmdNumber ) {
		Com_Error( ERR_DROP, "CL_GetUserCmd: %i >= %i", cmdNumber, cl.cmdNumber );
	}

	// the usercmd has been overwritten in the wrapping
	// buffer because it is too far out of date
	if ( cmdNumber <= cl.cmdNumber - CMD_BACKUP ) {
		return qfalse;
	}

	*ucmd = cl.cmds[ cmdNumber & CMD_MASK ];

	return qtrue;
}

int CL_GetCurrentCmdNumber( void ) {
	return cl.cmdNumber;
}


/*
====================
CL_GetParseEntityState
====================
*/
qboolean	CL_GetParseEntityState( int parseEntityNumber, entityState_t *state ) {
	// can't return anything that hasn't been parsed yet
	if ( parseEntityNumber >= cl.parseEntitiesNum ) {
		Com_Error( ERR_DROP, "CL_GetParseEntityState: %i >= %i",
			parseEntityNumber, cl.parseEntitiesNum );
	}

	// can't return anything that has been overwritten in the circular buffer
	if ( parseEntityNumber <= cl.parseEntitiesNum - MAX_PARSE_ENTITIES ) {
		return qfalse;
	}

	*state = cl.parseEntities[ parseEntityNumber & ( MAX_PARSE_ENTITIES - 1 ) ];
	return qtrue;
}

/*
====================
CL_GetCurrentSnapshotNumber
====================
*/
void	CL_GetCurrentSnapshotNumber( int *snapshotNumber, int *serverTime ) {
	*snapshotNumber = cl.snap.messageNum;
	*serverTime = cl.snap.serverTime;
}

/*
====================
CL_GetSnapshot

Follows the retail engine (fakk2.exe 0x41c6d0): a snapshot from before the
last server restart is not handed out, and the entities are ordered so that
an attached entity comes after its parent, which the client game module
places it from. Entities whose parents never turn up are added last, with a
note.
====================
*/
qboolean	CL_GetSnapshot( int snapshotNumber, snapshot_t *snapshot ) {
	clSnapshot_t	*clSnap;
	// -1: not seen, -2: added, otherwise the parent the entity waits for
	static int		placed[MAX_GENTITIES];
	int				i, pass, count;

	if ( snapshotNumber > cl.snap.messageNum ) {
		Com_Error( ERR_DROP, "CL_GetSnapshot: snapshotNumber > cl.snapshot.messageNum" );
	}

	// if the frame has fallen out of the circular buffer, we can't return it
	if ( cl.snap.messageNum - snapshotNumber >= PACKET_BACKUP ) {
		return qfalse;
	}

	// if the frame is not valid, we can't return it
	clSnap = &cl.snapshots[snapshotNumber & PACKET_MASK];
	if ( !clSnap->valid ) {
		return qfalse;
	}

	// a frame from the other side of a server restart is not comparable
	if ( ( clSnap->snapFlags ^ cl.snap.snapFlags ) & SNAPFLAG_SERVERCOUNT ) {
		return qfalse;
	}

	// if the entities in the frame have fallen out of their
	// circular buffer, we can't return it
	if ( cl.parseEntitiesNum - clSnap->parseEntitiesNum >= MAX_PARSE_ENTITIES ) {
		return qfalse;
	}

	// write the snapshot
	snapshot->snapFlags = clSnap->snapFlags;
	snapshot->serverCommandSequence = clSnap->serverCommandNum;
	snapshot->ping = clSnap->ping;
	snapshot->serverTime = clSnap->serverTime;
	Com_Memcpy( snapshot->areamask, clSnap->areamask, sizeof( snapshot->areamask ) );
	snapshot->ps = clSnap->ps;
	snapshot->number_of_sounds = clSnap->number_of_sounds;
	Com_Memcpy( snapshot->sounds, clSnap->sounds, sizeof( snapshot->sounds ) );

	count = clSnap->numEntities;
	if ( count > MAX_ENTITIES_IN_SNAPSHOT ) {
		Com_DPrintf( "CL_GetSnapshot: truncated %i entities to %i\n", count, MAX_ENTITIES_IN_SNAPSHOT );
		count = MAX_ENTITIES_IN_SNAPSHOT;
	}
	snapshot->numEntities = 0;

#define SNAP_ENTITY( n ) cl.parseEntities[ ( clSnap->parseEntitiesNum + ( n ) ) & ( MAX_PARSE_ENTITIES - 1 ) ]
#define SNAP_ADD( es ) ( placed[( es )->number] = -2, snapshot->entities[snapshot->numEntities++] = *( es ) )

	Com_Memset( placed, -1, sizeof( placed ) );
	for ( i = 0 ; i < count ; i++ ) {
		entityState_t	*es = &SNAP_ENTITY( i );

		if ( es->parent == ENTITYNUM_NONE || placed[es->parent] == -2 ) {
			SNAP_ADD( es );
		} else {
			placed[es->number] = es->parent;
		}
	}
	for ( pass = 0 ; pass < 8 && snapshot->numEntities != count ; pass++ ) {
		for ( i = 0 ; i < count ; i++ ) {
			entityState_t	*es = &SNAP_ENTITY( i );

			if ( placed[es->number] >= 0 && placed[placed[es->number]] == -2 ) {
				SNAP_ADD( es );
			}
		}
	}
	if ( snapshot->numEntities != count ) {
		Com_DPrintf( "CL_GetSnapshot: %i entities without their parents\n", count - snapshot->numEntities );
		for ( i = 0 ; i < count ; i++ ) {
			entityState_t	*es = &SNAP_ENTITY( i );

			if ( placed[es->number] >= 0 ) {
				Com_DPrintf( "CL_GetSnapshot: entity %d with parent %d at %.0f %.0f %.0f\n", es->number, es->parent,
					es->origin[0], es->origin[1], es->origin[2] );
				SNAP_ADD( es );
			}
		}
	}

#undef SNAP_ENTITY
#undef SNAP_ADD

	return qtrue;
}

/*
=====================
CL_SetUserCmdValue
=====================
*/
void CL_SetUserCmdValue( int userCmdValue, float sensitivityScale ) {
	cl.cgameUserCmdValue = userCmdValue;
	cl.cgameSensitivity = sensitivityScale;
}

/*
=====================
CL_AddCgameCommand
=====================
*/
void CL_AddCgameCommand( const char *cmdName ) {
	Cmd_AddCommand( cmdName, NULL );
}


/*
=====================
CL_ConfigstringModified
=====================
*/
void CL_ConfigstringModified( void ) {
	char		*old, *s;
	int			i, index;
	char		*dup;
	gameState_t	oldGs;
	int			len;

	index = atoi( Cmd_Argv(1) );
	if ( index < 0 || index >= MAX_CONFIGSTRINGS ) {
		Com_Error( ERR_DROP, "CL_ConfigstringModified: bad index %i", index );
	}
	// get everything after "cs <num>"
	s = Cmd_ArgsFrom(2);

	old = cl.gameState.stringData + cl.gameState.stringOffsets[ index ];
	if ( !strcmp( old, s ) ) {
		return;		// unchanged
	}

	// build the new gameState_t
	oldGs = cl.gameState;

	Com_Memset( &cl.gameState, 0, sizeof( cl.gameState ) );

	// leave the first 0 for uninitialized strings
	cl.gameState.dataCount = 1;

	for ( i = 0 ; i < MAX_CONFIGSTRINGS ; i++ ) {
		if ( i == index ) {
			dup = s;
		} else {
			dup = oldGs.stringData + oldGs.stringOffsets[ i ];
		}
		if ( !dup[0] ) {
			continue;		// leave with the default empty string
		}

		len = strlen( dup );

		if ( len + 1 + cl.gameState.dataCount > MAX_GAMESTATE_CHARS ) {
			Com_Error( ERR_DROP, "MAX_GAMESTATE_CHARS exceeded" );
		}

		// append it to the gameState string buffer
		cl.gameState.stringOffsets[ i ] = cl.gameState.dataCount;
		Com_Memcpy( cl.gameState.stringData + cl.gameState.dataCount, dup, len + 1 );
		cl.gameState.dataCount += len + 1;
	}

	if ( index == CS_SYSTEMINFO ) {
		// parse serverId and other cvars
		CL_SystemInfoChanged();
	}

}


/*
===================
CL_GetServerCommand

Set up argc/argv for the given command
===================
*/
qboolean CL_GetServerCommand( int serverCommandNumber ) {
	char	*s;
	char	*cmd;
	static char bigConfigString[BIG_INFO_STRING];
	int argc;

	// if we have irretrievably lost a reliable command, drop the connection
	if ( serverCommandNumber <= clc.serverCommandSequence - MAX_RELIABLE_COMMANDS ) {
		// when a demo record was started after the client got a whole bunch of
		// reliable commands then the client never got those first reliable commands
		if ( clc.demoplaying )
			return qfalse;
		Com_Error( ERR_DROP, "CL_GetServerCommand: a reliable command was cycled out" );
		return qfalse;
	}

	if ( serverCommandNumber > clc.serverCommandSequence ) {
		Com_Error( ERR_DROP, "CL_GetServerCommand: requested a command not received" );
		return qfalse;
	}

	s = clc.serverCommands[ serverCommandNumber & ( MAX_RELIABLE_COMMANDS - 1 ) ];
	clc.lastExecutedServerCommand = serverCommandNumber;

	Com_DPrintf( "serverCommand: %i : %s\n", serverCommandNumber, s );

rescan:
	Cmd_TokenizeString( s );
	cmd = Cmd_Argv(0);
	argc = Cmd_Argc();

	if ( !strcmp( cmd, "disconnect" ) ) {
		// https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=552
		// allow server to indicate why they were disconnected
		if ( argc >= 2 )
			Com_Error( ERR_SERVERDISCONNECT, "Server disconnected - %s", Cmd_Argv( 1 ) );
		else
			Com_Error( ERR_SERVERDISCONNECT, "Server disconnected" );
	}

	if ( !strcmp( cmd, "bcs0" ) ) {
		Com_sprintf( bigConfigString, BIG_INFO_STRING, "cs %s \"%s", Cmd_Argv(1), Cmd_Argv(2) );
		return qfalse;
	}

	if ( !strcmp( cmd, "bcs1" ) ) {
		s = Cmd_Argv(2);
		if( strlen(bigConfigString) + strlen(s) >= BIG_INFO_STRING ) {
			Com_Error( ERR_DROP, "bcs exceeded BIG_INFO_STRING" );
		}
		strcat( bigConfigString, s );
		return qfalse;
	}

	if ( !strcmp( cmd, "bcs2" ) ) {
		s = Cmd_Argv(2);
		if( strlen(bigConfigString) + strlen(s) + 1 >= BIG_INFO_STRING ) {
			Com_Error( ERR_DROP, "bcs exceeded BIG_INFO_STRING" );
		}
		strcat( bigConfigString, s );
		strcat( bigConfigString, "\"" );
		s = bigConfigString;
		goto rescan;
	}

	if ( !strcmp( cmd, "cs" ) ) {
		CL_ConfigstringModified();
		// reparse the string, because CL_ConfigstringModified may have done another Cmd_TokenizeString()
		Cmd_TokenizeString( s );
		return qtrue;
	}

	if ( !strcmp( cmd, "map_restart" ) ) {
		// clear notify lines and outgoing commands before passing
		// the restart to the cgame
		Con_ClearNotify();
		// reparse the string, because Con_ClearNotify() may have done another Cmd_TokenizeString()
		Cmd_TokenizeString( s );
		Com_Memset( cl.cmds, 0, sizeof( cl.cmds ) );
		return qtrue;
	}

	// the clientLevelShot command is used during development
	// to generate 128*128 screenshots from the intermission
	// point of levels for the menu system to use
	// we pass it along to the cgame to make appropriate adjustments,
	// but we also clear the console and notify lines here
	if ( !strcmp( cmd, "clientLevelShot" ) ) {
		// don't do it if we aren't running the server locally,
		// otherwise malicious remote servers could overwrite
		// the existing thumbnails
		if ( !com_sv_running->integer ) {
			return qfalse;
		}
		// close the console
		Con_Close();
		// take a special screenshot next frame
		Cbuf_AddText( "wait ; wait ; wait ; wait ; screenshot levelshot\n" );
		return qtrue;
	}

	// we may want to put a "connect to other server" command here

	// cgame can now act on the command
	return qtrue;
}


/*
====================
CL_CM_LoadMap

Just adds default parameters that cgame doesn't need to know about
====================
*/
void CL_CM_LoadMap( const char *mapname ) {
	int		checksum;

	CM_LoadMap( mapname, qtrue, &checksum );
}

/*
===========================================================================

The functions handed to the client game module

===========================================================================
*/

static void QDECL CL_CG_Printf( const char *fmt, ... ) {
	va_list		argptr;
	char		msg[MAXPRINTMSG];

	va_start( argptr, fmt );
	Q_vsnprintf( msg, sizeof( msg ), fmt, argptr );
	va_end( argptr );

	Com_Printf( "%s", msg );
}

static void QDECL CL_CG_DPrintf( const char *fmt, ... ) {
	va_list		argptr;
	char		msg[MAXPRINTMSG];

	va_start( argptr, fmt );
	Q_vsnprintf( msg, sizeof( msg ), fmt, argptr );
	va_end( argptr );

	Com_DPrintf( "%s", msg );
}

static void QDECL CL_CG_Error( int errorLevel, const char *fmt, ... ) {
	va_list		argptr;
	char		msg[MAXPRINTMSG];

	va_start( argptr, fmt );
	Q_vsnprintf( msg, sizeof( msg ), fmt, argptr );
	va_end( argptr );

	Com_Error( errorLevel, "%s", msg );
}

static void *CL_CG_Malloc( int size ) {
	return Z_Malloc( size );
}

static void CL_CG_Free( void *block ) {
	if ( block ) {
		Z_Free( block );
	}
}

static int CL_CG_FS_ReadFile( const char *name, void **buf, qboolean quiet ) {
	return FS_ReadFile( name, buf );
}

static void CL_CG_FS_WriteFile( const char *qpath, const void *buffer, int size ) {
	FS_WriteFile( qpath, buffer, size );
}

static int CL_CG_GetSnapshot( int snapshotNumber, snapshot_t *snapshot ) {
	return CL_GetSnapshot( snapshotNumber, snapshot );
}

static void CL_CG_SendConsoleCommand( const char *text ) {
	Cbuf_AddText( text );
}

// The retail engine redraws the screen only until the first snapshot is
// expected (fakk2.exe 0x423490).
static void CL_CG_UpdateLoadingScreen( void ) {
	if ( clc.state < CA_PRIMED ) {
		SCR_UpdateScreen();
	}
}

static void CL_CG_SendClientCommand( const char *s ) {
	CL_AddReliableCommand( s, qfalse );
}

// collision

static int CL_CG_CM_TransformedPointContents( const vec3_t p, int headnode, vec3_t origin, vec3_t angles ) {
	return CM_TransformedPointContents( p, headnode, origin, angles );
}

static void CL_CG_CM_BoxTrace( trace_t *results, const vec3_t start, const vec3_t end,
							   const vec3_t mins, const vec3_t maxs, int headnode, int brushmask, qboolean cylinder ) {
	CM_BoxTrace( results, start, end, mins, maxs, headnode, brushmask, cylinder );
}

static void CL_CG_CM_TransformedBoxTrace( trace_t *results, const vec3_t start, const vec3_t end,
										  const vec3_t mins, const vec3_t maxs, int headnode, int brushmask,
										  const vec3_t origin, const vec3_t angles, qboolean cylinder ) {
	CM_TransformedBoxTrace( results, start, end, mins, maxs, headnode, brushmask, origin, angles, cylinder );
}

static clipHandle_t CL_CG_CM_TempBoxModel( const vec3_t mins, const vec3_t maxs, int contents ) {
	return CM_TempBoxModel( mins, maxs, qfalse, contents );
}

static int CL_CG_CM_MarkFragments( int numPoints, const vec3_t *points, const vec3_t projection,
								   int maxPoints, vec3_t pointBuffer, int maxFragments, markFragment_t *fragmentBuffer ) {
	// FAKK2 fits decals to the collision model's brushes, not the drawn surfaces
	return CM_MarkFragments( numPoints, points, projection, maxPoints, pointBuffer, maxFragments, fragmentBuffer );
}

// sound
//
// Volumes and minimum distances go to the sound system, which takes a
// sound's own from global/sound*.txt when the module passes -1 (see
// snd_info.c). Reverb and the music system are not applied yet.

static void CL_CG_S_StartSound( vec3_t origin, int entnum, int entchannel, sfxHandle_t sfx, float volume, float min_dist ) {
	S_StartSound( origin, entnum, entchannel, sfx, volume, min_dist );
}

// The retail engine (0x43ed80) resolves an alias, registers the name and
// starts it on the listener's entity on CHAN_MENU with the default volume
// and distance.
static void CL_CG_S_StartLocalSound( const char *sound_name ) {
	const char	*name = Alias_FindRandom( sound_name );

	if ( !name ) {
		name = sound_name;
	}
	S_StartLocalSound( S_RegisterSound( name, qfalse ), CHAN_MENU );
}

static void CL_CG_S_StopSound( int entnum, int channel ) {
	S_StopSound( entnum, channel );
}

static void CL_CG_S_ClearLoopingSounds( void ) {
	S_ClearLoopingSounds( qfalse );
}

// FAKK2 names no entity for a looping sound: the sound system merges the
// frame's loops by sound, as the retail engine does (see snd_main.c). The
// retail import (0x43ee10) ignores handle 0. The module always passes a
// velocity of 0 0 0.
static void CL_CG_S_AddLoopingSound( const vec3_t origin, const vec3_t velocity, sfxHandle_t sfx, float volume, float min_dist ) {
	if ( sfx <= 0 ) {
		return;
	}
	S_AddLoopingSound( origin, sfx, volume, min_dist );
}

/*
FAKK2's module builds the listener's axis from its view angles with the yaw negated
("because of the miles sound system", cg_view.c): the retail engine hands Miles its
positions as ( -x, z, -y ) (0x45bf00), and the negated yaw is what makes that come out
the right way round. Quake III's backends take the world as it is, so the axis is
mirrored back here (forward and up across the x-z plane, left the other way, which
leaves the roll negated); with the module's axis as it came, a player who had turned
heard the world mirrored, left for right and front for back.
*/
static void CL_CG_S_Respatialize( int entityNum, vec3_t origin, vec3_t axis[3] ) {
	vec3_t	listenerAxis[3];

	VectorSet( listenerAxis[0], axis[0][0], -axis[0][1], axis[0][2] );
	VectorSet( listenerAxis[1], -axis[1][0], axis[1][1], -axis[1][2] );
	VectorSet( listenerAxis[2], axis[2][0], -axis[2][1], axis[2][2] );
	S_Respatialize( entityNum, origin, listenerAxis, 0 );
}

static sfxHandle_t CL_CG_S_RegisterSound( const char *sample ) {
	return S_RegisterSound( sample, qfalse );
}

static void CL_CG_S_EndRegistration( void ) {
}

static void CL_CG_S_UpdateEntity( int entityNum, const vec3_t origin, const vec3_t velocity, qboolean use_listener ) {
	S_UpdateEntityPosition( entityNum, origin, use_listener );
}

static void CL_CG_S_SetReverb( int reverb_type, float reverb_level ) {
	S_SetReverb( reverb_type, reverb_level );
}

static void CL_CG_S_SetGlobalAmbientVolumeLevel( float volume ) {
}

static void CL_CG_MUSIC_NewSoundtrack( const char *name ) {
	MUSIC_NewSoundtrack( name );
}

static void CL_CG_MUSIC_UpdateMood( int current_mood, int fallback_mood ) {
	MUSIC_UpdateMood( current_mood, fallback_mood );
}

static void CL_CG_MUSIC_UpdateVolume( float volume, float fade_time ) {
	MUSIC_UpdateVolume( volume, fade_time );
}

// FAKK2 (fakk2.exe 0x41ff70): the offset the mouse has turned, whether
// +cameralook is held, and centerview's reset, which is reported once
static float *CL_CG_GetCameraOffset( qboolean *lookactive, qboolean *resetview ) {
	*lookactive = in_cameraLook;
	*resetview = cl_cameraResetView;
	cl_cameraResetView = qfalse;
	return cl_cameraOffset;
}

// renderer

static void CL_CG_BeginRegistration( void ) {
	// the client began the renderer's registration when it started it
}

static void CL_CG_EndRegistration( void ) {
	re.EndRegistration();
}

static qhandle_t CL_CG_R_RegisterModel( const char *name ) {
	int		len = strlen( name );

	if ( len > 4 && !Q_stricmp( name + len - 4, ".tik" ) ) {
		return CL_RegisterTikiModel( name, NULL );
	}
	return re.RegisterModel( name );
}

// cl_showRefEnts <text>: prints every entity the module adds whose model
// name contains the text, and the camera of every scene, for comparing
// placement with the retail game
static cvar_t	*cl_showRefEnts;

static void CL_CG_R_AddRefEntityToScene( refEntity_t *ent ) {
	if ( !cl_showRefEnts ) {
		cl_showRefEnts = Cvar_Get( "cl_showRefEnts", "", CVAR_TEMP );
	}
	if ( cl_showRefEnts->string[0] ) {
		const tiki_t	*tiki = CL_TikiForHandle( ent->hModel );
		const char		*name = tiki ? TIKI_Name( tiki ) : "?";

		if ( Q_stristr( name, cl_showRefEnts->string ) ) {
			vec3_t	angles;

			vectoangles( ent->axis[0], angles );
			Com_Printf( "refent %i %s ent %i org %.1f %.1f %.1f yaw %.0f pitch %.0f axislen %.2f scale %.2f anim %i frame %i old %i/%i lerp %.2f rfx %x\n",
				cl.serverTime, name, ent->entityNumber, ent->origin[0], ent->origin[1], ent->origin[2],
				angles[YAW], angles[PITCH], VectorLength( ent->axis[0] ), ent->scale, ent->anim, ent->frame, ent->oldanim, ent->oldframe,
				ent->backlerp, ent->renderfx );
		}
	}
	re.AddRefEntityToScene( ent );
}

static void CL_CG_R_RenderScene( refdef_t *fd ) {
	if ( cl_showRefEnts && cl_showRefEnts->string[0] ) {
		vec3_t	angles;

		vectoangles( fd->viewaxis[0], angles );
		Com_Printf( "scene %i org %.1f %.1f %.1f yaw %.0f pitch %.0f fov %.0f%s\n", cl.serverTime,
			fd->vieworg[0], fd->vieworg[1], fd->vieworg[2], angles[YAW], angles[PITCH], fd->fov_x,
			fd->sky_portal ? " skyportal" : "" );
	}
	re.RenderScene( fd );
}

static void CL_CG_R_AddRefSpriteToScene( refEntity_t *ent ) {
	re.AddRefSpriteToScene( ent );
}

static void CL_CG_R_AddLightToScene( vec3_t origin, float intensity, float r, float g, float b, int type ) {
	re.AddLightToScene( origin, intensity, r, g, b, type );
}

static void CL_CG_R_AddPolyToScene( qhandle_t hShader, int numVerts, const polyVert_t *verts, int renderfx ) {
	re.AddPolyToScene( hShader, numVerts, verts, 1, renderfx );
}

static void CL_CG_R_ModelBounds( clipHandle_t model, vec3_t mins, vec3_t maxs ) {
	re.ModelBounds( model, mins, maxs );
}

static float CL_CG_R_ModelRadius( clipHandle_t model ) {
	return re.ModelRadius( model );
}

static void CL_CG_R_DebugLine( vec3_t start, vec3_t end, float r, float g, float b, float alpha ) {
	re.DebugLine( start, end, r, g, b, alpha );
}

static void CL_CG_R_SwipePoint( vec3_t p1, vec3_t p2, float time ) {
	re.SwipePoint( p1, p2, time );
}

// TIKI
//
// The client game module gets a model's definition through TIKI_GetHandle
// from the renderer's model handle. Here the two handles are the same
// number; a handle that is not a .tik model has no definition.

static tiki_t *CL_Tiki( int tikihandle ) {
	return CL_TikiForHandle( tikihandle );
}

static int CL_CG_TIKI_GetHandle( qhandle_t handle ) {
	return CL_TikiForHandle( handle ) ? handle : -1;
}

static int CL_CG_NumAnims( int tikihandle ) { return TIKI_NumAnims( CL_Tiki( tikihandle ) ); }
static int CL_CG_NumSkins( int tikihandle ) { return TIKI_NumSkins( CL_Tiki( tikihandle ) ); }
static int CL_CG_NumSurfaces( int tikihandle ) { return TIKI_NumSurfaces( CL_Tiki( tikihandle ) ); }
static int CL_CG_NumTags( int tikihandle ) { return TIKI_NumTags( CL_Tiki( tikihandle ) ); }
static qboolean CL_CG_InitCommands( int tikihandle, tiki_cmd_t *tiki_cmd ) { return TIKI_InitCommands( CL_Tiki( tikihandle ), qfalse, tiki_cmd ); }
static void CL_CG_CalculateBounds( int tikihandle, float scale, vec3_t mins, vec3_t maxs ) { TIKI_CalculateBounds( CL_Tiki( tikihandle ), scale, mins, maxs ); }
static void CL_CG_FlushAll( void ) {}
static const char *CL_CG_TIKI_NameForNum( int tikihandle ) {
	const char	*name = TIKI_Name( CL_Tiki( tikihandle ) );

	return name ? name : "";
}

static const char *CL_CG_Anim_NameForNum( int tikihandle, int animnum ) { return TIKI_Anim_NameForNum( CL_Tiki( tikihandle ), animnum ); }
static int CL_CG_Anim_NumForName( int tikihandle, const char *name ) { return TIKI_Anim_NumForName( CL_Tiki( tikihandle ), name ); }
static int CL_CG_Anim_Random( int tikihandle, const char *name ) { return TIKI_Anim_Random( CL_Tiki( tikihandle ), name ); }
static int CL_CG_Anim_NumFrames( int tikihandle, int animnum ) { return TIKI_Anim_NumFrames( CL_Tiki( tikihandle ), animnum ); }
static float CL_CG_Anim_Time( int tikihandle, int animnum ) { return TIKI_Anim_Time( CL_Tiki( tikihandle ), animnum ); }
static void CL_CG_Anim_Delta( int tikihandle, int animnum, vec3_t delta ) { TIKI_Anim_Delta( CL_Tiki( tikihandle ), animnum, delta ); }
static int CL_CG_Anim_Flags( int tikihandle, int animnum ) { return TIKI_Anim_Flags( CL_Tiki( tikihandle ), animnum ); }
static int CL_CG_Anim_CrossblendTime( int tikihandle, int animnum ) { return TIKI_Anim_CrossblendTime( CL_Tiki( tikihandle ), animnum ); }
static qboolean CL_CG_Anim_HasCommands( int tikihandle, int animnum ) { return TIKI_Anim_HasCommands( CL_Tiki( tikihandle ), animnum, qfalse ); }

static qboolean CL_CG_Frame_Commands( int tikihandle, int animnum, int framenum, tiki_cmd_t *tiki_cmd ) { return TIKI_Frame_Commands( CL_Tiki( tikihandle ), animnum, framenum, qfalse, tiki_cmd ); }
static void CL_CG_Frame_Delta( int tikihandle, int animnum, int framenum, vec3_t delta ) { TIKI_Frame_Delta( CL_Tiki( tikihandle ), animnum, framenum, delta ); }
static float CL_CG_Frame_Time( int tikihandle, int animnum, int framenum ) { return TIKI_Frame_Time( CL_Tiki( tikihandle ), animnum, framenum ); }
static void CL_CG_Frame_Bounds( int tikihandle, int animnum, int framenum, float scale, vec3_t mins, vec3_t maxs ) { TIKI_Frame_Bounds( CL_Tiki( tikihandle ), animnum, framenum, scale, mins, maxs ); }
static float CL_CG_Frame_Radius( int tikihandle, int animnum, int framenum ) {
	vec3_t	mins, maxs;

	TIKI_Frame_Bounds( CL_Tiki( tikihandle ), animnum, framenum, 1.0f, mins, maxs );
	return RadiusFromBounds( mins, maxs );
}

static int CL_CG_Surface_NameToNum( int tikihandle, const char *name ) { return TIKI_Surface_NameToNum( CL_Tiki( tikihandle ), name ); }
static const char *CL_CG_Surface_NumToName( int tikihandle, int num ) { return TIKI_Surface_NumToName( CL_Tiki( tikihandle ), num ); }
static int CL_CG_Surface_Flags( int tikihandle, int num ) { return TIKI_Surface_Flags( CL_Tiki( tikihandle ), num ); }
static int CL_CG_Surface_NumSkins( int tikihandle, int num ) { return TIKI_Surface_NumSkins( CL_Tiki( tikihandle ), num ); }

static int CL_CG_Tag_NumForName( int tikihandle, const char *name ) { return TIKI_Tag_NumForName( CL_Tiki( tikihandle ), name ); }
static const char *CL_CG_Tag_NameForNum( int tikihandle, int num ) { return TIKI_Tag_NameForNum( CL_Tiki( tikihandle ), num ); }

// retail's table points Tag_Orientation straight at the TIKI code (0x44fe40)
static orientation_t CL_CG_Tag_Orientation( int tikihandle, int anim, int frame, int tagnum, float scale, int *bone_tag, vec4_t *bone_quat ) {
	return TIKI_Tag_Orientation( CL_Tiki( tikihandle ), anim, frame, tagnum, scale, bone_tag, bone_quat );
}

// retail leaves this slot NULL; the server's import of the same shape is followed
static orientation_t CL_CG_Tag_OrientationEx( int tikihandle, int anim, int frame, int tagnum, float scale, int *bone_tag, vec4_t *bone_quat,
	int crossblend_anim, int crossblend_frame, float crossblend_lerp, qboolean uselegs, qboolean usetorso, int torso_anim, int torso_frame,
	int torso_crossblend_anim, int torso_crossblend_frame, float torso_crossblend_lerp ) {
	tikiAnimState_t	st;

	TIKI_AnimStateFrame( &st, anim, frame, scale, bone_tag, bone_quat );
	st.crossblend_anim = crossblend_anim;
	st.crossblend_frame = crossblend_frame;
	st.crossblend_lerp = crossblend_lerp;
	st.uselegs = uselegs;
	st.usetorso = usetorso;
	st.torso_anim = st.torso_oldanim = torso_anim;
	st.torso_frame = st.torso_oldframe = torso_frame;
	st.torso_crossblend_anim = torso_crossblend_anim;
	st.torso_crossblend_frame = torso_crossblend_frame;
	st.torso_crossblend_lerp = torso_crossblend_lerp;
	return TIKI_Tag_OrientationEx( CL_Tiki( tikihandle ), &st, tagnum );
}

// retail (0x450560) looks the name up and asks for the frame with a cross-blend
// lerp of 0 towards animation 0, which would place the tag from that animation
// instead; the SDK client game never calls this, so the frame asked for is used
static orientation_t CL_CG_Tag_NamedOrientation( int tikihandle, int anim, int frame, float scale, const char *tagName, int *bone_tag, vec4_t *bone_quat ) {
	tiki_t	*tiki = CL_Tiki( tikihandle );

	return TIKI_Tag_Orientation( tiki, anim, frame, TIKI_Tag_NumForName( tiki, tagName ), scale, bone_tag, bone_quat );
}

// the tag of the frame the model shows, interpolated towards its old frame and
// cross-blended as the model is drawn (0x451c90)
static orientation_t CL_CG_Tag_LerpedOrientation( int tikihandle, refEntity_t *model, int tagnum ) {
	tikiAnimState_t	st;

	Com_Memset( &st, 0, sizeof( st ) );
	st.anim = model->anim;
	st.frame = model->frame;
	st.oldanim = model->oldanim;
	st.oldframe = model->oldframe;
	st.backlerp = model->backlerp;
	st.crossblend_anim = model->crossblend_anim;
	st.crossblend_frame = model->crossblend_frame;
	st.crossblend_lerp = model->crossblend_lerp;
	st.uselegs = model->uselegs;
	st.usetorso = model->usetorso;
	st.torso_anim = model->torso_anim;
	st.torso_frame = model->torso_frame;
	st.torso_oldanim = model->torso_oldanim;
	st.torso_oldframe = model->torso_oldframe;
	st.torso_backlerp = model->torso_backlerp;
	st.torso_crossblend_anim = model->torso_crossblend_anim;
	st.torso_crossblend_frame = model->torso_crossblend_frame;
	st.torso_crossblend_lerp = model->torso_crossblend_lerp;
	st.scale = model->scale;
	st.bone_tag = model->bone_tag;
	st.bone_quat = model->bone_quat;
	return TIKI_Tag_OrientationEx( CL_Tiki( tikihandle ), &st, tagnum );
}

static AliasList_t *CL_TikiAliases( int tikihandle, qboolean create ) {
	tiki_t		*tiki = CL_Tiki( tikihandle );
	AliasList_t	**list;

	if ( !tiki ) {
		return NULL;
	}
	list = TIKI_AliasList( tiki );
	if ( !*list && create ) {
		*list = AliasList_New( TIKI_Name( tiki ) );
	}
	return *list;
}

static qboolean CL_CG_TIKI_Alias_Add( int tikihandle, const char *alias, const char *name, const char *parameters ) {
	return Alias_ListAdd( CL_TikiAliases( tikihandle, qtrue ), alias, name, parameters );
}
static const char *CL_CG_TIKI_Alias_FindRandom( int tikihandle, const char *alias ) {
	return Alias_ListFindRandom( CL_TikiAliases( tikihandle, qfalse ), alias );
}
static void CL_CG_TIKI_Alias_Dump( int tikihandle ) {
	Alias_ListDump( CL_TikiAliases( tikihandle, qfalse ) );
}
static void CL_CG_TIKI_Alias_Clear( int tikihandle ) {
	Alias_ListClear( CL_TikiAliases( tikihandle, qfalse ) );
}
static const char *CL_CG_TIKI_Alias_FindDialog( int tikihandle, const char *alias, int random, int entity_number ) {
	return Alias_ListFindDialog( CL_TikiAliases( tikihandle, qfalse ), alias, random, entity_number );
}

/*
====================
CL_FillCGameImport
====================
*/
static void CL_FillCGameImport( clientGameImport_t *import ) {
	Com_Memset( import, 0, sizeof( *import ) );

	import->apiversion = CGAME_IMPORT_API_VERSION;

	import->Printf = CL_CG_Printf;
	import->DPrintf = CL_CG_DPrintf;
	import->DebugPrintf = CL_CG_DPrintf;
	import->Malloc = CL_CG_Malloc;
	import->Free = CL_CG_Free;
	import->Error = CL_CG_Error;
	import->Milliseconds = Sys_Milliseconds;
	import->Cvar_Get = Cvar_Get;
	import->Cvar_Set = Cvar_Set;
	import->Argc = Cmd_Argc;
	import->Argv = Cmd_Argv;
	import->Args = Cmd_Args;
	import->AddCommand = CL_AddCgameCommand;
	import->FS_ReadFile = CL_CG_FS_ReadFile;
	import->FS_FreeFile = FS_FreeFile;
	import->FS_WriteFile = CL_CG_FS_WriteFile;
	import->FS_WriteTextFile = CL_CG_FS_WriteFile;
	import->SendConsoleCommand = CL_CG_SendConsoleCommand;
	import->UpdateLoadingScreen = CL_CG_UpdateLoadingScreen;
	import->SendClientCommand = CL_CG_SendClientCommand;

	import->CM_LoadMap = CL_CM_LoadMap;
	import->CM_InlineModel = CM_InlineModel;
	import->CM_NumInlineModels = CM_NumInlineModels;
	import->CM_PointContents = CM_PointContents;
	import->CM_TransformedPointContents = CL_CG_CM_TransformedPointContents;
	import->CM_BoxTrace = CL_CG_CM_BoxTrace;
	import->CM_TransformedBoxTrace = CL_CG_CM_TransformedBoxTrace;
	import->CM_TempBoxModel = CL_CG_CM_TempBoxModel;
	import->CM_MarkFragments = CL_CG_CM_MarkFragments;

	import->S_StartSound = CL_CG_S_StartSound;
	import->S_StartLocalSound = CL_CG_S_StartLocalSound;
	import->S_StopSound = CL_CG_S_StopSound;
	import->S_ClearLoopingSounds = CL_CG_S_ClearLoopingSounds;
	import->S_AddLoopingSound = CL_CG_S_AddLoopingSound;
	import->S_Respatialize = CL_CG_S_Respatialize;
	import->S_BeginRegistration = S_BeginRegistration;
	import->S_RegisterSound = CL_CG_S_RegisterSound;
	import->S_EndRegistration = CL_CG_S_EndRegistration;
	import->S_UpdateEntity = CL_CG_S_UpdateEntity;
	import->S_SetReverb = CL_CG_S_SetReverb;
	import->S_SetGlobalAmbientVolumeLevel = CL_CG_S_SetGlobalAmbientVolumeLevel;

	import->MUSIC_NewSoundtrack = CL_CG_MUSIC_NewSoundtrack;
	import->MUSIC_UpdateMood = CL_CG_MUSIC_UpdateMood;
	import->MUSIC_UpdateVolume = CL_CG_MUSIC_UpdateVolume;

	import->get_lip_length = SV_SoundLength;
	import->get_lip_amplitudes = SV_SoundAmplitudes;

	import->get_camera_offset = CL_CG_GetCameraOffset;

	import->BeginRegistration = CL_CG_BeginRegistration;
	import->EndRegistration = CL_CG_EndRegistration;
	import->R_ClearScene = re.ClearScene;
	import->R_RenderScene = CL_CG_R_RenderScene;
	import->R_LoadWorldMap = re.LoadWorld;
	import->R_RegisterModel = CL_CG_R_RegisterModel;
	import->R_RegisterSkin = re.RegisterSkin;
	import->R_RegisterShader = re.RegisterShader;
	import->R_RegisterShaderNoMip = re.RegisterShaderNoMip;
	import->R_AddRefEntityToScene = CL_CG_R_AddRefEntityToScene;
	import->R_AddRefSpriteToScene = CL_CG_R_AddRefSpriteToScene;
	import->R_AddLightToScene = CL_CG_R_AddLightToScene;
	import->R_AddPolyToScene = CL_CG_R_AddPolyToScene;
	import->R_SetColor = re.SetColor;
	import->R_DrawStretchPic = re.DrawStretchPic;
	import->R_GetRenderEntity = re.GetRenderEntity;
	import->R_ModelBounds = CL_CG_R_ModelBounds;
	import->R_ModelRadius = CL_CG_R_ModelRadius;
	import->R_Noise = re.Noise;
	import->R_DebugLine = CL_CG_R_DebugLine;
	import->R_SwipeBegin = re.SwipeBegin;
	import->R_SwipePoint = CL_CG_R_SwipePoint;
	import->R_SwipeEnd = re.SwipeEnd;
	import->R_GetShaderWidth = re.GetShaderWidth;
	import->R_GetShaderHeight = re.GetShaderHeight;
	import->R_DrawBox = re.DrawBox;

	import->GetGameState = CL_GetGameState;
	import->GetSnapshot = CL_CG_GetSnapshot;
	import->GetCurrentSnapshotNumber = CL_GetCurrentSnapshotNumber;
	import->GetGlconfig = CL_GetGlconfig;
	import->GetParseEntityState = CL_GetParseEntityState;
	import->GetCurrentCmdNumber = CL_GetCurrentCmdNumber;
	import->GetUserCmd = CL_GetUserCmd;
	import->GetServerCommand = CL_GetServerCommand;

	import->Alias_Add = Alias_Add;
	import->Alias_FindRandom = Alias_FindRandom;
	import->Alias_Dump = Alias_Dump;
	import->Alias_Clear = Alias_Clear;

	import->TIKI_GetHandle = CL_CG_TIKI_GetHandle;
	import->NumAnims = CL_CG_NumAnims;
	import->NumSkins = CL_CG_NumSkins;
	import->NumSurfaces = CL_CG_NumSurfaces;
	import->NumTags = CL_CG_NumTags;
	import->InitCommands = CL_CG_InitCommands;
	import->CalculateBounds = CL_CG_CalculateBounds;
	import->FlushAll = CL_CG_FlushAll;
	import->TIKI_NameForNum = CL_CG_TIKI_NameForNum;

	import->Anim_NameForNum = CL_CG_Anim_NameForNum;
	import->Anim_NumForName = CL_CG_Anim_NumForName;
	import->Anim_Random = CL_CG_Anim_Random;
	import->Anim_NumFrames = CL_CG_Anim_NumFrames;
	import->Anim_Time = CL_CG_Anim_Time;
	import->Anim_Delta = CL_CG_Anim_Delta;
	import->Anim_Flags = CL_CG_Anim_Flags;
	import->Anim_CrossblendTime = CL_CG_Anim_CrossblendTime;
	import->Anim_HasCommands = CL_CG_Anim_HasCommands;

	import->Frame_Commands = CL_CG_Frame_Commands;
	import->Frame_Delta = CL_CG_Frame_Delta;
	import->Frame_Time = CL_CG_Frame_Time;
	import->Frame_Bounds = CL_CG_Frame_Bounds;
	import->Frame_Radius = CL_CG_Frame_Radius;

	import->Surface_NameToNum = CL_CG_Surface_NameToNum;
	import->Surface_NumToName = CL_CG_Surface_NumToName;
	import->Surface_Flags = CL_CG_Surface_Flags;
	import->Surface_NumSkins = CL_CG_Surface_NumSkins;

	import->Tag_NumForName = CL_CG_Tag_NumForName;
	import->Tag_NameForNum = CL_CG_Tag_NameForNum;
	import->Tag_Orientation = CL_CG_Tag_Orientation;
	import->Tag_OrientationEx = CL_CG_Tag_OrientationEx;
	import->Tag_NamedOrientation = CL_CG_Tag_NamedOrientation;
	import->Tag_LerpedOrientation = CL_CG_Tag_LerpedOrientation;

	import->TIKI_Alias_Add = CL_CG_TIKI_Alias_Add;
	import->TIKI_Alias_FindRandom = CL_CG_TIKI_Alias_FindRandom;
	import->TIKI_Alias_Dump = CL_CG_TIKI_Alias_Dump;
	import->TIKI_Alias_Clear = CL_CG_TIKI_Alias_Clear;
	import->TIKI_Alias_FindDialog = CL_CG_TIKI_Alias_FindDialog;
}

/*
===============
CL_LoadCGameLibrary

Looks beside the server game module: the home directory, the executable's
directory and the base directory, each under the current game directory.
===============
*/
static void *CL_LoadCGameLibrary( char *loadedFrom, int loadedFromSize ) {
	const char	*bases[3];
	const char	*gamedir = FS_GetCurrentGameDir();
	char		name[MAX_QPATH];
	int			i;

	Com_sprintf( name, sizeof( name ), "cgame" ARCH_STRING DLL_EXT );

	bases[0] = Cvar_VariableString( "fs_homepath" );
	bases[1] = Sys_BinaryPath();
	bases[2] = Cvar_VariableString( "fs_basepath" );

	for ( i = 0 ; i < (int)ARRAY_LEN( bases ) ; i++ ) {
		char	*path;
		void	*library;

		if ( !bases[i] || !bases[i][0] ) {
			continue;
		}
		path = FS_BuildOSPath( bases[i], gamedir, name );
		library = Sys_LoadLibrary( path );
		if ( library ) {
			Q_strncpyz( loadedFrom, path, loadedFromSize );
			return library;
		}
	}
	return NULL;
}

/*
====================
CL_ShutdownCGame

====================
*/
void CL_ShutdownCGame( void ) {
	Key_SetCatcher( Key_GetCatcher( ) & ~KEYCATCH_CGAME );
	cls.cgameStarted = qfalse;
	if ( cge ) {
		cge->CG_Shutdown();
		cge = NULL;
	}
	if ( cgameLibrary ) {
		Sys_UnloadLibrary( cgameLibrary );
		cgameLibrary = NULL;
	}
}

/*
====================
CL_InitCGame

Should only be called by CL_StartHunkUsers
====================
*/
void CL_InitCGame( void ) {
	typedef clientGameExport_t *( QDECL *GetCGameAPIProc )( void );
	static clientGameImport_t	import;
	GetCGameAPIProc		GetCGameAPI;
	char				loadedFrom[MAX_OSPATH];
	const char			*info;
	const char			*mapname;
	const char			*spawnpos;
	int					t1, t2;

	t1 = Sys_Milliseconds();

	// put away the console
	Con_Close();

	// find the current mapname
	info = cl.gameState.stringData + cl.gameState.stringOffsets[ CS_SERVERINFO ];
	mapname = Info_ValueForKey( info, "mapname" );
	// FAKK2: a spawn point may follow the bsp's name after a '$'
	spawnpos = strchr( mapname, '$' );
	Com_sprintf( cl.mapname, sizeof( cl.mapname ), "maps/%.*s.bsp",
		spawnpos ? (int)( spawnpos - mapname ) : (int)strlen( mapname ), mapname );

	// load the library
	CL_ShutdownCGame();
	cgameLibrary = CL_LoadCGameLibrary( loadedFrom, sizeof( loadedFrom ) );
	if ( !cgameLibrary ) {
		Com_Error( ERR_DROP, "Couldn't load cgame" ARCH_STRING DLL_EXT );
	}
	GetCGameAPI = (GetCGameAPIProc)Sys_LoadFunction( cgameLibrary, "GetCGameAPI" );
	if ( !GetCGameAPI ) {
		Sys_UnloadLibrary( cgameLibrary );
		cgameLibrary = NULL;
		Com_Error( ERR_DROP, "%s does not export GetCGameAPI", loadedFrom );
	}
	cge = GetCGameAPI();
	if ( !cge || !cge->CG_Init || !cge->CG_DrawActiveFrame ) {
		cge = NULL;
		Sys_UnloadLibrary( cgameLibrary );
		cgameLibrary = NULL;
		Com_Error( ERR_DROP, "%s returned no client game interface", loadedFrom );
	}
	Com_Printf( "Loaded client game module from %s\n", loadedFrom );

	CL_FillCGameImport( &import );
	clc.state = CA_LOADING;

	// init for this gamestate
	// use the lastExecutedServerCommand instead of the serverCommandSequence
	// otherwise server commands sent just before a gamestate are dropped
	S_BeginRegistration();
	cge->CG_Init( &import, clc.serverMessageSequence, clc.lastExecutedServerCommand );

	// reset any CVAR_CHEAT cvars registered by cgame
	if ( !clc.demoplaying && !cl_connectedToCheatServer )
		Cvar_SetCheatState();

	// we will send a usercmd this frame, which
	// will cause the server to send us the first snapshot
	clc.state = CA_PRIMED;

	t2 = Sys_Milliseconds();

	Com_Printf( "CL_InitCGame: %5.2f seconds\n", (t2-t1)/1000.0 );

	// have the renderer touch all its images, so they are present
	// on the card even if the driver does deferred loading
	re.EndRegistration();

	// make sure everything is paged in
	if (!Sys_LowPhysicalMemory()) {
		Com_TouchMemory();
	}

	// clear anything that got printed
	Con_ClearNotify ();
}


/*
====================
CL_GameCommand

See if the current console command is claimed by the cgame
====================
*/
qboolean CL_GameCommand( void ) {
	if ( !cge ) {
		return qfalse;
	}

	return cge->CG_ConsoleCommand();
}



/*
=====================
CL_CGameRendering

The scene, then the game's 2D layer over it once play has begun.
=====================
*/
void CL_CGameRendering( stereoFrame_t stereo ) {
	if ( !cge ) {
		return;
	}
	cge->CG_DrawActiveFrame( cl.serverTime, stereo, clc.demoplaying );
	if ( clc.state == CA_ACTIVE ) {
		cge->CG_Draw2D();
	}
}


/*
=================
CL_AdjustTimeDelta

Adjust the clients view of server time.

We attempt to have cl.serverTime exactly equal the server's view
of time plus the timeNudge, but with variable latencies over
the internet it will often need to drift a bit to match conditions.

Our ideal time would be to have the adjusted time approach, but not pass,
the very latest snapshot.

Adjustments are only made when a new snapshot arrives with a rational
latency, which keeps the adjustment process framerate independent and
prevents massive overadjustment during times of significant packet loss
or bursted delayed packets.
=================
*/

#define	RESET_TIME	500

void CL_AdjustTimeDelta( void ) {
	int		newDelta;
	int		deltaDelta;

	cl.newSnapshots = qfalse;

	// the delta never drifts when replaying a demo
	if ( clc.demoplaying ) {
		return;
	}

	newDelta = cl.snap.serverTime - cls.realtime;
	deltaDelta = abs( newDelta - cl.serverTimeDelta );

	if ( deltaDelta > RESET_TIME ) {
		cl.serverTimeDelta = newDelta;
		cl.oldServerTime = cl.snap.serverTime;	// FIXME: is this a problem for cgame?
		cl.serverTime = cl.snap.serverTime;
		if ( cl_showTimeDelta->integer ) {
			Com_Printf( "<RESET> " );
		}
	} else if ( deltaDelta > 100 ) {
		// fast adjust, cut the difference in half
		if ( cl_showTimeDelta->integer ) {
			Com_Printf( "<FAST> " );
		}
		cl.serverTimeDelta = ( cl.serverTimeDelta + newDelta ) >> 1;
	} else {
		// slow drift adjust, only move 1 or 2 msec

		// if any of the frames between this and the previous snapshot
		// had to be extrapolated, nudge our sense of time back a little
		// the granularity of +1 / -2 is too high for timescale modified frametimes
		if ( com_timescale->value == 0 || com_timescale->value == 1 ) {
			if ( cl.extrapolatedSnapshot ) {
				cl.extrapolatedSnapshot = qfalse;
				cl.serverTimeDelta -= 2;
			} else {
				// otherwise, move our sense of time forward to minimize total latency
				cl.serverTimeDelta++;
			}
		}
	}

	if ( cl_showTimeDelta->integer ) {
		Com_Printf( "%i ", cl.serverTimeDelta );
	}
}


/*
==================
CL_FirstSnapshot
==================
*/
void CL_FirstSnapshot( void ) {
	// ignore snapshots that don't have entities
	if ( cl.snap.snapFlags & SNAPFLAG_NOT_ACTIVE ) {
		return;
	}
	clc.state = CA_ACTIVE;
	URC_EndLoading();		// the retail client's UI_EndLoad (0x41d520): the loading screen goes

	// set the timedelta so we are exactly on this first frame
	cl.serverTimeDelta = cl.snap.serverTime - cls.realtime;
	cl.oldServerTime = cl.snap.serverTime;

	clc.timeDemoBaseTime = cl.snap.serverTime;

	// if this is the first frame of active play,
	// execute the contents of activeAction now
	// this is to allow scripting a timedemo to start right
	// after loading
	if ( cl_activeAction->string[0] ) {
		Cbuf_AddText( cl_activeAction->string );
		Cvar_Set( "activeAction", "" );
	}

#ifdef USE_MUMBLE
	if ((cl_useMumble->integer) && !mumble_islinked()) {
		int ret = mumble_link(CLIENT_WINDOW_TITLE);
		Com_Printf("Mumble: Linking to Mumble application %s\n", ret==0?"ok":"failed");
	}
#endif

#ifdef USE_VOIP
	if (!clc.voipCodecInitialized) {
		int i;
		int error;

		clc.opusEncoder = opus_encoder_create(48000, 1, OPUS_APPLICATION_VOIP, &error);

		if ( error ) {
			Com_DPrintf("VoIP: Error opus_encoder_create %d\n", error);
			return;
		}

		for (i = 0; i < MAX_CLIENTS; i++) {
			clc.opusDecoder[i] = opus_decoder_create(48000, 1, &error);
			if ( error ) {
				Com_DPrintf("VoIP: Error opus_decoder_create(%d) %d\n", i, error);
				return;
			}
			clc.voipIgnore[i] = qfalse;
			clc.voipGain[i] = 1.0f;
		}
		clc.voipCodecInitialized = qtrue;
		clc.voipMuteAll = qfalse;
		Cmd_AddCommand ("voip", CL_Voip_f);
		Cvar_Set("cl_voipSendTarget", "spatial");
		Com_Memset(clc.voipTargets, ~0, sizeof(clc.voipTargets));
	}
#endif
}

/*
==================
CL_SetCGameTime
==================
*/
void CL_SetCGameTime( void ) {
	// getting a valid frame message ends the connection process
	if ( clc.state != CA_ACTIVE ) {
		if ( clc.state != CA_PRIMED ) {
			return;
		}
		if ( clc.demoplaying ) {
			// we shouldn't get the first snapshot on the same frame
			// as the gamestate, because it causes a bad time skip
			if ( !clc.firstDemoFrameSkipped ) {
				clc.firstDemoFrameSkipped = qtrue;
				return;
			}
			CL_ReadDemoMessage();
		}
		if ( cl.newSnapshots ) {
			cl.newSnapshots = qfalse;
			CL_FirstSnapshot();
		}
		if ( clc.state != CA_ACTIVE ) {
			return;
		}
	}

	// if we have gotten to this point, cl.snap is guaranteed to be valid
	if ( !cl.snap.valid ) {
		Com_Error( ERR_DROP, "CL_SetCGameTime: !cl.snap.valid" );
	}

	// allow pause in single player
	if ( sv_paused->integer && CL_CheckPaused() && com_sv_running->integer ) {
		// paused. FAKK2 (deliberate): the server's clock stands still, so hold
		// ours by taking the frame off the delta. Retail (0x41d590) and Quake III
		// let cls.realtime run on, and the first command after the pause jumped
		// the player through the whole of it at once; the server then dropped
		// every command after it as old for as long again (the menus pause the
		// game, so after a save the player had no control for a while)
		cl.serverTimeDelta -= cls.frametime;
		return;
	}

	if ( cl.snap.serverTime < cl.oldFrameServerTime ) {
		Com_Error( ERR_DROP, "cl.snap.serverTime < cl.oldFrameServerTime" );
	}
	cl.oldFrameServerTime = cl.snap.serverTime;


	// get our current view of time

	if ( clc.demoplaying && cl_freezeDemo->integer ) {
		// cl_freezeDemo is used to lock a demo in place for single frame advances

	} else {
		// cl_timeNudge is a user adjustable cvar that allows more
		// or less latency to be added in the interest of better
		// smoothness or better responsiveness.
		int tn;

		tn = cl_timeNudge->integer;
		if (tn<-30) {
			tn = -30;
		} else if (tn>30) {
			tn = 30;
		}

		cl.serverTime = cls.realtime + cl.serverTimeDelta - tn;

		// guarantee that time will never flow backwards, even if
		// serverTimeDelta made an adjustment or cl_timeNudge was changed
		if ( cl.serverTime < cl.oldServerTime ) {
			cl.serverTime = cl.oldServerTime;
		}
		cl.oldServerTime = cl.serverTime;

		// note if we are almost past the latest frame (without timeNudge),
		// so we will try and adjust back a bit when the next snapshot arrives
		if ( cls.realtime + cl.serverTimeDelta >= cl.snap.serverTime - 5 ) {
			cl.extrapolatedSnapshot = qtrue;
		}
	}

	// if we have gotten new snapshots, drift serverTimeDelta
	// don't do this every frame, or a period of packet loss would
	// make a huge adjustment
	if ( cl.newSnapshots ) {
		CL_AdjustTimeDelta();
	}

	if ( !clc.demoplaying ) {
		return;
	}

	// if we are playing a demo back, we can just keep reading
	// messages from the demo file until the cgame definitely
	// has valid snapshots to interpolate between

	// a timedemo will always use a deterministic set of time samples
	// no matter what speed machine it is run on,
	// while a normal demo may have different time samples
	// each time it is played back
	if ( cl_timedemo->integer ) {
		int now = Sys_Milliseconds( );
		int frameDuration;

		if (!clc.timeDemoStart) {
			clc.timeDemoStart = clc.timeDemoLastFrame = now;
			clc.timeDemoMinDuration = INT_MAX;
			clc.timeDemoMaxDuration = 0;
		}

		frameDuration = now - clc.timeDemoLastFrame;
		clc.timeDemoLastFrame = now;

		// Ignore the first measurement as it'll always be 0
		if( clc.timeDemoFrames > 0 )
		{
			if( frameDuration > clc.timeDemoMaxDuration )
				clc.timeDemoMaxDuration = frameDuration;

			if( frameDuration < clc.timeDemoMinDuration )
				clc.timeDemoMinDuration = frameDuration;

			// 255 ms = about 4fps
			if( frameDuration > UCHAR_MAX )
				frameDuration = UCHAR_MAX;

			clc.timeDemoDurations[ ( clc.timeDemoFrames - 1 ) %
				MAX_TIMEDEMO_DURATIONS ] = frameDuration;
		}

		clc.timeDemoFrames++;
		cl.serverTime = clc.timeDemoBaseTime + clc.timeDemoFrames * 50;
	}

	while ( cl.serverTime >= cl.snap.serverTime ) {
		// feed another messag, which should change
		// the contents of cl.snap
		CL_ReadDemoMessage();
		if ( clc.state != CA_ACTIVE ) {
			return;		// end of demo
		}
	}

}
