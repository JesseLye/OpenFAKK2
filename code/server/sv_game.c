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
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
// sv_game.c -- interface to the game dll

#include "server.h"
#include "../sys/sys_local.h"
#include "../sys/sys_loadlib.h"

#include "../botlib/botlib.h"

botlib_export_t	*botlib_export;

// these functions must be used instead of pointer arithmetic, because
// the game allocates gentities with private information after the server shared part
int	SV_NumForGentity( sharedEntity_t *ent ) {
	int		num;

	num = ( (byte *)ent - (byte *)sv.gentities ) / sv.gentitySize;

	return num;
}

sharedEntity_t *SV_GentityNum( int num ) {
	sharedEntity_t *ent;

	ent = (sharedEntity_t *)((byte *)sv.gentities + sv.gentitySize*(num));

	return ent;
}

playerState_t *SV_GameClientNum( int num ) {
	playerState_t	*ps;

	ps = (playerState_t *)((byte *)sv.gameClients + sv.gameClientSize*(num));

	return ps;
}

svEntity_t	*SV_SvEntityForGentity( sharedEntity_t *gEnt ) {
	if ( !gEnt || gEnt->s.number < 0 || gEnt->s.number >= MAX_GENTITIES ) {
		Com_Error( ERR_DROP, "SV_SvEntityForGentity: bad gEnt" );
	}
	return &sv.svEntities[ gEnt->s.number ];
}

sharedEntity_t *SV_GEntityForSvEntity( svEntity_t *svEnt ) {
	int		num;

	num = svEnt - sv.svEntities;
	return SV_GentityNum( num );
}

/*
===============
SV_GameSendServerCommand

Sends a command string to a client
===============
*/
void SV_GameSendServerCommand( int clientNum, const char *text ) {
	if ( clientNum == -1 ) {
		SV_SendServerCommand( NULL, "%s", text );
	} else {
		if ( clientNum < 0 || clientNum >= sv_maxclients->integer ) {
			return;
		}
		SV_SendServerCommand( svs.clients + clientNum, "%s", text );	
	}
}


/*
===============
SV_GameDropClient

Disconnects the client with a message
===============
*/
void SV_GameDropClient( int clientNum, const char *reason ) {
	if ( clientNum < 0 || clientNum >= sv_maxclients->integer ) {
		return;
	}
	SV_DropClient( svs.clients + clientNum, reason );	
}


/*
=================
SV_SetBrushModel

sets mins and maxs for inline bmodels
=================
*/
void SV_SetBrushModel( sharedEntity_t *ent, const char *name ) {
	clipHandle_t	h;
	vec3_t			mins, maxs;

	if (!name) {
		Com_Error( ERR_DROP, "SV_SetBrushModel: NULL" );
	}

	if (name[0] != '*') {
		Com_Error( ERR_DROP, "SV_SetBrushModel: %s isn't a brush model", name );
	}


	ent->s.modelindex = atoi( name + 1 );

	h = CM_InlineModel( ent->s.modelindex );
	CM_ModelBounds( h, mins, maxs );
	VectorCopy (mins, ent->mins);
	VectorCopy (maxs, ent->maxs);
	ent->bmodel = qtrue;

	ent->contents = -1;		// we don't know exactly what is in the brushes

	SV_LinkEntity( ent );		// FIXME: remove
}



/*
=================
SV_inPVS

Also checks portalareas so that doors block sight
=================
*/
qboolean SV_inPVS (const vec3_t p1, const vec3_t p2)
{
	int		leafnum;
	int		cluster;
	int		area1, area2;
	byte	*mask;

	leafnum = CM_PointLeafnum (p1);
	cluster = CM_LeafCluster (leafnum);
	area1 = CM_LeafArea (leafnum);
	mask = CM_ClusterPVS (cluster);

	leafnum = CM_PointLeafnum (p2);
	cluster = CM_LeafCluster (leafnum);
	area2 = CM_LeafArea (leafnum);
	if ( mask && (!(mask[cluster>>3] & (1<<(cluster&7)) ) ) )
		return qfalse;
	if (!CM_AreasConnected (area1, area2))
		return qfalse;		// a door blocks sight
	return qtrue;
}


