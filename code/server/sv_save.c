/*
===========================================================================
Saved games and the player's progress between levels.

As the retail engine keeps them (fakk2.exe 1.02, the commands registered at
0x441fa0). A game is saved as four files, each named by
Com_GetArchiveFileName, "save/<config>_<name>.<extension>":

  .sav  the level, written and read by the game module (WriteLevel, ReadLevel)
  .spv  the player's persistent data (inventory, health, ...) as it was when
        the level was entered: the game's WritePersistant, kept as the
        "current" save's .spv while a level is played and copied
  .ssv  the server's header (below), followed by the area portal state
  .tga  a 256 x 256 picture of the screen, which the client takes (saveshot)

"current" is the save name of the level being played. gamemap, the game's
way of ending a level, writes the player's data to current.spv before the
next level spawns, and the next level reads it back once its entities have
spawned, which is how the player carries her inventory from level to level.
restart (the game's Level::PlayerRestart; a dead player is shown the load
menu instead) spawns the level again and so returns her to the state she
entered it in. map and devmap start afresh: they remove the current save
first.

savegame copies current.spv to the save and has the game write the level,
then writes the header. loadgame reads the header and copies the save's .spv
to current.spv, then spawns its map; the server reads the level instead of
spawning the map's entities (SV_SpawnServer). A level the game cannot read
falls back to spawning the map with the save's persistent data.
loadbrokengame loads a save the game has said it cannot read.
===========================================================================
*/

#include "server.h"

// the .ssv header, 200 bytes, little endian as retail wrote it on x86
// (0x442a00): what the load menu lists
typedef struct {
	int		time;			// time() when saved
	int		mapTime;		// the level clock, milliseconds
	char	comment[64];	// "Starting <level>" for an autosave, else the level's name
	char	mapName[64];	// without its spawn point
	char	saveName[64];
} saveHeader_t;

#define	SAVE_HEADER_SIZE	200
#define	SAVE_SHOT_SIZE		256

#define	SAVE_STAT_HEALTH	0		// playerState_t stats[], the game's STAT_HEALTH

static char	sv_saveName[MAX_QPATH] = SAVE_CURRENT;
static char	sv_loadMapName[MAX_QPATH];
static int	sv_loadMapTime;
static int	sv_autoSaveDelay;		// milliseconds until gamemap's autosave, 0 for none

void SV_ResetSaveName( void ) {
	Q_strncpyz( sv_saveName, SAVE_CURRENT, sizeof( sv_saveName ) );
}

// the level clock of the game being loaded, read from its header
int SV_LoadedMapTime( void ) {
	return sv_loadMapTime;
}

static qboolean SV_BadSaveName( const char *name ) {
	if ( strstr( name, ".." ) || strchr( name, '/' ) || strchr( name, '\\' ) || !name[0] ) {
		Com_Printf( "Bad savedir.\n" );
		return qtrue;
	}
	return qfalse;
}

/*
==================
SV_ArchivePersistantFile

The retail 0x4428f0: the game writes or reads the player's persistent data
in the current save's .spv.
==================
*/
qboolean SV_ArchivePersistantFile( qboolean loading ) {
	const char	*name;

	Com_DPrintf( "SV_ArchivePersistantFile()\n" );
	name = Com_GetArchiveFileName( sv_saveName, "spv" );
	if ( loading ) {
		return SV_GameReadPersistant( name );
	}
	SV_GameWritePersistant( name );
	return qtrue;
}

/*
==================
SV_CopyPersistantFile

The retail 0x442830: copies the persistent data between the current save and
the one being saved or loaded. Retail copies the files in the home directory
and leaves the destination alone when there is no source; here a missing
source removes the destination, so that a game saved after "map" (which has
none) cannot pick up the data of an earlier level.
==================
*/
static void SV_CopyPersistantFile( qboolean toCurrent ) {
	char	current[MAX_QPATH], save[MAX_QPATH];
	void	*buffer;
	long	length;

	Q_strncpyz( current, Com_GetArchiveFileName( SAVE_CURRENT, "spv" ), sizeof( current ) );
	Q_strncpyz( save, Com_GetArchiveFileName( sv_saveName, "spv" ), sizeof( save ) );
	if ( !Q_stricmp( current, save ) ) {
		return;
	}

	length = FS_ReadFile( toCurrent ? save : current, &buffer );
	if ( length < 0 ) {
		FS_Remove_HomeData( toCurrent ? current : save );
		return;
	}
	FS_WriteFile( toCurrent ? current : save, buffer, length );
	FS_FreeFile( buffer );
}

