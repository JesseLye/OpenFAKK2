/*
===========================================================================
TIKI models on the client.

The engine's TIKI code reads a .tik definition; the renderer draws it. This
file joins the two: it registers a definition with the renderer and keeps,
for every model handle the renderer gave out for a .tik, the definition it
came from, which is what the client game module's TIKI queries work on.
===========================================================================
*/

#include "client.h"
#include "../tiki/tiki.h"

// the renderer's model handles index this
#define	CL_MAX_TIKI_HANDLES	4096

static tiki_t	*cl_tikiForHandle[CL_MAX_TIKI_HANDLES];

/*
===============
CL_ClearTikiHandles

The renderer's handles are only good until it is restarted.
===============
*/
void CL_ClearTikiHandles( void ) {
	Com_Memset( cl_tikiForHandle, 0, sizeof( cl_tikiForHandle ) );
}

/*
===============
CL_RegisterTikiModel

Registers a model definition with the renderer and returns the renderer's
handle for it, or 0. The definition is returned through tiki if asked for.
===============
*/
qhandle_t CL_RegisterTikiModel( const char *name, tiki_t **tikiOut ) {
	static const char	*surfaceNames[MAX_MODEL_SURFACES];
	static const char	*shaderNames[MAX_MODEL_SURFACES * TIKI_MODEL_MAX_SKINS];
	static const char	*animFiles[1024];
	static tiki_cmd_t	init;
	tikiModelDesc_t		desc;
	tiki_t				*tiki;
	qhandle_t			handle;
	int					i, j;

	if ( tikiOut ) {
		*tikiOut = NULL;
	}

	tiki = TIKI_Register( name );
	if ( !tiki ) {
		return 0;
	}

	Com_Memset( &desc, 0, sizeof( desc ) );
	desc.name = TIKI_Name( tiki );
	desc.scale = TIKI_Scale( tiki );
	TIKI_Origin( tiki, desc.origin );
	desc.path = TIKI_Path( tiki );
	desc.skeletal = TIKI_IsSkeletal( tiki );
	TIKI_CalculateBounds( tiki, 1.0f, desc.mins, desc.maxs );
	desc.radius = TIKI_Radius( tiki );

	desc.numAnims = TIKI_NumAnims( tiki );
	if ( desc.numAnims > ARRAY_LEN( animFiles ) ) {
		desc.numAnims = ARRAY_LEN( animFiles );
	}
	for ( i = 0 ; i < desc.numAnims ; i++ ) {
		animFiles[i] = TIKI_Anim_File( tiki, i );
	}
	desc.animFiles = animFiles;

	desc.numSurfaces = TIKI_NumSurfaces( tiki );
	if ( desc.numSurfaces > MAX_MODEL_SURFACES ) {
		desc.numSurfaces = MAX_MODEL_SURFACES;
	}
	for ( i = 0 ; i < desc.numSurfaces ; i++ ) {
		surfaceNames[i] = TIKI_Surface_NumToName( tiki, i );
		for ( j = 0 ; j < TIKI_MODEL_MAX_SKINS ; j++ ) {
			const char	*shader = TIKI_Surface_Shader( tiki, i, j );

			shaderNames[i * TIKI_MODEL_MAX_SKINS + j] = shader && shader[0] ? shader : NULL;
		}
	}

	// A model's own initialisation can hide surfaces ("surface <name>
	// +nodraw"); the sky boxes without ground are made that way. The client
	// game module applies the rest of the commands itself.
	TIKI_InitCommands( tiki, qtrue, &init );
	for ( j = 0 ; j < init.num_cmds ; j++ ) {
		const tiki_singlecmd_t	*cmd = &init.cmds[j];

		if ( cmd->num_args != 3 || Q_stricmp( cmd->args[0], "surface" ) || Q_stricmp( cmd->args[2], "+nodraw" ) ) {
			continue;
		}
		for ( i = 0 ; i < desc.numSurfaces ; i++ ) {
			if ( !Q_stricmp( surfaceNames[i], cmd->args[1] ) || !Q_stricmp( cmd->args[1], "all" ) ) {
				shaderNames[i * TIKI_MODEL_MAX_SKINS] = NULL;
			}
		}
	}
	desc.surfaceNames = surfaceNames;
	desc.shaderNames = shaderNames;

	handle = re.RegisterTiki( &desc );
	if ( handle > 0 && handle < CL_MAX_TIKI_HANDLES ) {
		cl_tikiForHandle[handle] = tiki;
	}
	if ( tikiOut ) {
		*tikiOut = tiki;
	}
	return handle;
}

/*
===============
CL_TikiForHandle

The definition behind a renderer model handle, or NULL when the model is not
a .tik.
===============
*/
tiki_t *CL_TikiForHandle( qhandle_t handle ) {
	if ( handle <= 0 || handle >= CL_MAX_TIKI_HANDLES ) {
		return NULL;
	}
	return cl_tikiForHandle[handle];
}