/*
=================
SV_inPVSIgnorePortals

Does NOT check portalareas
=================
*/
qboolean SV_inPVSIgnorePortals( const vec3_t p1, const vec3_t p2)
{
	int		leafnum;
	int		cluster;
	byte	*mask;

	leafnum = CM_PointLeafnum (p1);
	cluster = CM_LeafCluster (leafnum);
	mask = CM_ClusterPVS (cluster);

	leafnum = CM_PointLeafnum (p2);
	cluster = CM_LeafCluster (leafnum);

	if ( mask && (!(mask[cluster>>3] & (1<<(cluster&7)) ) ) )
		return qfalse;

	return qtrue;
}


/*
========================
SV_AdjustAreaPortalState
========================
*/
void SV_AdjustAreaPortalState( sharedEntity_t *ent, qboolean open ) {
	svEntity_t	*svEnt;

	svEnt = SV_SvEntityForGentity( ent );
	if ( svEnt->areanum2 == -1 ) {
		return;
	}
	CM_AdjustAreaPortalState( svEnt->areanum, svEnt->areanum2, open );
}


/*
==================
SV_EntityContact
==================
*/
qboolean	SV_EntityContact( vec3_t mins, vec3_t maxs, const sharedEntity_t *gEnt, int capsule ) {
	const float	*origin, *angles;
	clipHandle_t	ch;
	trace_t			trace;

	// check for exact collision
	origin = gEnt->currentOrigin;
	angles = gEnt->currentAngles;

	ch = SV_ClipHandleForEntity( gEnt );
	CM_TransformedBoxTrace ( &trace, vec3_origin, vec3_origin, mins, maxs,
		ch, -1, origin, angles, capsule );

	return trace.startsolid;
}


/*
===============
SV_GetServerinfo

===============
*/
void SV_GetServerinfo( char *buffer, int bufferSize ) {
	if ( bufferSize < 1 ) {
		Com_Error( ERR_DROP, "SV_GetServerinfo: bufferSize == %i", bufferSize );
	}
	Q_strncpyz( buffer, Cvar_InfoString( CVAR_SERVERINFO ), bufferSize );
}

/*
===============
SV_LocateGameData

===============
*/
void SV_LocateGameData( sharedEntity_t *gEnts, int numGEntities, int sizeofGEntity_t,
					   playerState_t *clients, int sizeofGameClient ) {
	sv.gentities = gEnts;
	sv.gentitySize = sizeofGEntity_t;
	sv.num_entities = numGEntities;

	sv.gameClients = clients;
	sv.gameClientSize = sizeofGameClient;
}


/*
===============
SV_GetUsercmd

===============
*/
void SV_GetUsercmd( int clientNum, usercmd_t *cmd ) {
	if ( clientNum < 0 || clientNum >= sv_maxclients->integer ) {
		Com_Error( ERR_DROP, "SV_GetUsercmd: bad clientNum:%i", clientNum );
	}
	*cmd = svs.clients[clientNum].lastUsercmd;
}

//==============================================

/*
===========================================================================

GAME MODULE INTERFACE

The game module is a native library that exchanges two tables of function
pointers with the server: game_import_t (services the server provides) and
game_export_t (entry points the game provides). See g_public.h.

===========================================================================
*/

game_export_t	*ge;

static void			*gameLibrary;
static game_import_t	gameImport;

// debug line storage the game writes into directly
static debugline_t	*sv_debugLines;
static int			sv_numDebugLines;

static int			sv_farPlane;
static qboolean		sv_skyPortal;

#define	GAME_PRINT_BUFFER	4096

static void QDECL SV_GamePrintf( const char *fmt, ... ) {
	va_list		argptr;
	char		text[GAME_PRINT_BUFFER];

	va_start( argptr, fmt );
	Q_vsnprintf( text, sizeof( text ), fmt, argptr );
	va_end( argptr );

	Com_Printf( "%s", text );
}

static void QDECL SV_GameDPrintf( const char *fmt, ... ) {
	va_list		argptr;
	char		text[GAME_PRINT_BUFFER];

	va_start( argptr, fmt );
	Q_vsnprintf( text, sizeof( text ), fmt, argptr );
	va_end( argptr );

	Com_DPrintf( "%s", text );
}

/*
===============
SV_GameError

The game's error levels predate ERR_SERVERDISCONNECT, so its values after
ERR_DROP do not line up with the engine's.
===============
*/
static void QDECL SV_GameError( int errorLevel, const char *fmt, ... ) {
	va_list		argptr;
	char		text[GAME_PRINT_BUFFER];
	int			code;

	va_start( argptr, fmt );
	Q_vsnprintf( text, sizeof( text ), fmt, argptr );
	va_end( argptr );

	switch ( errorLevel ) {
	case 0:		code = ERR_FATAL; break;
	case 1:		code = ERR_DROP; break;
	case 2:		code = ERR_DISCONNECT; break;
	case 3:		code = ERR_NEED_CD; break;
	default:	code = ERR_DROP; break;
	}

	Com_Error( code, "%s", text );
}

