/*
===========================================================================
Game module interface for Heavy Metal: F.A.K.K. 2 1.02.

The layout of every structure in this file is shared with the game module
(gamex86.dll and its rebuilds) and mirrors the interface Ritual published in
the 1.02 SDK. Nothing here may be reordered or resized.
===========================================================================
*/

#ifndef __G_PUBLIC_H__
#define __G_PUBLIC_H__

#define	GAME_API_VERSION	4

// entity->svFlags
// the server does not know how to interpret most of the values
// in entityStates (level eType), so the game must explicitly flag
// special server behaviors
#define	SVF_NOCLIENT			(1<<0)	// don't send entity to clients, even if it has effects
#define	SVF_BOT					(1<<1)
#define	SVF_BROADCAST			(1<<2)	// send to all connected clients
#define	SVF_PORTAL				(1<<3)	// merge a second pvs at origin2 into snapshots
#define	SVF_SENDPVS				(1<<4)	// even though it doesn't have a sound or modelindex, still run it through the pvs
#define	SVF_USE_CURRENT_ORIGIN	(1<<5)	// entity->currentOrigin instead of entity->s.origin
										// for link position (missiles and movers)
#define	SVF_DEADMONSTER			(1<<6)	// treat as CONTENTS_DEADMONSTER for collision
#define	SVF_MONSTER				(1<<7)	// treat as CONTENTS_MONSTER for collision
#define	SVF_USEBBOX				(1<<9)	// do not perform perfect collision use the bbox instead
#define	SVF_ONLYPARENT			(1<<10)	// only send this entity to its parent
#define	SVF_HIDEOWNER			(1<<11)	// hide the owner of the client
#define	SVF_MONSTERCLIP			(1<<12)	// treat as CONTENTS_MONSTERCLIP for collision
#define	SVF_PLAYERCLIP			(1<<13)	// treat as CONTENTS_PLAYERCLIP for collision
#define	SVF_SENDONCE			(1<<14)	// send this entity over the network at least one time
#define	SVF_SENT				(1<<15)	// set when the entity has been sent over at least one time

typedef enum {
	SOLID_NOT,			// no interaction with other objects
	SOLID_TRIGGER,		// only touch when inside, after moving
	SOLID_BBOX,			// touch on edge
	SOLID_BSP			// bsp clip, touch on edge
} solid_t;

//===============================================================

// The server sees only this leading part of each game entity. The game module
// appends its private data after it, which is why entities are always located
// through the size the game reports rather than by array indexing.
typedef struct gentity_s {
	entityState_t	s;				// communicated by server to clients
	struct playerState_s	*client;
	qboolean	inuse;
	qboolean	linked;				// qfalse if not in any good cluster
	int			linkcount;

	int			svFlags;			// SVF_NOCLIENT, SVF_BROADCAST, etc

	qboolean	bmodel;				// if false, assume an explicit mins / maxs bounding box
									// only set by gi.SetBrushModel
	vec3_t		mins, maxs;
	int			contents;			// CONTENTS_TRIGGER, CONTENTS_SOLID, CONTENTS_BODY, etc
									// a non-solid entity should set to 0

	vec3_t		absmin, absmax;		// derived from mins/maxs and origin + rotation

	float		radius;				// radius of object
	vec3_t		centroid;			// centroid, to be used with radius
	int			areanum;			// areanum needs to be seen inside the game as well

	// currentOrigin will be used for all collision detection and world linking.
	// it will not necessarily be the same as the trajectory evaluation for the current
	// time, because each entity must be moved one at a time after time is advanced
	// to avoid simultanious collision issues
	vec3_t		currentOrigin;
	vec3_t		currentAngles;

	// when a trace call is made and passEntityNum != ENTITYNUM_NONE,
	// an ent will be excluded from testing if:
	// ent->s.number == passEntityNum	(don't interact with self)
	// ent->ownerNum == passEntityNum	(don't interact with your own missiles)
	// entity[ent->ownerNum].ownerNum == passEntityNum	(don't interact with other missiles from owner)
	int			ownerNum;

	solid_t		solid;
} gentity_t;

typedef gentity_t sharedEntity_t;

//===============================================================

