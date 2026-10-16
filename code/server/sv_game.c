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
#include "../tiki/tiki.h"
#include "../qcommon/alias.h"

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
/*
=================
SV_SetBrushModel

A brush entity's model index is the inline model's number, as in Quake III
(fakk2.exe 0x444510); no config string names it. The client game module
indexes the inline models with it directly.
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

	// we don't know exactly what is in the brushes, so everything, unless the
	// game has already said (fakk2.exe 0x444510)
	if ( !ent->contents ) {
		ent->contents = -1;
	}

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


// read when snapshots are built
int					sv_farPlaneSquared;
qboolean			sv_skyPortal;

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
// "latch until the sound system restarts"
#define	GAME_CVAR_SOUND_LATCH	0x1000

static cvar_t *SV_GameCvar( const char *var_name, const char *value, int flags ) {
	int		engineFlags;

	engineFlags = flags & GAME_CVAR_SHARED_FLAGS;
	if ( flags & GAME_CVAR_SOUND_LATCH ) {
		engineFlags |= CVAR_LATCH | CVAR_SOUND_LATCH;
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
	int		length = (int)FS_ReadFile( name, buf );

	if ( length < 0 && !quiet ) {
		Com_Printf( "Can't find %s\n", name );
	}
	return length;
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

// the model definition behind each model config string, NULL for inline
// brush models and for names that failed to load
static tiki_t		*sv_tikis[MAX_MODELS];

static qboolean SV_IsTikiName( const char *name ) {
	size_t	len = strlen( name );

	return len > 4 && !Q_stricmp( name + len - 4, ".tik" );
}

static int SV_GameModelIndex( const char *name ) {
	tiki_t	*tiki = NULL;
	int		index;

	if ( !name || !name[0] ) {
		return 0;
	}

	// The config string comes first, so a model definition that fails to
	// load still takes an index (fakk2.exe 0x444fd0); the game gets -1 for
	// it and treats the entity as one that cannot be spawned. Which index
	// a name gets matters: a brush entity's model index is its inline model
	// number, and the game takes it for a definition if one has that index.
	index = SV_FindIndex( name, CS_MODELS, MAX_MODELS, qtrue );
	if ( index <= 0 ) {
		return index;
	}

	if ( SV_IsTikiName( name ) ) {
		tiki = TIKI_Register( name );
	}
	// bound on every call: config strings are reused from level to level
	sv_tikis[index] = tiki;
	if ( SV_IsTikiName( name ) && !tiki ) {
		return -1;
	}
	return index;
}

static tiki_t *SV_Tiki( int modelindex ) {
	if ( modelindex <= 0 || modelindex >= MAX_MODELS ) {
		return NULL;
	}
	return sv_tikis[modelindex];
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

// A brush model name sets only the brush model; any other name gets a config
// string index and, for a model definition, its definition (fakk2.exe
// 0x445050). The game then asks IsModel of the index either way, so a brush
// entity whose number is also a definition's config string index is taken
// for that definition; retail does the same.
static void SV_GameSetModel( gentity_t *ent, const char *name ) {
	if ( !name ) {
		Com_Error( ERR_DROP, "SV_GameSetModel: NULL name" );
	}
	if ( name[0] == '*' ) {
		SV_SetBrushModel( ent, name );
		return;
	}
	ent->s.modelindex = SV_GameModelIndex( name );
	ent->bmodel = qfalse;
}

static void SV_GameSetBrushModel( gentity_t *ent, const char *name ) {
	SV_GameSetModel( ent, name );
}

static void SV_GameSetFarPlane( int farplane ) {
	// kept squared, with a margin, for the distance test (fakk2.exe 0x444740)
	sv_farPlaneSquared = farplane ? ( farplane + 32 ) * ( farplane + 32 ) : 0;
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

Model queries. The game addresses models by config string index.

===========================================================================
*/