/*
==================
SV_ArchiveLevelFile

The retail 0x442940. Saving, the game writes the level. Loading, it reads it,
and the area portals are set as they were from the end of the .ssv.
==================
*/
qboolean SV_ArchiveLevelFile( qboolean loading ) {
	char			name[MAX_QPATH];
	fileHandle_t	f;
	byte			header[SAVE_HEADER_SIZE];

	Com_DPrintf( "SV_ArchiveLevelFile()\n" );
	Q_strncpyz( name, Com_GetArchiveFileName( sv_saveName, "sav" ), sizeof( name ) );
	if ( !loading ) {
		SV_GameWriteLevel( name, qfalse );
		return qtrue;
	}

	if ( !SV_GameReadLevel( name ) ) {
		return qfalse;
	}
	FS_FOpenFileRead( Com_GetArchiveFileName( sv_saveName, "ssv" ), &f, qtrue );
	if ( f ) {
		FS_Read( header, sizeof( header ), f );
		CM_ReadPortalState( f );
		FS_FCloseFile( f );
	}
	return qtrue;
}

/*
==================
SV_ArchiveServerFile

The retail 0x442a00: the .ssv. Saving writes the header and the area portals
and has the client take the save's picture. Loading reads the header for the
map to spawn and the level clock to resume.
==================
*/
static qboolean SV_ArchiveServerFile( qboolean loading, qboolean autosave ) {
	char			name[MAX_QPATH];
	saveHeader_t	header;
	fileHandle_t	f;

	Com_DPrintf( "SV_ArchiveServerFile(%s)\n", autosave ? "true" : "false" );
	Com_Memset( &header, 0, sizeof( header ) );
	Q_strncpyz( name, Com_GetArchiveFileName( sv_saveName, "ssv" ), sizeof( name ) );

	if ( !loading ) {
		char	mapName[MAX_QPATH];

		f = FS_FOpenFileWrite_HomeData( name );
		if ( !f ) {
			Com_Printf( "Couldn't write %s\n", name );
			return qfalse;
		}
		SV_MapBaseName( sv_mapname->string, mapName, sizeof( mapName ) );
		header.time = LittleLong( (int)time( NULL ) );
		header.mapTime = LittleLong( sv.time );
		Com_sprintf( header.comment, sizeof( header.comment ), autosave ? "Starting %s" : "%s", sv.configstrings[CS_NAME] );
		Q_strncpyz( header.mapName, mapName, sizeof( header.mapName ) );
		Q_strncpyz( header.saveName, sv_saveName, sizeof( header.saveName ) );
		FS_Write( &header, SAVE_HEADER_SIZE, f );
		CM_WritePortalState( f );
		FS_FCloseFile( f );

		if ( !com_dedicated->integer ) {
			Cbuf_ExecuteText( EXEC_NOW, va( "saveshot %s %i %i\n",
				Com_GetArchiveFileName( sv_saveName, "tga" ), SAVE_SHOT_SIZE, SAVE_SHOT_SIZE ) );
		}
		return qtrue;
	}

	FS_FOpenFileRead( name, &f, qtrue );
	if ( !f ) {
		Com_Printf( "Couldn't read %s\n", name );
		return qfalse;
	}
	FS_Read( &header, SAVE_HEADER_SIZE, f );
	FS_FCloseFile( f );
	header.mapName[sizeof( header.mapName ) - 1] = 0;
	Q_strncpyz( sv_loadMapName, header.mapName, sizeof( sv_loadMapName ) );
	sv_loadMapTime = LittleLong( header.mapTime );
	return qtrue;
}

/*
==================
SV_SaveGameName

The retail 0x442e30: the first of <base>0000 to <base>9999 not saved yet.
==================
*/
static void SV_SaveGameName( const char *base, char *out, int outSize ) {
	int		i;

	for ( i = 0 ; i < 10000 ; i++ ) {
		Com_sprintf( out, outSize, "%s%i%i%i%i", base, i / 1000, ( i % 1000 ) / 100, ( i % 100 ) / 10, i % 10 );
		if ( FS_ReadFile( Com_GetArchiveFileName( out, "ssv" ), NULL ) <= 0 ) {
			return;
		}
	}
}