static void *SV_GameMalloc( int size ) {
	void	*block;

	if ( size < 0 ) {
		Com_Error( ERR_DROP, "SV_GameMalloc: negative size %i", size );
	}
	// the game expects cleared memory
	block = calloc( 1, size ? size : 1 );
	if ( !block ) {
		Com_Error( ERR_FATAL, "SV_GameMalloc: failed on allocation of %i bytes", size );
	}
	return block;
}

static void SV_GameFree( void *block ) {
	free( block );
}

// game cvar flags that mean the same thing to the engine
#define	GAME_CVAR_SHARED_FLAGS	0x07ff
// "latch until the sound system restarts"; the nearest engine behaviour is a plain latch
#define	GAME_CVAR_SOUND_LATCH	0x1000

static cvar_t *SV_GameCvar( const char *var_name, const char *value, int flags ) {
	int		engineFlags;

	engineFlags = flags & GAME_CVAR_SHARED_FLAGS;
	if ( flags & GAME_CVAR_SOUND_LATCH ) {
		engineFlags |= CVAR_LATCH;
	}
	return Cvar_Get( var_name, value, engineFlags );
}

static void SV_GameCvarSet( const char *var_name, const char *value ) {
	Cvar_Set( var_name, value );
}

static const char *SV_GameArgs( void ) {
	return Cmd_Args();
}

static void SV_GameAddCommand( const char *cmd ) {
	// the game claims the command through ConsoleCommand; registering it only
	// makes it known to completion and stops it being forwarded as chat
	Cmd_AddCommand( cmd, NULL );
}

static int SV_GameReadFile( const char *name, void **buf, qboolean quiet ) {
	(void)quiet;
	return (int)FS_ReadFile( name, buf );
}

static void SV_GameWriteFile( const char *qpath, const void *buffer, int size ) {
	FS_WriteFile( qpath, buffer, size );
}

static char *SV_GamePrepFileWrite( const char *filename ) {
	static char	ospath[MAX_OSPATH];

	Q_strncpyz( ospath, FS_BuildOSPath( Cvar_VariableString( "fs_homepath" ), FS_GetCurrentGameDir(), filename ), sizeof( ospath ) );
	FS_CreatePath( ospath );
	return ospath;
}

static int SV_GameFSRead( void *buffer, int len, fileHandle_t f ) {
	return FS_Read( buffer, len, f );
}

static int SV_GameFSSeek( fileHandle_t f, long offset, int origin ) {
	return FS_Seek( f, offset, origin );
}

static const char *SV_GameArchiveFileName( const char *filename, const char *extension ) {
	static char	name[MAX_QPATH];

	Com_sprintf( name, sizeof( name ), "save/%s.%s", filename, extension );
	return name;
}

static void SV_GameSendConsoleCommand( const char *text ) {
	Cbuf_ExecuteText( EXEC_APPEND, text );
}

static void SV_GameDebugGraph( float value, int color ) {
	(void)value;
	(void)color;
}

static void QDECL SV_GameSendServerCommandf( int clientnum, const char *fmt, ... ) {
	va_list		argptr;
	char		text[MAX_STRING_CHARS];

	va_start( argptr, fmt );
	Q_vsnprintf( text, sizeof( text ), fmt, argptr );
	va_end( argptr );

	SV_GameSendServerCommand( clientnum, text );
}

static char *SV_GameGetConfigstring( int index ) {
	if ( index < 0 || index >= MAX_CONFIGSTRINGS ) {
		Com_Error( ERR_DROP, "SV_GameGetConfigstring: bad index %i", index );
	}
	return sv.configstrings[index];
}

static void SV_GameTrace( trace_t *result, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, int passEntityNum, int contentmask, qboolean cylinder ) {
	SV_Trace( result, start, (float *)mins, (float *)maxs, end, passEntityNum, contentmask, cylinder );
	// the game resolves the entity pointer itself from entityNum
	result->ent = NULL;
}