static qboolean SV_GameIsModel( int index ) { return SV_Tiki( index ) != NULL; }
static int SV_GameNumAnims( int modelindex ) { return TIKI_NumAnims( SV_Tiki( modelindex ) ); }
static int SV_GameNumSkins( int modelindex ) { return TIKI_NumSkins( SV_Tiki( modelindex ) ); }
static int SV_GameNumSurfaces( int modelindex ) { return TIKI_NumSurfaces( SV_Tiki( modelindex ) ); }
static int SV_GameNumTags( int modelindex ) { return TIKI_NumTags( SV_Tiki( modelindex ) ); }
static qboolean SV_GameInitCommands( int modelindex, tiki_cmd_t *tiki_cmd ) { return TIKI_InitCommands( SV_Tiki( modelindex ), qtrue, tiki_cmd ); }
static void SV_GameCalculateBounds( int modelindex, float scale, vec3_t mins, vec3_t maxs ) { TIKI_CalculateBounds( SV_Tiki( modelindex ), scale, mins, maxs ); }
static const char *SV_GameAnim_NameForNum( int modelindex, int animnum ) { return TIKI_Anim_NameForNum( SV_Tiki( modelindex ), animnum ); }
static int SV_GameAnim_NumForName( int modelindex, const char *name ) { return TIKI_Anim_NumForName( SV_Tiki( modelindex ), name ); }
static int SV_GameAnim_Random( int modelindex, const char *name ) { return TIKI_Anim_Random( SV_Tiki( modelindex ), name ); }
static int SV_GameAnim_NumFrames( int modelindex, int animnum ) { return TIKI_Anim_NumFrames( SV_Tiki( modelindex ), animnum ); }
static float SV_GameAnim_Time( int modelindex, int animnum ) { return TIKI_Anim_Time( SV_Tiki( modelindex ), animnum ); }
static void SV_GameAnim_Delta( int modelindex, int animnum, vec3_t delta ) { TIKI_Anim_Delta( SV_Tiki( modelindex ), animnum, delta ); }
static int SV_GameAnim_Flags( int modelindex, int animnum ) { return TIKI_Anim_Flags( SV_Tiki( modelindex ), animnum ); }
static qboolean SV_GameAnim_HasCommands( int modelindex, int animnum ) { return TIKI_Anim_HasCommands( SV_Tiki( modelindex ), animnum, qtrue ); }
static qboolean SV_GameFrame_Commands( int modelindex, int animnum, int framenum, tiki_cmd_t *tiki_cmd ) { return TIKI_Frame_Commands( SV_Tiki( modelindex ), animnum, framenum, qtrue, tiki_cmd ); }
static void SV_GameFrame_Delta( int modelindex, int animnum, int framenum, vec3_t delta ) { TIKI_Frame_Delta( SV_Tiki( modelindex ), animnum, framenum, delta ); }
static float SV_GameFrame_Time( int modelindex, int animnum, int framenum ) { return TIKI_Frame_Time( SV_Tiki( modelindex ), animnum, framenum ); }
static void SV_GameFrame_Bounds( int modelindex, int animnum, int framenum, float scale, vec3_t mins, vec3_t maxs ) { TIKI_Frame_Bounds( SV_Tiki( modelindex ), animnum, framenum, scale, mins, maxs ); }
static int SV_GameSurface_NameToNum( int modelindex, const char *name ) { return TIKI_Surface_NameToNum( SV_Tiki( modelindex ), name ); }
static const char *SV_GameSurface_NumToName( int modelindex, int num ) { return TIKI_Surface_NumToName( SV_Tiki( modelindex ), num ); }
static int SV_GameSurface_Flags( int modelindex, int num ) { return TIKI_Surface_Flags( SV_Tiki( modelindex ), num ); }
static int SV_GameSurface_NumSkins( int modelindex, int num ) { return TIKI_Surface_NumSkins( SV_Tiki( modelindex ), num ); }
static int SV_GameTag_NumForName( int modelindex, const char *name ) { return TIKI_Tag_NumForName( SV_Tiki( modelindex ), name ); }
static const char *SV_GameTag_NameForNum( int modelindex, int num ) { return TIKI_Tag_NameForNum( SV_Tiki( modelindex ), num ); }

