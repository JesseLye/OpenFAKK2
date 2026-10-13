/*
===========================================================================
A headless test player.

With sv_testplayer set, the server puts a player in client slot 0 when a map
starts and feeds it idle input every frame. There is no client program, no
window and no network traffic: the point is to get the game past everything
that waits for a player (level scripts start with "waitforplayer") and to
exercise the server's client paths, so its behaviour can be compared with the
retail engine's before a real client exists.

  sv_testplayer 1   connect and idle
  sv_testplayer 2   also print the server commands the player is sent

Like a real client, the player connects when the map starts but enters the
world a little later: a client first has to load the level. The delay is
sv_testplayerdelay milliseconds of game time. It matters because scripts
that wait for the player are released when the player enters, and commands
given on the command line after the map run before that.

The player occupies its slot like a bot would in Quake III (address type
NA_BOT), which is what keeps packets from being sent anywhere.
===========================================================================
*/

#include "server.h"

#define	TESTPLAYER_SLOT		0
#define	TESTPLAYER_NAME		"TestPlayer"

static cvar_t	*sv_testplayer;
static cvar_t	*sv_testplayerdelay;
static int		testPlayerEnterTime;

static qboolean SV_TestPlayer_Enabled( void ) {
	if ( !sv_testplayer ) {
		sv_testplayer = Cvar_Get( "sv_testplayer", "0", 0 );
		// 450 ms puts the player's entry at the same level time as the retail
		// client's on the retail maps (dumps taken ten seconds later line up)
		sv_testplayerdelay = Cvar_Get( "sv_testplayerdelay", "450", 0 );
	}
	return sv_testplayer->integer != 0;
}

qboolean SV_IsTestPlayer( const client_t *cl ) {
	return cl == &svs.clients[TESTPLAYER_SLOT]
		&& cl->state != CS_FREE
		&& cl->netchan.remoteAddress.type == NA_BOT;
}

/*
===============
SV_TestPlayer_ServerCommand

Called instead of queueing a reliable command for the test player, which has
nobody to acknowledge it.
===============
*/
void SV_TestPlayer_ServerCommand( const char *cmd ) {
	if ( sv_testplayer && sv_testplayer->integer >= 2 ) {
		Com_Printf( "testplayer <- %s\n", cmd );
	}
}

/*
===============
SV_TestPlayer_Spawn

Called once a map has spawned. Connects the player if it is not already
present; a player carried over from the previous map is reconnected by the
normal map-change path.
===============
*/
void SV_TestPlayer_Spawn( void ) {
	client_t		*cl;
	sharedEntity_t	*ent;
	const char		*denied;

	if ( !SV_TestPlayer_Enabled() ) {
		return;
	}
	if ( sv_maxclients->integer < 1 ) {
		return;
	}

	cl = &svs.clients[TESTPLAYER_SLOT];
	if ( cl->state != CS_FREE ) {
		return;
	}

	Com_Memset( cl, 0, sizeof( *cl ) );
	cl->netchan.remoteAddress.type = NA_BOT;
	cl->rate = 16384;
	cl->snapshotMsec = 1000 / sv_fps->integer;
	cl->lastPacketTime = svs.time;
	cl->lastConnectTime = svs.time;
	Q_strncpyz( cl->name, TESTPLAYER_NAME, sizeof( cl->name ) );
	Q_strncpyz( cl->userinfo, "\\name\\" TESTPLAYER_NAME "\\rate\\25000\\snaps\\20", sizeof( cl->userinfo ) );

	ent = SV_GentityNum( TESTPLAYER_SLOT );
	ent->s.number = TESTPLAYER_SLOT;
	cl->gentity = ent;
	cl->state = CS_CONNECTED;

	denied = SV_GameClientConnect( TESTPLAYER_SLOT, qtrue );
	if ( denied ) {
		Com_Printf( "Test player was refused: %s\n", denied );
		Com_Memset( cl, 0, sizeof( *cl ) );
		return;
	}

	Com_Printf( "Test player connected\n" );
	testPlayerEnterTime = sv.time + ( sv_testplayerdelay->integer > 0 ? sv_testplayerdelay->integer : 0 );
}

/*
===============
SV_TestPlayer_Frame

Called before each game frame: the player presses nothing and looks straight
ahead.
===============
*/
void SV_TestPlayer_Frame( int frameMsec ) {
	client_t	*cl = &svs.clients[TESTPLAYER_SLOT];
	usercmd_t	cmd;

	if ( !svs.clients || !SV_IsTestPlayer( cl ) ) {
		return;
	}

	if ( cl->state == CS_CONNECTED && sv.time >= testPlayerEnterTime ) {
		SV_ClientEnterWorld( cl, NULL );
	}
	if ( cl->state != CS_ACTIVE ) {
		return;
	}

	// stand in for the traffic a real client would generate
	cl->lastPacketTime = svs.time;
	cl->reliableAcknowledge = cl->reliableSequence;

	Com_Memset( &cmd, 0, sizeof( cmd ) );
	cmd.serverTime = sv.time;
	cmd.msec = (byte)( frameMsec > 255 ? 255 : frameMsec );

	SV_ClientThink( cl, &cmd );
}
