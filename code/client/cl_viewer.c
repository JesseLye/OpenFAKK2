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
  viewsky                             print the sky portal camera's position
  viewsky <x> <y> <z> | off           move the sky portal camera, or turn it off
  viewstop                            leave the viewer

The camera uses the normal movement keys (+forward, +back, +moveleft,
+moveright, +moveup, +movedown, +left, +right, the mouse) and moves three
times as fast while +speed is held.
===========================================================================
*/

#include "client.h"
#include "../tiki/tiki.h"

static qboolean	viewerActive;
static vec3_t	viewerOrigin;
static cvar_t	*cl_viewerSpeed;
static cvar_t	*cl_viewerFov;

// the map's sky portal camera, if it has one
static qboolean	viewerSkyPortal;
static vec3_t	viewerSkyOrigin;

#define	VIEWER_EYE_HEIGHT	82	// above a player start, roughly where the game's camera sits

// Map entities that name a vertex-animated model are drawn where the map puts
// them, standing in the first frame of their idle animation. That is what
// the map says, not what the game would do with them: nothing is hidden,
// moved, animated or spawned by script.
#define	VIEWER_MAX_MODELS	1000

typedef struct {
	qhandle_t	model;
	vec3_t		origin;
	vec3_t		axis[3];
	qboolean	scaled;
} viewerModel_t;

static viewerModel_t	viewerModels[VIEWER_MAX_MODELS];
static int				viewerNumModels;
static int				viewerSkeletal, viewerFailed;

qboolean CL_Viewer_Active( void ) {
	return viewerActive;
}

void CL_Viewer_Stop( void ) {
	viewerActive = qfalse;
}