// retail's imports (0x444d00, 0x444da0) pass everything on to the TIKI code
static orientation_t SV_GameTag_Orientation( int modelindex, int anim, int frame, int num, float scale, int *bone_tag, vec4_t *bone_quat ) {
	return TIKI_Tag_Orientation( SV_Tiki( modelindex ), anim, frame, num, scale, bone_tag, bone_quat );
}

static orientation_t SV_GameTag_OrientationEx( int modelindex, int anim, int frame, int num, float scale, int *bone_tag, vec4_t *bone_quat,
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
	return TIKI_Tag_OrientationEx( SV_Tiki( modelindex ), &st, num );
}

static const char *SV_GameNameForNum( int modelindex ) {
	if ( modelindex <= 0 || modelindex >= MAX_MODELS ) {
		return "";
	}
	return sv.configstrings[CS_MODELS + modelindex];
}

/*
===========================================================================

Aliases. Each model has its own list; the game addresses it by model index.

===========================================================================
*/

static AliasList_t *SV_ModelAliases( int modelindex, qboolean create ) {
	tiki_t		*tiki = SV_Tiki( modelindex );
	AliasList_t	**slot = TIKI_AliasList( tiki );

	if ( !slot ) {
		return NULL;
	}
	if ( !*slot && create ) {
		*slot = AliasList_New( TIKI_Name( tiki ) );
	}
	return *slot;
}

static qboolean SV_GameAlias_Add( int modelindex, const char *alias, const char *name, const char *parameters ) {
	return Alias_ListAdd( SV_ModelAliases( modelindex, qtrue ), alias, name, parameters );
}

static const char *SV_GameAlias_FindRandom( int modelindex, const char *alias ) {
	return Alias_ListFindRandom( SV_ModelAliases( modelindex, qfalse ), alias );
}

static void SV_GameAlias_Dump( int modelindex ) {
	Alias_ListDump( SV_ModelAliases( modelindex, qfalse ) );
}

static void SV_GameAlias_Clear( int modelindex ) {
	Alias_ListClear( SV_ModelAliases( modelindex, qfalse ) );
}

static const char *SV_GameAlias_FindDialog( int modelindex, const char *alias, int random, int entity_number ) {
	return Alias_ListFindDialog( SV_ModelAliases( modelindex, qfalse ), alias, random, entity_number );
}

static void *SV_GameAlias_GetList( int modelindex ) {
	return SV_ModelAliases( modelindex, qfalse );
}

static void SV_GameAlias_UpdateDialog( int model_index, const char *alias, int number_of_times_played, byte been_played_this_loop, int last_time_played ) {
	Alias_ListUpdateDialog( SV_ModelAliases( model_index, qfalse ), alias, number_of_times_played, been_played_this_loop, last_time_played );
}

static void SV_GameAlias_AddActorDialog( int model_index, const char *alias, int actor_number, int number_of_times_played, byte been_played_this_loop, int last_time_played ) {
	Alias_ListAddActorDialog( SV_ModelAliases( model_index, qfalse ), alias, actor_number, number_of_times_played, been_played_this_loop, last_time_played );
}

/*
===========================================================================

Sound playback and client prints that are not implemented yet.

===========================================================================
*/


/*
=================
SV_CenterPrintf, SV_LocationPrintf

FAKK2: the retail server (0x4450f0, 0x445190) formats the text, drops it
when it is over 256 characters ("Centerprint text exceeds buffer size")
and keeps it in the client's record for the next message to carry
(svc_centerprint, svc_locprint); SV_SendClientSnapshot writes it.
=================
*/
static client_t *SV_PrintClient( gentity_t *ent ) {
	int		num = ((sharedEntity_t *)ent)->s.number;

	if ( num < 0 || num >= sv_maxclients->integer ) {
		return NULL;
	}
	return &svs.clients[num];
}

static void QDECL SV_CenterPrintf( gentity_t *ent, const char *fmt, ... ) {
	client_t	*cl = SV_PrintClient( ent );
	char		text[1024];
	va_list		argptr;

	if ( !cl ) {
		return;
	}
	va_start( argptr, fmt );
	Q_vsnprintf( text, sizeof( text ), fmt, argptr );
	va_end( argptr );
	if ( strlen( text ) >= sizeof( cl->centerprint ) ) {
		Com_Error( ERR_DROP, "Centerprint text exceeds buffer size\n" );
	}
	Q_strncpyz( cl->centerprint, text, sizeof( cl->centerprint ) );
}

