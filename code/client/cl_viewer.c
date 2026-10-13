/*
===========================================================================
A map viewer.

"viewmap <name>" loads maps/<name>.bsp into the renderer and lets the camera
fly through it, with no server, game module or user interface involved. It
exists to exercise the renderer on FAKK2 maps before the client game module
runs, and to take screenshots that can be compared with the retail game.

  viewmap <name>                      load a map and start at its player start
  viewpos                             print the camera position
  viewpos <x> <y> <z> [yaw] [pitch]   move the camera
  viewstop                            leave the viewer

The camera uses the normal movement keys (+forward, +back, +moveleft,
+moveright, +moveup, +movedown, +left, +right, the mouse) and moves three
times as fast while +speed is held.
===========================================================================
*/

#include "client.h"

static qboolean	viewerActive;
static vec3_t	viewerOrigin;
static cvar_t	*cl_viewerSpeed;
static cvar_t	*cl_viewerFov;

#define	VIEWER_EYE_HEIGHT	82	// above a player start, roughly where the game's camera sits

qboolean CL_Viewer_Active( void ) {
	return viewerActive;
}

void CL_Viewer_Stop( void ) {
	viewerActive = qfalse;
}

/*
===============
CL_Viewer_FindStart

Puts the camera at the map's first player start, if it has one.
===============
*/
static void CL_Viewer_FindStart( void ) {
	char		token[MAX_TOKEN_CHARS], key[MAX_TOKEN_CHARS];
	char		classname[MAX_TOKEN_CHARS];
	vec3_t		origin;
	float		angle;
	qboolean	haveOrigin;

	VectorClear( viewerOrigin );
	VectorClear( cl.viewangles );

	while ( re.GetEntityToken( token, sizeof( token ) ) ) {
		if ( token[0] != '{' ) {
			continue;
		}
		classname[0] = 0;
		angle = 0;
		haveOrigin = qfalse;
		while ( re.GetEntityToken( key, sizeof( key ) ) && key[0] != '}' ) {
			if ( !re.GetEntityToken( token, sizeof( token ) ) ) {
				break;
			}
			if ( !Q_stricmp( key, "classname" ) ) {
				Q_strncpyz( classname, token, sizeof( classname ) );
			} else if ( !Q_stricmp( key, "origin" ) ) {
				haveOrigin = sscanf( token, "%f %f %f", &origin[0], &origin[1], &origin[2] ) == 3;
			} else if ( !Q_stricmp( key, "angle" ) ) {
				angle = atof( token );
			}
		}
		if ( haveOrigin && !Q_stricmp( classname, "info_player_start" ) ) {
			VectorCopy( origin, viewerOrigin );
			viewerOrigin[2] += VIEWER_EYE_HEIGHT;
			cl.viewangles[YAW] = angle;
			return;
		}
	}
}

static void CL_Viewer_ViewMap_f( void ) {
	char	name[MAX_QPATH];

	if ( Cmd_Argc() != 2 ) {
		Com_Printf( "usage: viewmap <mapname>\n" );
		return;
	}

	Com_sprintf( name, sizeof( name ), "maps/%s.bsp", Cmd_Argv( 1 ) );
	if ( FS_ReadFile( name, NULL ) <= 0 ) {
		Com_Printf( "Can't find map %s\n", name );
		return;
	}

	// nothing else may be using the renderer's world
	SV_Shutdown( "Viewing a map" );
	CL_Disconnect( qtrue );
	CL_FlushMemory();

	re.LoadWorld( name );
	CL_Viewer_FindStart();
	re.EndRegistration();

	viewerActive = qtrue;
	Key_SetCatcher( 0 );
	Con_Close();

	Com_Printf( "Viewing %s from %.0f %.0f %.0f\n", name, viewerOrigin[0], viewerOrigin[1], viewerOrigin[2] );
}

static void CL_Viewer_ViewPos_f( void ) {
	int		i;

	if ( !viewerActive ) {
		Com_Printf( "No map is being viewed\n" );
		return;
	}
	if ( Cmd_Argc() < 4 ) {
		Com_Printf( "viewpos %.1f %.1f %.1f %.1f %.1f\n", viewerOrigin[0], viewerOrigin[1], viewerOrigin[2],
			cl.viewangles[YAW], cl.viewangles[PITCH] );
		return;
	}
	for ( i = 0 ; i < 3 ; i++ ) {
		viewerOrigin[i] = atof( Cmd_Argv( 1 + i ) );
	}
	if ( Cmd_Argc() > 4 ) {
		cl.viewangles[YAW] = atof( Cmd_Argv( 4 ) );
	}
	if ( Cmd_Argc() > 5 ) {
		cl.viewangles[PITCH] = atof( Cmd_Argv( 5 ) );
	}
	cl.viewangles[ROLL] = 0;
}

static void CL_Viewer_ViewStop_f( void ) {
	if ( viewerActive ) {
		CL_Viewer_Stop();
		Key_SetCatcher( KEYCATCH_CONSOLE );
	}
}

void CL_Viewer_Init( void ) {
	cl_viewerSpeed = Cvar_Get( "cl_viewerSpeed", "400", CVAR_ARCHIVE );
	cl_viewerFov = Cvar_Get( "cl_viewerFov", "90", CVAR_ARCHIVE );

	Cmd_AddCommand( "viewmap", CL_Viewer_ViewMap_f );
	Cmd_AddCommand( "viewpos", CL_Viewer_ViewPos_f );
	Cmd_AddCommand( "viewstop", CL_Viewer_ViewStop_f );
}

/*
===============
CL_Viewer_Frame

Moves the camera with whatever movement keys are held.
===============
*/
void CL_Viewer_Frame( void ) {
	usercmd_t	cmd;
	vec3_t		forward, right, up;
	float		scale;

	if ( !viewerActive ) {
		return;
	}

	// reads the keys and the mouse, and turns cl.viewangles
	cmd = CL_CreateFreeCmd();

	if ( cl.viewangles[PITCH] > 89 ) {
		cl.viewangles[PITCH] = 89;
	} else if ( cl.viewangles[PITCH] < -89 ) {
		cl.viewangles[PITCH] = -89;
	}

	scale = cl_viewerSpeed->value * cls.frametime * 0.001f / 127.0f;
	if ( cmd.buttons & BUTTON_RUN ) {
		scale *= 3;
	}

	AngleVectors( cl.viewangles, forward, right, up );
	VectorMA( viewerOrigin, cmd.forwardmove * scale, forward, viewerOrigin );
	VectorMA( viewerOrigin, cmd.rightmove * scale, right, viewerOrigin );
	viewerOrigin[2] += cmd.upmove * scale;
}

void CL_Viewer_Draw( void ) {
	refdef_t	rd;
	float		fov, x;

	if ( !viewerActive ) {
		return;
	}

	Com_Memset( &rd, 0, sizeof( rd ) );
	rd.width = cls.glconfig.vidWidth;
	rd.height = cls.glconfig.vidHeight;

	fov = cl_viewerFov->value;
	if ( fov < 10 || fov > 160 ) {
		fov = 90;
	}
	rd.fov_x = fov;
	x = rd.width / tan( fov / 360 * M_PI );
	rd.fov_y = atan2( rd.height, x ) * 360 / M_PI;

	rd.time = cls.realtime;
	VectorCopy( viewerOrigin, rd.vieworg );
	AnglesToAxis( cl.viewangles, rd.viewaxis );

	// areamask stays clear: every area is drawn

	re.ClearScene();
	re.RenderScene( &rd );
}