/*
==================
SV_SaveGame

The retail 0x442ee0. Without a name, the game is saved as the map's name and
the next free number.
==================
*/
static void SV_SaveGame( const char *name, qboolean autosave ) {
	char			saveName[MAX_QPATH];
	client_t		*cl;
	playerState_t	*ps;
	char			*s;

	if ( sv.state != SS_GAME ) {
		Com_Printf( "You must be in a game to save.\n" );
		return;
	}
	if ( com_deathmatch->integer ) {
		Com_Printf( "Can't savegame in a deathmatch\n" );
		return;
	}
	cl = svs.clients;
	ps = ( cl && cl->gentity ) ? cl->gentity->client : NULL;
	if ( !ps || !ps->stats[SAVE_STAT_HEALTH] ) {
		Com_Printf( "Can't savegame when dead\n" );
		return;
	}

	if ( !name ) {
		char	base[MAX_QPATH];

		SV_MapBaseName( sv_mapname->string, base, sizeof( base ) );
		for ( s = base ; *s ; s++ ) {
			if ( *s == '/' ) {
				*s = '_';
			}
		}
		SV_SaveGameName( base, saveName, sizeof( saveName ) );
	} else {
		Q_strncpyz( saveName, name, sizeof( saveName ) );
	}
	if ( SV_BadSaveName( saveName ) ) {
		return;
	}

	Com_DPrintf( "Saving game...%s\n", saveName );
	Q_strncpyz( sv_saveName, saveName, sizeof( sv_saveName ) );
	SV_CopyPersistantFile( qfalse );
	SV_ArchiveLevelFile( qfalse );
	SV_ArchiveServerFile( qfalse, autosave );
	Com_Printf( "Done.\n" );
	SV_ResetSaveName();
}

static void SV_SaveGame_f( void ) {
	SV_SaveGame( Cmd_Argc() == 2 ? Cmd_Argv( 1 ) : NULL, qfalse );
}

// queued by gamemap once the new level has started (SV_GameMap_f)
static void SV_AutoSaveGame_f( void ) {
	SV_SaveGame( NULL, qtrue );
}

/*
==================
SV_LoadGame_f

The retail 0x442cb0. A save the game module says it cannot read is offered to
loadbrokengame in a dialog.
==================
*/
static void SV_LoadGame_f( void ) {
	char		name[MAX_QPATH];
	const char	*file;

	if ( sv_maxclients->integer > 1 ) {
		return;
	}
	if ( svs.clients && svs.clients[0].state == CS_PRIMED ) {
		return;
	}
	if ( Cmd_Argc() != 2 ) {
		Com_Printf( "USAGE: loadgame <name>\n" );
		return;
	}
	Q_strncpyz( name, Cmd_Argv( 1 ), sizeof( name ) );
	Com_Printf( "Loading game...%s\n", name );
	if ( SV_BadSaveName( name ) ) {
		return;
	}

	SV_InitGameProgs();

	Q_strncpyz( sv_saveName, name, sizeof( sv_saveName ) );
	file = Com_GetArchiveFileName( name, "sav" );
	if ( FS_ReadFile( file, NULL ) == -1 ) {
		Com_Printf( "Savegame not found.\n" );
		SV_ResetSaveName();
		return;
	}
	if ( !SV_GameLevelArchiveValid( file ) ) {
		// the client's dialog, whose Ok runs loadbrokengame; a dedicated
		// server has no dialog to show
		if ( com_dedicated->integer ) {
			Com_Printf( "This save game is invalid; \"loadbrokengame %s\" restarts its level\n", name );
		} else {
			Cbuf_ExecuteText( EXEC_NOW, va( "dialog \"\" \"\" \"loadbrokengame %s\" 260 150 "
				"textures\\menu\\invalid textures\\menu\\okbutton textures\\menu\\cancelbutton\n", name ) );
		}
		SV_ResetSaveName();
		return;
	}

	if ( SV_ArchiveServerFile( qtrue, qfalse ) ) {
		SV_CopyPersistantFile( qtrue );
		SV_SpawnServer( sv_loadMapName, qfalse, qtrue );
	}
	SV_ResetSaveName();
}