static void QDECL SV_LocationPrintf( gentity_t *ent, int x, int y, const char *fmt, ... ) {
	client_t	*cl = SV_PrintClient( ent );
	char		text[1024];
	va_list		argptr;

	if ( !cl ) {
		return;
	}
	va_start( argptr, fmt );
	Q_vsnprintf( text, sizeof( text ), fmt, argptr );
	va_end( argptr );
	if ( strlen( text ) >= sizeof( cl->locprint ) ) {
		Com_Error( ERR_DROP, "Locationprint text exceeds buffer size\n" );
	}
	Q_strncpyz( cl->locprint, text, sizeof( cl->locprint ) );
	cl->locprintX = x;
	cl->locprintY = y;
}
/*
===========================================================================

Sounds

The game starts a sound by name; it reaches the clients in their next
snapshot as a sound index into the CS_SOUNDS configstrings, and the client
game module plays it from there. Every connected client gets every sound;
whether the retail engine filters them by visibility has not been read
(its import is fakk2.exe 0x448520), and spatialisation is the client's job
anyway.

===========================================================================
*/

static void SV_QueueSound( const server_sound_t *sound ) {
	int			i;

	for ( i = 0 ; i < sv_maxclients->integer ; i++ ) {
		client_t	*client = &svs.clients[i];

		if ( client->state < CS_PRIMED ) {
			continue;
		}
		if ( client->numPendingSounds >= MAX_SERVER_SOUNDS ) {
			Com_DPrintf( "SV_QueueSound: %s has too many sounds queued\n", client->name );
			continue;
		}
		client->pendingSounds[client->numPendingSounds++] = *sound;
	}
}

static void SV_GameSound( vec3_t *org, int entnum, int channel, const char *sound_name, float volume, float attenuation ) {
	server_sound_t	sound;
	int				index;

	if ( !sound_name || !sound_name[0] ) {
		return;
	}
	index = SV_FindIndex( sound_name, CS_SOUNDS, MAX_SOUNDS, qtrue );
	if ( !index ) {
		return;
	}

	Com_Memset( &sound, 0, sizeof( sound ) );
	if ( org ) {
		VectorCopy( *org, sound.origin );
	} else if ( entnum >= 0 && entnum < MAX_GENTITIES ) {
		VectorCopy( SV_GentityNum( entnum )->s.origin, sound.origin );
	}
	sound.entity_number = entnum;
	sound.channel = channel;
	sound.sound_index = index;
	sound.volume = volume;
	sound.min_dist = attenuation;
	SV_QueueSound( &sound );
}

static void SV_GameStopSound( int entnum, int channel ) {
	server_sound_t	sound;

	Com_Memset( &sound, 0, sizeof( sound ) );
	sound.entity_number = entnum;
	sound.channel = channel;
	sound.stop_flag = qtrue;
	SV_QueueSound( &sound );
}

/*
===========================================================================

Lip-sync data

Each dialog sound may have a text file beside it with the extension .lip:
the sound's length in seconds, the number of mouth amplitudes, then one
amplitude (0-255) per line. The game uses the length to time dialog and
the amplitudes to animate mouths.

===========================================================================
*/

#define	LIP_MAX_AMPLITUDES	16384

typedef struct lipSync_s {
	struct lipSync_s	*next;
	char				name[MAX_QPATH];
	qboolean			found;
	float				length;
	int					numAmplitudes;
	byte				*amplitudes;
} lipSync_t;

static lipSync_t	*sv_lipSyncs;