//
// functions provided by the main engine
//
typedef struct {
	//============== general services ==================

	void	(*Printf)( const char *fmt, ... );
	void	(*DPrintf)( const char *fmt, ... );
	void	(*DebugPrintf)( const char *fmt, ... );
	void	(*Error)( int errorLevel, const char *fmt, ... );

	int		(*Milliseconds)( void );

	void	*(*Malloc)( int size );
	void	(*Free)( void *block );

	cvar_t	*(*cvar)( const char *var_name, const char *value, int flags );
	void	(*cvar_set)( const char *var_name, const char *value );

	int		(*argc)( void );
	char	*(*argv)( int n );
	const char	*(*args)( void );
	void	(*AddCommand)( const char *cmd );

	int		(*FS_ReadFile)( const char *name, void **buf, qboolean quiet );
	void	(*FS_FreeFile)( void *buf );
	void	(*FS_WriteFile)( const char *qpath, const void *buffer, int size );
	fileHandle_t (*FS_FOpenFileWrite)( const char *qpath );
	fileHandle_t (*FS_FOpenFileAppend)( const char *filename );
	char	*(*FS_PrepFileWrite)( const char *filename );
	int		(*FS_Write)( const void *buffer, int len, fileHandle_t f );
	int		(*FS_Read)( void *buffer, int len, fileHandle_t f );
	void	(*FS_FCloseFile)( fileHandle_t f );
	int		(*FS_FTell)( fileHandle_t f );
	int		(*FS_FSeek)( fileHandle_t f, long offset, int origin );
	void	(*FS_Flush)( fileHandle_t f );

	const char	*(*GetArchiveFileName)( const char *filename, const char *extension );

	void	(*SendConsoleCommand)( const char *text );
	void	(*DebugGraph)( float value, int color );

	//=========== server specific functionality =============

	void	(*SendServerCommand)( int clientnum, const char *fmt, ... );

	void	(*setConfigstring)( int index, const char *val );
	char	*(*getConfigstring)( int index );

	void	(*setUserinfo)( int index, const char *val );
	void	(*getUserinfo)( int index, char *buffer, int bufferSize );

	void	(*SetBrushModel)( gentity_t *ent, const char *name );

	void	(*trace)( trace_t *result, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, int passEntityNum, int contentmask, qboolean cylinder );
	int		(*pointcontents)( const vec3_t point, int passEntityNum );
	int		(*pointbrushnum)( const vec3_t point, int passEntityNum );

	qboolean	(*inPVS)( vec3_t p1, vec3_t p2 );
	qboolean	(*inPVSIgnorePortals)( vec3_t p1, vec3_t p2 );

	void	(*AdjustAreaPortalState)( gentity_t *ent, qboolean open );
	qboolean	(*AreasConnected)( int area1, int area2 );

	void	(*linkentity)( gentity_t *ent );
	void	(*unlinkentity)( gentity_t *ent );

	int		(*AreaEntities)( vec3_t mins, vec3_t maxs, int *list, int maxcount );

	void	(*ClipToEntity)( trace_t *trace, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, int entityNum, int contentmask );

	int		(*imageindex)( const char *name );
	int		(*itemindex)( const char *name );
	int		(*soundindex)( const char *name );
	int		(*modelindex)( const char *name );

	void	(*SetLightStyle)( int i, const char *data );

	const char	*(*GameDir)( void );

	//=========== model (TIKI) queries =============

	qboolean	(*IsModel)( int index );
	void	(*setmodel)( gentity_t *ent, const char *name );

	int		(*NumAnims)( int modelindex );
	int		(*NumSkins)( int modelindex );
	int		(*NumSurfaces)( int modelindex );
	int		(*NumTags)( int modelindex );
	qboolean	(*InitCommands)( int modelindex, tiki_cmd_t *tiki_cmd );
	void	(*CalculateBounds)( int modelindex, float scale, vec3_t mins, vec3_t maxs );

	const char	*(*Anim_NameForNum)( int modelindex, int animnum );
	int		(*Anim_NumForName)( int modelindex, const char *name );
	int		(*Anim_Random)( int modelindex, const char *name );
	int		(*Anim_NumFrames)( int modelindex, int animnum );
	float	(*Anim_Time)( int modelindex, int animnum );
	void	(*Anim_Delta)( int modelindex, int animnum, vec3_t delta );
	void	(*Anim_AbsoluteDelta)( int modelindex, int animnum, vec3_t delta );
	int		(*Anim_Flags)( int modelindex, int animnum );
	qboolean	(*Anim_HasCommands)( int modelindex, int animnum );

	qboolean	(*Frame_Commands)( int modelindex, int animnum, int framenum, tiki_cmd_t *tiki_cmd );
	void	(*Frame_Delta)( int modelindex, int animnum, int framenum, vec3_t delta );
	float	(*Frame_Time)( int modelindex, int animnum, int framenum );
	void	(*Frame_Bounds)( int modelindex, int animnum, int framenum, float scale, vec3_t mins, vec3_t maxs );

	int		(*Surface_NameToNum)( int modelindex, const char *name );
	const char	*(*Surface_NumToName)( int modelindex, int num );
	int		(*Surface_Flags)( int modelindex, int num );
	int		(*Surface_NumSkins)( int modelindex, int num );

	int		(*Tag_NumForName)( int modelindex, const char *name );
	const char	*(*Tag_NameForNum)( int modelindex, int num );
	orientation_t	(*Tag_Orientation)( int modelindex, int anim, int frame, int num, float scale, int *bone_tag, vec4_t *bone_quat );
	orientation_t	(*Tag_OrientationEx)( int modelindex, int anim, int frame, int num, float scale, int *bone_tag, vec4_t *bone_quat,
		int crossblend_anim, int crossblend_frame, float crossblend_lerp, qboolean uselegs, qboolean usetorso, int torso_anim, int torso_frame,
		int torso_crossblend_anim, int torso_crossblend_frame, float torso_crossblend_lerp );

	//=========== aliases =============

	qboolean	(*Alias_Add)( int modelindex, const char *alias, const char *name, const char *parameters );
	const char	*(*Alias_FindRandom)( int modelindex, const char *alias );
	void	(*Alias_Dump)( int modelindex );
	void	(*Alias_Clear)( int modelindex );
	const char	*(*Alias_FindDialog)( int modelindex, const char *alias, int random, int entity_number );
	void	*(*Alias_GetList)( int modelindex );
	void	(*Alias_UpdateDialog)( int model_index, const char *alias, int number_of_times_played, byte been_played_this_loop, int last_time_played );
	void	(*Alias_AddActorDialog)( int model_index, const char *alias, int actor_number, int number_of_times_played, byte been_played_this_loop, int last_time_played );

	const char	*(*NameForNum)( int modelindex );

	qboolean	(*GlobalAlias_Add)( const char *alias, const char *name, const char *parameters );
	const char	*(*GlobalAlias_FindRandom)( const char *alias );
	void	(*GlobalAlias_Dump)( void );
	void	(*GlobalAlias_Clear)( void );

	//=========== client output and sound =============

	void	(*centerprintf)( gentity_t *ent, const char *fmt, ... );
	void	(*locationprintf)( gentity_t *ent, int x, int y, const char *fmt, ... );

	void	(*Sound)( vec3_t *org, int entnum, int channel, const char *sound_name, float volume, float attenuation );
	void	(*StopSound)( int entnum, int channel );
	float	(*SoundLength)( const char *path );
	byte	*(*SoundAmplitudes)( const char *name, int *number_of_amplitudes );

	unsigned short	(*CalcCRC)( const unsigned char *start, int count );

	debugline_t	**DebugLines;
	int			*numDebugLines;

	void	(*LocateGameData)( gentity_t *gEnts, int numGEntities, int sizeofGEntity_t,
		playerState_t *clients, int sizeofGameClient );

	void	(*SetFarPlane)( int farplane );
	void	(*SetSkyPortal)( qboolean skyportal );
} game_import_t;