static int SV_GamePointBrushNum( const vec3_t point, int passEntityNum ) {
	(void)point;
	(void)passEntityNum;
	// TODO: needs a brush-number query in the collision model
	return 0;
}

static qboolean SV_GameInPVS( vec3_t p1, vec3_t p2 ) {
	return SV_inPVS( p1, p2 );
}

static qboolean SV_GameInPVSIgnorePortals( vec3_t p1, vec3_t p2 ) {
	return SV_inPVSIgnorePortals( p1, p2 );
}

static int SV_GameAreaEntities( vec3_t mins, vec3_t maxs, int *list, int maxcount ) {
	return SV_AreaEntities( mins, maxs, list, maxcount );
}

static void SV_GameClipToEntity( trace_t *trace, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, int entityNum, int contentmask ) {
	SV_ClipToEntity( trace, start, (float *)mins, (float *)maxs, end, entityNum, contentmask, qfalse );
	trace->ent = NULL;
}

/*
===============
SV_FindIndex

Config strings double as the name tables the client uses to resolve model,
sound, image and item indexes. Index 0 of each range means "none".
===============
*/
static int SV_FindIndex( const char *name, int start, int max, qboolean create ) {
	int		i;

	if ( !name || !name[0] ) {
		return 0;
	}

	for ( i = 1 ; i < max ; i++ ) {
		const char	*s = sv.configstrings[start + i];

		if ( !s || !s[0] ) {
			break;
		}
		if ( !Q_stricmp( s, name ) ) {
			return i;
		}
	}

	if ( !create ) {
		return 0;
	}
	if ( i == max ) {
		Com_Error( ERR_DROP, "SV_FindIndex: overflow adding '%s' (range %i, max %i)", name, start, max );
	}

	SV_SetConfigstring( start + i, name );
	return i;
}

static int SV_GameImageIndex( const char *name ) {
	return SV_FindIndex( name, CS_IMAGES, MAX_IMAGES, qtrue );
}

static int SV_GameItemIndex( const char *name ) {
	return SV_FindIndex( name, CS_ITEMS, MAX_ITEMS, qtrue );
}

static int SV_GameSoundIndex( const char *name ) {
	return SV_FindIndex( name, CS_SOUNDS, MAX_SOUNDS, qtrue );
}

static int SV_GameModelIndex( const char *name ) {
	return SV_FindIndex( name, CS_MODELS, MAX_MODELS, qtrue );
}

static void SV_GameSetLightStyle( int i, const char *data ) {
	if ( i < 0 || i >= MAX_LIGHTSTYLES ) {
		Com_Error( ERR_DROP, "SV_GameSetLightStyle: bad index %i", i );
	}
	SV_SetConfigstring( CS_LIGHTSTYLES + i, data );
}

static const char *SV_GameDir( void ) {
	return FS_GetCurrentGameDir();
}

static void SV_GameSetModel( gentity_t *ent, const char *name ) {
	if ( !name ) {
		Com_Error( ERR_DROP, "SV_GameSetModel: NULL name" );
	}
	if ( name[0] == '*' ) {
		ent->s.modelindex = SV_GameModelIndex( name );
		SV_SetBrushModel( ent, name );
		return;
	}
	ent->s.modelindex = SV_GameModelIndex( name );
}

static void SV_GameSetFarPlane( int farplane ) {
	sv_farPlane = farplane;
}

static void SV_GameSetSkyPortal( qboolean skyportal ) {
	sv_skyPortal = skyportal;
}

/*
===============
SV_GameCalcCRC

CRC-16/CCITT with an initial value of 0xffff, as used by the Quake engines.
===============
*/
static unsigned short SV_GameCalcCRC( const unsigned char *start, int count ) {
	unsigned short	crc = 0xffff;
	int				i;

	while ( count-- > 0 ) {
		crc ^= (unsigned short)( *start++ << 8 );
		for ( i = 0 ; i < 8 ; i++ ) {
			if ( crc & 0x8000 ) {
				crc = (unsigned short)( ( crc << 1 ) ^ 0x1021 );
			} else {
				crc = (unsigned short)( crc << 1 );
			}
		}
	}
	return crc;
}

/*
===========================================================================

Model, alias and sound queries that are not implemented yet.

Each returns the value that means "nothing found" so the game keeps running,
but models have no animations, tags, bounds or init commands until the TIKI
loader exists. They are kept together so the gap is visible.

===========================================================================
*/