static lipSync_t *SV_LoadLipSync( const char *soundName ) {
	lipSync_t	*lip;
	char		name[MAX_QPATH];
	void		*buffer;
	char		*p, *end;
	int			i;

	if ( !soundName || !soundName[0] ) {
		return NULL;
	}
	COM_StripExtension( soundName, name, sizeof( name ) );
	Q_strcat( name, sizeof( name ), ".lip" );

	for ( lip = sv_lipSyncs ; lip ; lip = lip->next ) {
		if ( !Q_stricmp( lip->name, name ) ) {
			return lip;
		}
	}

	lip = calloc( 1, sizeof( *lip ) );
	if ( !lip ) {
		Com_Error( ERR_FATAL, "SV_LoadLipSync: out of memory" );
	}
	Q_strncpyz( lip->name, name, sizeof( lip->name ) );
	lip->next = sv_lipSyncs;
	sv_lipSyncs = lip;

	FS_ReadFile( name, &buffer );
	if ( !buffer ) {
		return lip;
	}

	p = buffer;
	lip->length = (float)strtod( p, &end );
	if ( end != p ) {
		long	count;

		p = end;
		count = strtol( p, &end, 10 );
		if ( end != p && count >= 0 && count <= LIP_MAX_AMPLITUDES ) {
			lip->amplitudes = calloc( 1, count ? count : 1 );
			if ( !lip->amplitudes ) {
				Com_Error( ERR_FATAL, "SV_LoadLipSync: out of memory" );
			}
			p = end;
			for ( i = 0 ; i < count ; i++ ) {
				long	value = strtol( p, &end, 10 );

				if ( end == p ) {
					break;		// fewer values than declared; the rest stay silent
				}
				lip->amplitudes[i] = (byte)( value < 0 ? 0 : value > 255 ? 255 : value );
				p = end;
			}
			lip->numAmplitudes = (int)count;
			lip->found = qtrue;
		}
	}
	if ( !lip->found ) {
		Com_Printf( "WARNING: %s is not a valid lip-sync file\n", name );
	}

	FS_FreeFile( buffer );
	return lip;
}

// negative when the sound has no lip-sync data
float SV_SoundLength( const char *path ) {
	lipSync_t	*lip = SV_LoadLipSync( path );

	return ( lip && lip->found ) ? lip->length : -1;
}

