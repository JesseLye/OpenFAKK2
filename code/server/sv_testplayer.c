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

For sv_enterstall milliseconds after entering, the player sends no commands
and no snapshots are built for it, as with the retail client, which is still
finishing its own loading then (the server imposes the same gap on a real
client: SV_ClientEnterStalled). It matters because a level script that
freezes the player for a cinematic does so in that gap; a command sent at
once would let the player drop to the floor first.

The player can be given input with the key commands a real client binds
(+forward, +back, +moveleft, +moveright, +moveup, +movedown, +left, +right,
+attackleft, +attackright, +use, +speed, and their "-" forms). A level
script can issue them with stuffcmd at chosen game times, and the same
script drives the retail client, so both can be put through the same moves.
How the retail client turns held keys into a movement command has not been
examined. That +speed means run was confirmed by comparing distance walked;
the other values used here (moves of 127, Quake III's 140 degrees per second
turn rate) are assumptions still to be checked against it.

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

// usercmd_t->buttons bits, as the game module defines them
#define	TP_BUTTON_ATTACKRIGHT	( 1 << 0 )
#define	TP_BUTTON_ATTACKLEFT	( 1 << 1 )
#define	TP_BUTTON_RUN			( 1 << 3 )
#define	TP_BUTTON_USE			( 1 << 5 )
#define	TP_BUTTON_ANY			( 1 << 7 )

#define	TP_MOVE_SPEED			127
#define	TP_YAW_SPEED			140.0f	// degrees per second

typedef enum {
	TPK_FORWARD, TPK_BACK, TPK_MOVELEFT, TPK_MOVERIGHT, TPK_MOVEUP, TPK_MOVEDOWN,
	TPK_LEFT, TPK_RIGHT, TPK_ATTACKLEFT, TPK_ATTACKRIGHT, TPK_USE, TPK_SPEED,
	TPK_COUNT
} testPlayerKey_t;

static const char	*testPlayerKeyNames[TPK_COUNT] = {
	"forward", "back", "moveleft", "moveright", "moveup", "movedown",
	"left", "right", "attackleft", "attackright", "use", "speed"
};

static qboolean	testPlayerKeys[TPK_COUNT];
static float	testPlayerYaw;

/*
===============
SV_TestPlayer_Key_f

Handles every "+key" and "-key" command.
===============
*/
static void SV_TestPlayer_Key_f( void ) {
	const char	*cmd = Cmd_Argv( 0 );
	int			i;

	for ( i = 0 ; i < TPK_COUNT ; i++ ) {
		if ( !Q_stricmp( cmd + 1, testPlayerKeyNames[i] ) ) {
			testPlayerKeys[i] = cmd[0] == '+';
			return;
		}
	}
}

static void SV_TestPlayer_RegisterKeys( void ) {
	static qboolean	registered;
	int				i;

	if ( registered ) {
		return;
	}
	registered = qtrue;
	for ( i = 0 ; i < TPK_COUNT ; i++ ) {
		// command names must outlive the registration
		Cmd_AddCommand( CopyString( va( "+%s", testPlayerKeyNames[i] ) ), SV_TestPlayer_Key_f );
		Cmd_AddCommand( CopyString( va( "-%s", testPlayerKeyNames[i] ) ), SV_TestPlayer_Key_f );
	}
}

static void SV_TestPlayer_BuildCmd( usercmd_t *cmd, int frameMsec ) {
	qboolean	any = qfalse;
	int			i;

	Com_Memset( cmd, 0, sizeof( *cmd ) );
	cmd->serverTime = sv.time;
	cmd->msec = (byte)( frameMsec > 255 ? 255 : frameMsec );

	for ( i = 0 ; i < TPK_COUNT ; i++ ) {
		any |= testPlayerKeys[i];
	}

	testPlayerYaw += ( testPlayerKeys[TPK_LEFT] - testPlayerKeys[TPK_RIGHT] ) * TP_YAW_SPEED * frameMsec * 0.001f;
	cmd->angles[YAW] = (short)ANGLE2SHORT( testPlayerYaw );

	cmd->forwardmove = (signed char)( ( testPlayerKeys[TPK_FORWARD] - testPlayerKeys[TPK_BACK] ) * TP_MOVE_SPEED );
	cmd->rightmove = (signed char)( ( testPlayerKeys[TPK_MOVERIGHT] - testPlayerKeys[TPK_MOVELEFT] ) * TP_MOVE_SPEED );
	cmd->upmove = (signed char)( ( testPlayerKeys[TPK_MOVEUP] - testPlayerKeys[TPK_MOVEDOWN] ) * TP_MOVE_SPEED );

	if ( testPlayerKeys[TPK_ATTACKLEFT] ) {
		cmd->buttons |= TP_BUTTON_ATTACKLEFT;
	}
	if ( testPlayerKeys[TPK_ATTACKRIGHT] ) {
		cmd->buttons |= TP_BUTTON_ATTACKRIGHT;
	}
	if ( testPlayerKeys[TPK_USE] ) {
		cmd->buttons |= TP_BUTTON_USE;
	}
	// the retail client walks unless +speed is held: with +forward alone its
	// player covers 92 units a second, the walk animation's pace
	if ( testPlayerKeys[TPK_SPEED] ) {
		cmd->buttons |= TP_BUTTON_RUN;
	}
	if ( any ) {
		cmd->buttons |= TP_BUTTON_ANY;
	}
}

static qboolean SV_TestPlayer_Enabled( void ) {
	if ( !sv_testplayer ) {
		sv_testplayer = Cvar_Get( "sv_testplayer", "0", 0 );
		// 450 ms puts the player's entry at the same level time as the retail
		// client's on the retail maps (dumps taken ten seconds later line up)
		sv_testplayerdelay = Cvar_Get( "sv_testplayerdelay", "450", 0 );
	}
	return sv_testplayer->integer != 0;
}

/*
===============
SV_TestPlayer_Stalled

True until the player is in the world and past its stall. No snapshots are
built for it before then.
===============
*/
qboolean SV_TestPlayer_Stalled( const client_t *cl ) {
	return cl->state != CS_ACTIVE || SV_ClientEnterStalled( cl );
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

	// A client shows "print" commands on its console. Doing the same keeps the
	// server's log comparable with the retail client's, which is where output
	// meant for the player (such as "whatis") ends up.
	if ( !Q_strncmp( cmd, "print ", 6 ) ) {
		Cmd_TokenizeString( cmd );
		Com_Printf( "%s", Cmd_Argv( 1 ) );
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

	SV_TestPlayer_RegisterKeys();
	Com_Memset( testPlayerKeys, 0, sizeof( testPlayerKeys ) );
	testPlayerYaw = 0;

	Com_Printf( "Test player connected\n" );
	testPlayerEnterTime = sv.time + ( sv_testplayerdelay->integer > 0 ? sv_testplayerdelay->integer : 0 );
}

/*
===============
SV_TestPlayer_Frame

Called before each game frame with whatever keys are currently held.
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

	if ( SV_ClientEnterStalled( cl ) ) {
		return;
	}

	SV_TestPlayer_BuildCmd( &cmd, frameMsec );

	SV_ClientThink( cl, &cmd );
}