static qboolean SV_Stub_IsModel( int index ) { (void)index; return qfalse; }
static int SV_Stub_ModelCount( int modelindex ) { (void)modelindex; return 0; }
static qboolean SV_Stub_InitCommands( int modelindex, tiki_cmd_t *tiki_cmd ) { (void)modelindex; tiki_cmd->num_cmds = 0; return qfalse; }
static void SV_Stub_CalculateBounds( int modelindex, float scale, vec3_t mins, vec3_t maxs ) { (void)modelindex; (void)scale; VectorClear( mins ); VectorClear( maxs ); }
static const char *SV_Stub_Anim_NameForNum( int modelindex, int animnum ) { (void)modelindex; (void)animnum; return ""; }
static int SV_Stub_Anim_NumForName( int modelindex, const char *name ) { (void)modelindex; (void)name; return -1; }
static int SV_Stub_Anim_NumFrames( int modelindex, int animnum ) { (void)modelindex; (void)animnum; return 0; }
static float SV_Stub_Anim_Time( int modelindex, int animnum ) { (void)modelindex; (void)animnum; return 0; }
static void SV_Stub_Anim_Delta( int modelindex, int animnum, vec3_t delta ) { (void)modelindex; (void)animnum; VectorClear( delta ); }
static int SV_Stub_Anim_Flags( int modelindex, int animnum ) { (void)modelindex; (void)animnum; return 0; }
static qboolean SV_Stub_Anim_HasCommands( int modelindex, int animnum ) { (void)modelindex; (void)animnum; return qfalse; }
static qboolean SV_Stub_Frame_Commands( int modelindex, int animnum, int framenum, tiki_cmd_t *tiki_cmd ) { (void)modelindex; (void)animnum; (void)framenum; tiki_cmd->num_cmds = 0; return qfalse; }
static void SV_Stub_Frame_Delta( int modelindex, int animnum, int framenum, vec3_t delta ) { (void)modelindex; (void)animnum; (void)framenum; VectorClear( delta ); }
static float SV_Stub_Frame_Time( int modelindex, int animnum, int framenum ) { (void)modelindex; (void)animnum; (void)framenum; return 0; }
static void SV_Stub_Frame_Bounds( int modelindex, int animnum, int framenum, float scale, vec3_t mins, vec3_t maxs ) { (void)modelindex; (void)animnum; (void)framenum; (void)scale; VectorClear( mins ); VectorClear( maxs ); }
static int SV_Stub_NameToNum( int modelindex, const char *name ) { (void)modelindex; (void)name; return -1; }
static const char *SV_Stub_NumToName( int modelindex, int num ) { (void)modelindex; (void)num; return ""; }
static int SV_Stub_SurfaceInt( int modelindex, int num ) { (void)modelindex; (void)num; return 0; }

static orientation_t SV_Stub_Tag_Orientation( int modelindex, int anim, int frame, int num, float scale, int *bone_tag, vec4_t *bone_quat ) {
	orientation_t	orient;

	(void)modelindex; (void)anim; (void)frame; (void)num; (void)scale; (void)bone_tag; (void)bone_quat;
	VectorClear( orient.origin );
	AxisClear( orient.axis );
	return orient;
}

static orientation_t SV_Stub_Tag_OrientationEx( int modelindex, int anim, int frame, int num, float scale, int *bone_tag, vec4_t *bone_quat,
	int crossblend_anim, int crossblend_frame, float crossblend_lerp, qboolean uselegs, qboolean usetorso, int torso_anim, int torso_frame,
	int torso_crossblend_anim, int torso_crossblend_frame, float torso_crossblend_lerp ) {
	(void)crossblend_anim; (void)crossblend_frame; (void)crossblend_lerp; (void)uselegs; (void)usetorso; (void)torso_anim; (void)torso_frame;
	(void)torso_crossblend_anim; (void)torso_crossblend_frame; (void)torso_crossblend_lerp;
	return SV_Stub_Tag_Orientation( modelindex, anim, frame, num, scale, bone_tag, bone_quat );
}