//
// functions exported by the game subsystem
//
typedef struct {
	int		apiversion;

	// the init function will only be called when a game starts,
	// not each time a level is loaded.  Persistant data for clients
	// and the server can be allocated in init
	void	(*Init)( int startTime, int randomSeed );
	void	(*Shutdown)( void );
	void	(*Cleanup)( void );

	// each new level entered will cause a call to SpawnEntities
	void	(*SpawnEntities)( const char *mapname, const char *entstring, int levelTime );

	// return NULL if the client is allowed to connect, otherwise return
	// a text string with the reason for denial
	char	*(*ClientConnect)( int clientNum, qboolean firstTime );

	void	(*ClientBegin)( gentity_t *ent, usercmd_t *cmd );
	void	(*ClientUserinfoChanged)( gentity_t *ent, const char *userinfo );
	void	(*ClientDisconnect)( gentity_t *ent );
	void	(*ClientCommand)( gentity_t *ent );
	void	(*ClientThink)( gentity_t *ent, usercmd_t *cmd );

	void	(*BotBegin)( gentity_t *ent );
	void	(*BotThink)( gentity_t *ent, int msec );

	void	(*PrepFrame)( void );
	void	(*RunFrame)( int levelTime, int frameTime );

	// ConsoleCommand will be called when a command has been issued
	// that is not recognized as a builtin function.
	// The game can issue gi.argc() / gi.argv() commands to get the command
	// and parameters.  Return qfalse if the game doesn't recognize it as a command.
	qboolean	(*ConsoleCommand)( void );

	// persistant data across levels and savegames
	void	(*WritePersistant)( const char *filename );
	qboolean	(*ReadPersistant)( const char *filename );

	void	(*WriteLevel)( const char *filename, qboolean autosave );
	qboolean	(*ReadLevel)( const char *filename );
	qboolean	(*LevelArchiveValid)( const char *filename );

	//
	// global variables shared between game and server
	//
	struct gentity_s	*gentities;
	int			gentitySize;
	int			num_entities;		// current number, <= max_entities
	int			max_entities;

	const char	*error_message;
} game_export_t;

typedef game_export_t *(*GetGameAPIProc)( game_import_t *import );

// reserved config string ranges, shared with the game and client game modules
#define	CS_SOUNDTRACK			8
#define	CS_FOGINFO				9
#define	CS_SKYINFO				10
#define	CS_GAME_VERSION			11
#define	CS_LEVEL_START_TIME		12

#define	CS_MODELS				32
#define	CS_SOUNDS				(CS_MODELS+MAX_MODELS)
#define	CS_IMAGES				(CS_SOUNDS+MAX_SOUNDS)
#define	CS_LIGHTSTYLES			(CS_IMAGES+MAX_IMAGES)
#define	CS_PLAYERS				(CS_LIGHTSTYLES+MAX_LIGHTSTYLES)
#define	CS_ITEMS				(CS_PLAYERS+MAX_CLIENTS)
#define	CS_AMMO					(CS_ITEMS+MAX_ITEMS)
#define	CS_LOCATIONS			(CS_AMMO+MAX_AMMO)
#define	CS_MAX					(CS_LOCATIONS+MAX_LOCATIONS)

#endif // __G_PUBLIC_H__