/*
===============
CL_Viewer_AddModel
===============
*/
static void CL_Viewer_AddModel( const char *model, const vec3_t origin, const vec3_t angles, float scale ) {
	const char		*surfaceNames[64], *shaderNames[64];
	char			name[MAX_QPATH];
	viewerModel_t	*vm;
	tiki_t			*tiki;
	vec3_t			tikiOrigin;
	qhandle_t		handle;
	int				anim, numSurfaces, i;

	if ( viewerNumModels == VIEWER_MAX_MODELS ) {
		return;
	}

	// maps leave the directory out
	if ( Q_stricmpn( model, "models/", 7 ) ) {
		Com_sprintf( name, sizeof( name ), "models/%s", model );
	} else {
		Q_strncpyz( name, model, sizeof( name ) );
	}

	tiki = TIKI_Register( name );
	if ( !tiki || !TIKI_NumAnims( tiki ) ) {
		viewerFailed++;
		return;
	}
	if ( TIKI_IsSkeletal( tiki ) ) {
		viewerSkeletal++;
		return;
	}

	anim = TIKI_Anim_NumForName( tiki, "idle" );
	if ( anim < 0 ) {
		anim = 0;
	}
	numSurfaces = TIKI_NumSurfaces( tiki );
	if ( numSurfaces > ARRAY_LEN( surfaceNames ) ) {
		numSurfaces = ARRAY_LEN( surfaceNames );
	}
	for ( i = 0 ; i < numSurfaces ; i++ ) {
		surfaceNames[i] = TIKI_Surface_NumToName( tiki, i );
		shaderNames[i] = TIKI_Surface_Shader( tiki, i, 0 );
	}
	TIKI_Origin( tiki, tikiOrigin );

	handle = re.RegisterTan( name, TIKI_Anim_File( tiki, anim ), 0, TIKI_Scale( tiki ), tikiOrigin,
		TIKI_Path( tiki ), numSurfaces, surfaceNames, shaderNames );
	if ( !handle ) {
		viewerFailed++;
		return;
	}

	vm = &viewerModels[viewerNumModels++];
	vm->model = handle;
	VectorCopy( origin, vm->origin );
	AnglesToAxis( angles, vm->axis );
	vm->scaled = scale != 1.0f;
	if ( vm->scaled ) {
		for ( i = 0 ; i < 3 ; i++ ) {
			VectorScale( vm->axis[i], scale, vm->axis[i] );
		}
	}
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
	char		script[MAX_TOKEN_CHARS], model[MAX_TOKEN_CHARS];
	vec3_t		angles;
	float		scale;
	qboolean	haveOrigin, haveStart = qfalse;

	VectorClear( viewerOrigin );
	VectorClear( cl.viewangles );
	viewerSkyPortal = qfalse;
	viewerNumModels = viewerSkeletal = viewerFailed = 0;

	while ( re.GetEntityToken( token, sizeof( token ) ) ) {
		if ( token[0] != '{' ) {
			continue;
		}
		classname[0] = 0;
		script[0] = 0;
		model[0] = 0;
		angle = 0;
		scale = 1;
		VectorClear( origin );
		VectorClear( angles );
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
				angles[YAW] = angle;
			} else if ( !Q_stricmp( key, "angles" ) ) {
				sscanf( token, "%f %f %f", &angles[0], &angles[1], &angles[2] );
			} else if ( !Q_stricmp( key, "scale" ) ) {
				scale = atof( token );
			} else if ( !Q_stricmp( key, "model" ) ) {
				Q_strncpyz( model, token, sizeof( model ) );
			} else if ( !Q_stricmp( key, "script" ) ) {
				Q_strncpyz( script, token, sizeof( script ) );
			}
		}
		if ( haveOrigin && !haveStart && !Q_stricmp( classname, "info_player_start" ) ) {
			VectorCopy( origin, viewerOrigin );
			viewerOrigin[2] += VIEWER_EYE_HEIGHT;
			cl.viewangles[YAW] = angle;
			haveStart = qtrue;
		}
		// The game makes an entity the sky portal's camera with the render
		// effect "skyorigin", which maps give in the entity's script key.
		// Only its position is used here; the game can also turn it.
		if ( haveOrigin && !viewerSkyPortal && Q_stristr( script, "skyorigin" ) ) {
			VectorCopy( origin, viewerSkyOrigin );
			viewerSkyPortal = qtrue;
		}
		if ( strlen( model ) > 4 && !Q_stricmp( model + strlen( model ) - 4, ".tik" ) ) {
			CL_Viewer_AddModel( model, origin, angles, scale > 0 ? scale : 1 );
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
	if ( viewerSkyPortal ) {
		Com_Printf( "Sky portal camera at %.0f %.0f %.0f\n", viewerSkyOrigin[0], viewerSkyOrigin[1], viewerSkyOrigin[2] );
	}
	Com_Printf( "%i models placed; not drawn: %i skeletal, %i that failed to load\n",
		viewerNumModels, viewerSkeletal, viewerFailed );
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

static void CL_Viewer_ViewSky_f( void ) {
	int		i;

	if ( !viewerActive ) {
		Com_Printf( "No map is being viewed\n" );
		return;
	}
	if ( Cmd_Argc() == 2 && !Q_stricmp( Cmd_Argv( 1 ), "off" ) ) {
		viewerSkyPortal = qfalse;
		return;
	}
	if ( Cmd_Argc() == 4 ) {
		for ( i = 0 ; i < 3 ; i++ ) {
			viewerSkyOrigin[i] = atof( Cmd_Argv( 1 + i ) );
		}
		viewerSkyPortal = qtrue;
		return;
	}
	if ( viewerSkyPortal ) {
		Com_Printf( "viewsky %.1f %.1f %.1f\n", viewerSkyOrigin[0], viewerSkyOrigin[1], viewerSkyOrigin[2] );
	} else {
		Com_Printf( "No sky portal camera (viewsky <x> <y> <z> sets one)\n" );
	}
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
	Cmd_AddCommand( "viewsky", CL_Viewer_ViewSky_f );
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
	int			i;

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

	rd.sky_portal = viewerSkyPortal;
	VectorCopy( viewerSkyOrigin, rd.sky_origin );
	AxisClear( rd.sky_axis );

	re.ClearScene();
	for ( i = 0 ; i < viewerNumModels ; i++ ) {
		const viewerModel_t	*vm = &viewerModels[i];
		refEntity_t			ent;

		Com_Memset( &ent, 0, sizeof( ent ) );
		ent.reType = RT_MODEL;
		ent.hModel = vm->model;
		VectorCopy( vm->origin, ent.origin );
		VectorCopy( vm->origin, ent.oldorigin );
		VectorCopy( vm->origin, ent.lightingOrigin );
		AxisCopy( vm->axis, ent.axis );
		ent.nonNormalizedAxes = vm->scaled;
		re.AddRefEntityToScene( &ent );
	}
	re.RenderScene( &rd );
}