static qboolean SV_Stub_Alias_Add( int modelindex, const char *alias, const char *name, const char *parameters ) { (void)modelindex; (void)alias; (void)name; (void)parameters; return qfalse; }
static const char *SV_Stub_Alias_FindRandom( int modelindex, const char *alias ) { (void)modelindex; (void)alias; return NULL; }
static void SV_Stub_Alias_Void( int modelindex ) { (void)modelindex; }
static const char *SV_Stub_Alias_FindDialog( int modelindex, const char *alias, int random, int entity_number ) { (void)modelindex; (void)alias; (void)random; (void)entity_number; return NULL; }
static void *SV_Stub_Alias_GetList( int modelindex ) { (void)modelindex; return NULL; }
static void SV_Stub_Alias_UpdateDialog( int model_index, const char *alias, int number_of_times_played, byte been_played_this_loop, int last_time_played ) { (void)model_index; (void)alias; (void)number_of_times_played; (void)been_played_this_loop; (void)last_time_played; }
static void SV_Stub_Alias_AddActorDialog( int model_index, const char *alias, int actor_number, int number_of_times_played, byte been_played_this_loop, int last_time_played ) { (void)model_index; (void)alias; (void)actor_number; (void)number_of_times_played; (void)been_played_this_loop; (void)last_time_played; }
static const char *SV_Stub_NameForNum( int modelindex ) { (void)modelindex; return ""; }
static qboolean SV_Stub_GlobalAlias_Add( const char *alias, const char *name, const char *parameters ) { (void)alias; (void)name; (void)parameters; return qfalse; }
static const char *SV_Stub_GlobalAlias_FindRandom( const char *alias ) { (void)alias; return NULL; }
static void SV_Stub_GlobalAlias_Void( void ) { }

static void QDECL SV_Stub_CenterPrintf( gentity_t *ent, const char *fmt, ... ) { (void)ent; (void)fmt; }
static void QDECL SV_Stub_LocationPrintf( gentity_t *ent, int x, int y, const char *fmt, ... ) { (void)ent; (void)x; (void)y; (void)fmt; }
static void SV_Stub_Sound( vec3_t *org, int entnum, int channel, const char *sound_name, float volume, float attenuation ) { (void)org; (void)entnum; (void)channel; (void)sound_name; (void)volume; (void)attenuation; }
static void SV_Stub_StopSound( int entnum, int channel ) { (void)entnum; (void)channel; }
static float SV_Stub_SoundLength( const char *path ) { (void)path; return 0; }
static byte *SV_Stub_SoundAmplitudes( const char *name, int *number_of_amplitudes ) { (void)name; *number_of_amplitudes = 0; return NULL; }

//==============================================