/*
==================
SV_LoadBrokenGame_f

The retail 0x442bf0: loadgame without asking the game whether it can read
the level. SV_SpawnServer still tries; a level it cannot read is spawned
afresh, and the player gets the persistent data saved with it.
==================
*/
static void SV_LoadBrokenGame_f( void ) {
	char	name[MAX_QPATH];

	if ( Cmd_Argc() != 2 ) {
		Com_Printf( "USAGE: loadbrokengame <name>\n" );
		return;
	}
	Q_strncpyz( name, Cmd_Argv( 1 ), sizeof( name ) );
	Com_Printf( "Loading broken game...%s\n", name );
	if ( SV_BadSaveName( name ) ) {
		return;
	}

	SV_InitGameProgs();

	Q_strncpyz( sv_saveName, name, sizeof( sv_saveName ) );
	if ( SV_ArchiveServerFile( qtrue, qfalse ) ) {
		SV_CopyPersistantFile( qtrue );
		SV_SpawnServer( sv_loadMapName, qfalse, qtrue );
	}
	SV_ResetSaveName();
}

/*
==================
SV_GameMap_f

The retail 0x441eb0: the game ends a level with "gamemap <next>". The
player's data goes to the current save, the next level reads it once it has
spawned, and a moment after it starts (unless the server is dedicated) the
game is saved as "Starting <level>". A map name starting with '*' starts
afresh, without the player's data (as in Quake II; retail does not strip the
'*', so its own spawn would fail on such a name).

Retail queues "wait 1500; pause ; autosavegame ; pause". Its wait counts
milliseconds (Cbuf_Execute, 0x42c590, takes the frame's time off it; "wait 1"
is one frame), so retail's level autosaves are made at 1.75 s of level time.
OpenFAKK2's wait counts frames, as Quake III's does, so the server counts the
1.5 s down itself (SV_AutoSaveFrame) and queues the rest; nothing the level
stuffs into the console waits behind it.
==================
*/
static void SV_GameMap_f( void ) {
	char	map[MAX_QPATH];
	char	bspname[MAX_QPATH];

	if ( Cmd_Argc() != 2 ) {
		Com_Printf( "USAGE: gamemap <map>\n" );
		return;
	}
	Com_DPrintf( "SV_GameMap(%s)\n", Cmd_Argv( 1 ) );

	SV_ResetSaveName();
	Q_strncpyz( map, Cmd_Argv( 1 ), sizeof( map ) );
	if ( map[0] == '*' ) {
		Com_WipeSaveGame( SAVE_CURRENT );
		memmove( map, map + 1, strlen( map ) );
	} else if ( sv.state == SS_GAME ) {
		SV_ArchivePersistantFile( qfalse );
	}

	SV_MapBaseName( map, bspname, sizeof( bspname ) );
	if ( FS_ReadFile( va( "maps/%s.bsp", bspname ), NULL ) == -1 ) {
		Com_Printf( "Can't find map maps/%s.bsp\n", bspname );
		return;
	}

	SV_SpawnServer( map, qfalse, qfalse );

	if ( !com_dedicated->integer ) {
		sv_autoSaveDelay = 1500;
	}
}

/*
==================
SV_AutoSaveFrame

Every server frame, paused or not, as retail's wait runs down: gamemap's
autosave once its delay has passed. Another level started in the meantime
cancels it (SV_CancelAutoSave).
==================
*/
void SV_AutoSaveFrame( int msec ) {
	if ( sv_autoSaveDelay <= 0 ) {
		return;
	}
	sv_autoSaveDelay -= msec;
	if ( sv_autoSaveDelay <= 0 ) {
		sv_autoSaveDelay = 0;
		Cbuf_AddText( "pause ; autosavegame ; pause\n" );
	}
}

void SV_CancelAutoSave( void ) {
	sv_autoSaveDelay = 0;
}

void SV_AddSaveCommands( void ) {
	Cmd_AddCommand( "gamemap", SV_GameMap_f );
	Cmd_AddCommand( "savegame", SV_SaveGame_f );
	Cmd_AddCommand( "autosavegame", SV_AutoSaveGame_f );
	Cmd_AddCommand( "loadgame", SV_LoadGame_f );
	Cmd_AddCommand( "loadbrokengame", SV_LoadBrokenGame_f );
}