byte *SV_SoundAmplitudes( const char *name, int *number_of_amplitudes ) {
	lipSync_t	*lip = SV_LoadLipSync( name );

	if ( !lip || !lip->found ) {
		*number_of_amplitudes = 0;
		return NULL;
	}
	*number_of_amplitudes = lip->numAmplitudes;
	return lip->amplitudes;
}

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
	import->GetArchiveFileName = Com_GetArchiveFileName;	// as the retail table has it (0x42eba0)
	import->SendConsoleCommand = SV_GameSendConsoleCommand;
	import->DebugGraph = SV_GameDebugGraph;

	import->SendServerCommand = SV_GameSendServerCommandf;
	import->setConfigstring = SV_SetConfigstring;
	import->getConfigstring = SV_GameGetConfigstring;
	import->setUserinfo = SV_SetUserinfo;
	import->getUserinfo = SV_GetUserinfo;
	import->SetBrushModel = SV_GameSetBrushModel;
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

	import->IsModel = SV_GameIsModel;
	import->setmodel = SV_GameSetModel;
	import->NumAnims = SV_GameNumAnims;
	import->NumSkins = SV_GameNumSkins;
	import->NumSurfaces = SV_GameNumSurfaces;
	import->NumTags = SV_GameNumTags;
	import->InitCommands = SV_GameInitCommands;
	import->CalculateBounds = SV_GameCalculateBounds;
	import->Anim_NameForNum = SV_GameAnim_NameForNum;
	import->Anim_NumForName = SV_GameAnim_NumForName;
	import->Anim_Random = SV_GameAnim_Random;
	import->Anim_NumFrames = SV_GameAnim_NumFrames;
	import->Anim_Time = SV_GameAnim_Time;
	import->Anim_Delta = SV_GameAnim_Delta;
	import->Anim_AbsoluteDelta = SV_GameAnim_Delta;
	import->Anim_Flags = SV_GameAnim_Flags;
	import->Anim_HasCommands = SV_GameAnim_HasCommands;
	import->Frame_Commands = SV_GameFrame_Commands;
	import->Frame_Delta = SV_GameFrame_Delta;
	import->Frame_Time = SV_GameFrame_Time;
	import->Frame_Bounds = SV_GameFrame_Bounds;
	import->Surface_NameToNum = SV_GameSurface_NameToNum;
	import->Surface_NumToName = SV_GameSurface_NumToName;
	import->Surface_Flags = SV_GameSurface_Flags;
	import->Surface_NumSkins = SV_GameSurface_NumSkins;
	import->Tag_NumForName = SV_GameTag_NumForName;
	import->Tag_NameForNum = SV_GameTag_NameForNum;
	import->Tag_Orientation = SV_GameTag_Orientation;
	import->Tag_OrientationEx = SV_GameTag_OrientationEx;

	import->Alias_Add = SV_GameAlias_Add;
	import->Alias_FindRandom = SV_GameAlias_FindRandom;
	import->Alias_Dump = SV_GameAlias_Dump;
	import->Alias_Clear = SV_GameAlias_Clear;
	import->Alias_FindDialog = SV_GameAlias_FindDialog;
	import->Alias_GetList = SV_GameAlias_GetList;
	import->Alias_UpdateDialog = SV_GameAlias_UpdateDialog;
	import->Alias_AddActorDialog = SV_GameAlias_AddActorDialog;
	import->NameForNum = SV_GameNameForNum;
	import->GlobalAlias_Add = Alias_Add;
	import->GlobalAlias_FindRandom = Alias_FindRandom;
	import->GlobalAlias_Dump = Alias_Dump;
	import->GlobalAlias_Clear = Alias_Clear;

	import->centerprintf = SV_CenterPrintf;
	import->locationprintf = SV_LocationPrintf;
	import->Sound = SV_GameSound;
	import->StopSound = SV_GameStopSound;
	import->SoundLength = SV_SoundLength;
	import->SoundAmplitudes = SV_SoundAmplitudes;

	import->CalcCRC = SV_GameCalcCRC;
	import->DebugLines = &com_debugLines;
	import->numDebugLines = &com_numDebugLines;
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

/*
===============
SV_GameResetModelIndexes

Before a level's entities are spawned or read from a saved game, the inline
models take the first model config strings, "*1" at index 1 and so on, as
the retail engine's SV_ClearWorld registers them (fakk2.exe 0x448810). A
brush entity's model index is its inline model number, so this is what keeps
the game from taking a brush entity for a definition with that index; it is
also the index 1 the game assumes for the world and uses for beams.
===============
*/
static void SV_GameResetModelIndexes( void ) {
	int		i;

	Com_Memset( sv_tikis, 0, sizeof( sv_tikis ) );
	for ( i = 1 ; i < CM_NumInlineModels() ; i++ ) {
		SV_FindIndex( va( "*%i", i ), CS_MODELS, MAX_MODELS, qtrue );
	}
}

void SV_GameSpawnEntities( const char *mapname ) {
	SV_GameResetModelIndexes();
	ge->SpawnEntities( mapname, CM_EntityString(), sv.time );
}

/*
===============
SV_GameReadLevel

A saved level replaces spawning the map's entities: the game rebuilds every
entity from the archive, registering its models and sounds as it goes.
===============
*/
qboolean SV_GameReadLevel( const char *filename ) {
	SV_GameResetModelIndexes();
	return ge->ReadLevel( filename );
}

void SV_GameWriteLevel( const char *filename, qboolean autosave ) {
	ge->WriteLevel( filename, autosave );
}

qboolean SV_GameLevelArchiveValid( const char *filename ) {
	return ge->LevelArchiveValid( filename );
}

void SV_GameWritePersistant( const char *filename ) {
	ge->WritePersistant( filename );
}

qboolean SV_GameReadPersistant( const char *filename ) {
	return ge->ReadPersistant( filename );
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

	Com_UnloadModule( gameLibrary );
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

	// clear all gentity pointers that might still be set from a previous session;
	// loadgame can load the module before any server has started
	for ( i = 0 ; svs.clients && i < sv_maxclients->integer ; i++ ) {
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