static void SV_FillGameImport( game_import_t *import ) {
	Com_Memset( import, 0, sizeof( *import ) );

	import->Printf = SV_GamePrintf;
	import->DPrintf = SV_GameDPrintf;
	import->DebugPrintf = SV_GameDPrintf;
	import->Error = SV_GameError;
	import->Milliseconds = Sys_Milliseconds;
	import->Malloc = SV_GameMalloc;
	import->Free = SV_GameFree;
	import->cvar = SV_GameCvar;
	import->cvar_set = SV_GameCvarSet;
	import->argc = Cmd_Argc;
	import->argv = Cmd_Argv;
	import->args = SV_GameArgs;
	import->AddCommand = SV_GameAddCommand;
	import->FS_ReadFile = SV_GameReadFile;
	import->FS_FreeFile = FS_FreeFile;
	import->FS_WriteFile = SV_GameWriteFile;
	import->FS_FOpenFileWrite = FS_FOpenFileWrite_HomeData;
	import->FS_FOpenFileAppend = FS_FOpenFileAppend_HomeData;
	import->FS_PrepFileWrite = SV_GamePrepFileWrite;
	import->FS_Write = FS_Write;
	import->FS_Read = SV_GameFSRead;
	import->FS_FCloseFile = FS_FCloseFile;
	import->FS_FTell = FS_FTell;
	import->FS_FSeek = SV_GameFSSeek;
	import->FS_Flush = FS_Flush;
	import->GetArchiveFileName = SV_GameArchiveFileName;
	import->SendConsoleCommand = SV_GameSendConsoleCommand;
	import->DebugGraph = SV_GameDebugGraph;

	import->SendServerCommand = SV_GameSendServerCommandf;
	import->setConfigstring = SV_SetConfigstring;
	import->getConfigstring = SV_GameGetConfigstring;
	import->setUserinfo = SV_SetUserinfo;
	import->getUserinfo = SV_GetUserinfo;
	import->SetBrushModel = SV_SetBrushModel;
	import->trace = SV_GameTrace;
	import->pointcontents = SV_PointContents;
	import->pointbrushnum = SV_GamePointBrushNum;
	import->inPVS = SV_GameInPVS;
	import->inPVSIgnorePortals = SV_GameInPVSIgnorePortals;
	import->AdjustAreaPortalState = SV_AdjustAreaPortalState;
	import->AreasConnected = CM_AreasConnected;
	import->linkentity = SV_LinkEntity;
	import->unlinkentity = SV_UnlinkEntity;
	import->AreaEntities = SV_GameAreaEntities;
	import->ClipToEntity = SV_GameClipToEntity;
	import->imageindex = SV_GameImageIndex;
	import->itemindex = SV_GameItemIndex;
	import->soundindex = SV_GameSoundIndex;
	import->modelindex = SV_GameModelIndex;
	import->SetLightStyle = SV_GameSetLightStyle;
	import->GameDir = SV_GameDir;

	import->IsModel = SV_Stub_IsModel;
	import->setmodel = SV_GameSetModel;
	import->NumAnims = SV_Stub_ModelCount;
	import->NumSkins = SV_Stub_ModelCount;
	import->NumSurfaces = SV_Stub_ModelCount;
	import->NumTags = SV_Stub_ModelCount;
	import->InitCommands = SV_Stub_InitCommands;
	import->CalculateBounds = SV_Stub_CalculateBounds;
	import->Anim_NameForNum = SV_Stub_Anim_NameForNum;
	import->Anim_NumForName = SV_Stub_Anim_NumForName;
	import->Anim_Random = SV_Stub_Anim_NumForName;
	import->Anim_NumFrames = SV_Stub_Anim_NumFrames;
	import->Anim_Time = SV_Stub_Anim_Time;
	import->Anim_Delta = SV_Stub_Anim_Delta;
	import->Anim_AbsoluteDelta = SV_Stub_Anim_Delta;
	import->Anim_Flags = SV_Stub_Anim_Flags;
	import->Anim_HasCommands = SV_Stub_Anim_HasCommands;
	import->Frame_Commands = SV_Stub_Frame_Commands;
	import->Frame_Delta = SV_Stub_Frame_Delta;
	import->Frame_Time = SV_Stub_Frame_Time;
	import->Frame_Bounds = SV_Stub_Frame_Bounds;
	import->Surface_NameToNum = SV_Stub_NameToNum;
	import->Surface_NumToName = SV_Stub_NumToName;
	import->Surface_Flags = SV_Stub_SurfaceInt;
	import->Surface_NumSkins = SV_Stub_SurfaceInt;
	import->Tag_NumForName = SV_Stub_NameToNum;
	import->Tag_NameForNum = SV_Stub_NumToName;
	import->Tag_Orientation = SV_Stub_Tag_Orientation;
	import->Tag_OrientationEx = SV_Stub_Tag_OrientationEx;

	import->Alias_Add = SV_Stub_Alias_Add;
	import->Alias_FindRandom = SV_Stub_Alias_FindRandom;
	import->Alias_Dump = SV_Stub_Alias_Void;
	import->Alias_Clear = SV_Stub_Alias_Void;
	import->Alias_FindDialog = SV_Stub_Alias_FindDialog;
	import->Alias_GetList = SV_Stub_Alias_GetList;
	import->Alias_UpdateDialog = SV_Stub_Alias_UpdateDialog;
	import->Alias_AddActorDialog = SV_Stub_Alias_AddActorDialog;
	import->NameForNum = SV_Stub_NameForNum;
	import->GlobalAlias_Add = SV_Stub_GlobalAlias_Add;
	import->GlobalAlias_FindRandom = SV_Stub_GlobalAlias_FindRandom;
	import->GlobalAlias_Dump = SV_Stub_GlobalAlias_Void;
	import->GlobalAlias_Clear = SV_Stub_GlobalAlias_Void;

	import->centerprintf = SV_Stub_CenterPrintf;
	import->locationprintf = SV_Stub_LocationPrintf;
	import->Sound = SV_Stub_Sound;
	import->StopSound = SV_Stub_StopSound;
	import->SoundLength = SV_Stub_SoundLength;
	import->SoundAmplitudes = SV_Stub_SoundAmplitudes;

	import->CalcCRC = SV_GameCalcCRC;
	import->DebugLines = &sv_debugLines;
	import->numDebugLines = &sv_numDebugLines;
	import->LocateGameData = SV_LocateGameData;
	import->SetFarPlane = SV_GameSetFarPlane;
	import->SetSkyPortal = SV_GameSetSkyPortal;
}

/*
===============
SV_LoadGameLibrary

Looks for the game module in the user's game directory, then beside the
executable, then in the installed game, so a locally built module takes
precedence over the retail one.
===============
*/
static void *SV_LoadGameLibrary( char *loadedFrom, int loadedFromSize ) {
	const char	*bases[3];
	const char	*gamedir = FS_GetCurrentGameDir();
	char		name[MAX_QPATH];
	int			i;

	Com_sprintf( name, sizeof( name ), "game" ARCH_STRING DLL_EXT );

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

qboolean SV_GameLoaded( void ) {
	return ge != NULL;
}

/*
===============
SV_ShutdownGameProgs

Called when the server shuts down. The game module stays loaded across map
changes.
===============
*/
void SV_ShutdownGameProgs( void ) {
	if ( !ge ) {
		return;
	}
	ge->Shutdown();
	ge = NULL;

	Sys_UnloadLibrary( gameLibrary );
	gameLibrary = NULL;

	sv.gentities = NULL;
	sv.gameClients = NULL;
	sv.num_entities = 0;
}

/*
===============
SV_InitGameProgs

Loads the game module and initialises it. Called once, when the first map
of a server session starts.
===============
*/
void SV_InitGameProgs( void ) {
	GetGameAPIProc	GetGameAPI;
	char			path[MAX_OSPATH];
	int				i;

	if ( ge ) {
		return;
	}

	gameLibrary = SV_LoadGameLibrary( path, sizeof( path ) );
	if ( !gameLibrary ) {
		Com_Error( ERR_FATAL, "Couldn't load the game module game" ARCH_STRING DLL_EXT );
	}
	Com_Printf( "LoadLibrary (%s)\n", path );

	GetGameAPI = (GetGameAPIProc)Sys_LoadFunction( gameLibrary, "GetGameAPI" );
	if ( !GetGameAPI ) {
		Sys_UnloadLibrary( gameLibrary );
		gameLibrary = NULL;
		Com_Error( ERR_FATAL, "%s does not export GetGameAPI", path );
	}

	SV_FillGameImport( &gameImport );
	ge = GetGameAPI( &gameImport );
	if ( !ge ) {
		Sys_UnloadLibrary( gameLibrary );
		gameLibrary = NULL;
		Com_Error( ERR_FATAL, "%s returned no game interface", path );
	}
	if ( ge->apiversion != GAME_API_VERSION ) {
		int		version = ge->apiversion;

		ge = NULL;
		Sys_UnloadLibrary( gameLibrary );
		gameLibrary = NULL;
		Com_Error( ERR_FATAL, "%s is game API version %i, expected %i", path, version, GAME_API_VERSION );
	}

	// clear all gentity pointers that might still be set from a previous session
	for ( i = 0 ; i < sv_maxclients->integer ; i++ ) {
		svs.clients[i].gentity = NULL;
	}

	// use the current msec count for a random seed
	ge->Init( sv.time, Com_Milliseconds() );
}

/*
====================
SV_GameCommand

See if the current console command is claimed by the game
====================
*/
qboolean SV_GameCommand( void ) {
	if ( sv.state != SS_GAME || !ge ) {
		return qfalse;
	}

	return ge->ConsoleCommand();
}

/*
===========================================================================

Calls into the game on behalf of the rest of the server. Clients are
addressed by number here and translated to the game's entity pointers.

===========================================================================
*/

void SV_GameRunFrame( int frameMsec ) {
	ge->RunFrame( sv.time, frameMsec );
}

const char *SV_GameClientConnect( int clientNum, qboolean firstTime ) {
	return ge->ClientConnect( clientNum, firstTime );
}

void SV_GameClientBegin( int clientNum, usercmd_t *cmd ) {
	ge->ClientBegin( SV_GentityNum( clientNum ), cmd );
}

void SV_GameClientUserinfoChanged( int clientNum, const char *userinfo ) {
	ge->ClientUserinfoChanged( SV_GentityNum( clientNum ), userinfo );
}

void SV_GameClientDisconnect( int clientNum ) {
	ge->ClientDisconnect( SV_GentityNum( clientNum ) );
}

void SV_GameClientCommand( int clientNum ) {
	ge->ClientCommand( SV_GentityNum( clientNum ) );
}

void SV_GameClientThink( int clientNum, usercmd_t *cmd ) {
	ge->ClientThink( SV_GentityNum( clientNum ), cmd );
}
