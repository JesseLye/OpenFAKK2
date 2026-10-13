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
#ifndef __TR_PUBLIC_H
#define __TR_PUBLIC_H

#include "tr_types.h"

#define	REF_API_VERSION		9

// FAKK2: what a renderer needs from a .tik model definition. The engine's
// TIKI code reads the definition and fills this in; the renderer loads the
// animation files it names and draws the frames the game asks for.
#define	TIKI_MODEL_MAX_SKINS	4

typedef struct {
	const char	*name;			// the .tik file's name, identifies the model
	float		scale;			// applied to every vertex
	vec3_t		origin;			// added after scaling
	const char	*path;			// directory the model's images are in
	qboolean	skeletal;		// animations are .ska over a .skb skeleton, not .tan files
	const char	*skelFile;		// the .skb file of a skeletal model
	vec3_t		mins, maxs;		// the definition's bounds, as the game sees them
	float		radius;

	int			numAnims;
	const char	**animFiles;	// one per animation

	int			numSurfaces;
	const char	**surfaceNames;
	// numSurfaces * TIKI_MODEL_MAX_SKINS shader names; NULL means the
	// definition hides the surface (skin 0) or has no shader for that skin
	const char	**shaderNames;
} tikiModelDesc_t;

//
// these are the functions exported by the refresh module
//
typedef struct {
	// called before the library is unloaded
	// if the system is just reconfiguring, pass destroyWindow = qfalse,
	// which will keep the screen from flashing to the desktop.
	void	(*Shutdown)( qboolean destroyWindow );

	// All data that will be used in a level should be
	// registered before rendering any frames to prevent disk hits,
	// but they can still be registered at a later time
	// if necessary.
	//
	// BeginRegistration makes any existing media pointers invalid
	// and returns the current gl configuration, including screen width
	// and height, which can be used by the client to intelligently
	// size display elements
	void	(*BeginRegistration)( glconfig_t *config );
	qhandle_t (*RegisterModel)( const char *name );
	// FAKK2: a model from its .tik definition, which the caller has read
	qhandle_t (*RegisterTiki)( const tikiModelDesc_t *desc );
	qhandle_t (*RegisterSkin)( const char *name );
	qhandle_t (*RegisterShader)( const char *name );
	qhandle_t (*RegisterShaderNoMip)( const char *name );
	void	(*LoadWorld)( const char *name );

	// the vis data is a large enough block of data that we go to the trouble
	// of sharing it with the clipmodel subsystem
	void	(*SetWorldVisData)( const byte *vis );

	// EndRegistration will draw a tiny polygon with each texture, forcing
	// them to be loaded into card memory
	void	(*EndRegistration)( void );

	// a scene is built up by calls to R_ClearScene and the various R_Add functions.
	// Nothing is drawn until R_RenderScene is called.
	void	(*ClearScene)( void );
	void	(*AddRefEntityToScene)( const refEntity_t *re );
	// FAKK2: a sprite; the entity's model names the shader and its scale the size
	void	(*AddRefSpriteToScene)( const refEntity_t *re );
	void	(*AddPolyToScene)( qhandle_t hShader , int numVerts, const polyVert_t *verts, int num );
	int		(*LightForPoint)( vec3_t point, vec3_t ambientLight, vec3_t directedLight, vec3_t lightDir );
	// type is a combination of dlighttype_t bits
	void	(*AddLightToScene)( const vec3_t org, float intensity, float r, float g, float b, int type );
	void	(*RenderScene)( const refdef_t *fd );
	// FAKK2: the entity added to the scene being built with that entity
	// number, or NULL. Attached entities are placed from their parent's tags.
	refEntity_t *(*GetRenderEntity)( int entityNumber );

	void	(*SetColor)( const float *rgba );	// NULL = 1,1,1,1
	void	(*DrawStretchPic) ( float x, float y, float w, float h,
		float s1, float t1, float s2, float t2, qhandle_t hShader );	// 0 = white
	// FAKK2: a rectangle in the current color
	void	(*DrawBox)( float x, float y, float w, float h );
	int		(*GetShaderWidth)( qhandle_t hShader );
	int		(*GetShaderHeight)( qhandle_t hShader );
	float	(*ModelRadius)( qhandle_t model );
	float	(*Noise)( float x, float y, float z, float t );
	void	(*DebugLine)( const vec3_t start, const vec3_t end, float r, float g, float b, float alpha );
	// FAKK2 sword swipes: a ribbon through the points given between Begin and End
	void	(*SwipeBegin)( float thistime, float life, qhandle_t shader );
	void	(*SwipePoint)( const vec3_t p1, const vec3_t p2, float time );
	void	(*SwipeEnd)( void );

	// Draw images for cinematic rendering, pass as 32 bit rgba
	void	(*DrawStretchRaw) (int x, int y, int w, int h, int cols, int rows, const byte *data, int client, qboolean dirty);
	void	(*UploadCinematic) (int w, int h, int cols, int rows, const byte *data, int client, qboolean dirty);

	void	(*BeginFrame)( stereoFrame_t stereoFrame );

	// if the pointers are not NULL, timing info will be returned
	void	(*EndFrame)( int *frontEndMsec, int *backEndMsec );


	int		(*MarkFragments)( int numPoints, const vec3_t *points, const vec3_t projection,
				   int maxPoints, vec3_t pointBuffer, int maxFragments, markFragment_t *fragmentBuffer );

	int		(*LerpTag)( orientation_t *tag,  qhandle_t model, int startFrame, int endFrame, 
					 float frac, const char *tagName );
	void	(*ModelBounds)( qhandle_t model, vec3_t mins, vec3_t maxs );

#ifdef __USEA3D
	void    (*A3D_RenderGeometry) (void *pVoidA3D, void *pVoidGeom, void *pVoidMat, void *pVoidGeomStatus);
#endif
	void	(*RegisterFont)(const char *fontName, int pointSize, fontInfo_t *font);
	void	(*RemapShader)(const char *oldShader, const char *newShader, const char *offsetTime);
	qboolean (*GetEntityToken)( char *buffer, int size );
	qboolean (*inPVS)( const vec3_t p1, const vec3_t p2 );

	void (*TakeVideoFrame)( int h, int w, byte* captureBuffer, byte *encodeBuffer, qboolean motionJpeg );
} refexport_t;

//
// these are the functions imported by the refresh module
//
typedef struct {
	// print message on the local console
	void	(QDECL *Printf)( int printLevel, const char *fmt, ...) Q_PRINTF_FUNC(2, 3);

	// abort the game
	void	(QDECL *Error)( int errorLevel, const char *fmt, ...) Q_NO_RETURN Q_PRINTF_FUNC(2, 3);

	// milliseconds should only be used for profiling, never
	// for anything game related.  Get time from the refdef
	int		(*Milliseconds)( void );

	// stack based memory allocation for per-level things that
	// won't be freed
#ifdef HUNK_DEBUG
	void	*(*Hunk_AllocDebug)( int size, ha_pref pref, char *label, char *file, int line );
#else
	void	*(*Hunk_Alloc)( int size, ha_pref pref );
#endif
	void	*(*Hunk_AllocateTempMemory)( int size );
	void	(*Hunk_FreeTempMemory)( void *block );

	// dynamic memory allocator for things that need to be freed
	void	*(*Malloc)( int bytes );
	void	(*Free)( void *buf );

	cvar_t	*(*Cvar_Get)( const char *name, const char *value, int flags );
	void	(*Cvar_Set)( const char *name, const char *value );
	void	(*Cvar_SetValue) (const char *name, float value);
	void	(*Cvar_CheckRange)( cvar_t *cv, float minVal, float maxVal, qboolean shouldBeIntegral );
	void	(*Cvar_SetDescription)( cvar_t *cv, const char *description );

	int		(*Cvar_VariableIntegerValue) (const char *var_name);

	void	(*Cmd_AddCommand)( const char *name, void(*cmd)(void) );
	void	(*Cmd_RemoveCommand)( const char *name );

	int		(*Cmd_Argc) (void);
	char	*(*Cmd_Argv) (int i);

	void	(*Cmd_ExecuteText) (int exec_when, const char *text);

	byte	*(*CM_ClusterPVS)(int cluster);

	// visualization for debugging collision detection
	void	(*CM_DrawDebugSurface)( void (*drawPoly)(int color, int numPoints, float *points) );

	// a -1 return means the file does not exist
	// NULL can be passed for buf to just determine existence
	int		(*FS_FileIsInPAK)( const char *name, int *pCheckSum );
	long		(*FS_ReadFile)( const char *name, void **buf );
	void	(*FS_FreeFile)( void *buf );
	char **	(*FS_ListFiles)( const char *name, const char *extension, int *numfilesfound );
	void	(*FS_FreeFileList)( char **filelist );
	void	(*FS_WriteFile)( const char *qpath, const void *buffer, int size );
	qboolean (*FS_FileExists)( const char *file );

	// cinematic stuff
	void	(*CIN_UploadCinematic)(int handle);
	int		(*CIN_PlayCinematic)( const char *arg0, int xpos, int ypos, int width, int height, int bits);
	e_status (*CIN_RunCinematic) (int handle);

	void	(*CL_WriteAVIVideoFrame)( const byte *buffer, int size );

	// input event handling
	void	(*IN_Init)( void *windowData );
	void	(*IN_Shutdown)( void );
	void	(*IN_Restart)( void );

	// math
	long    (*ftol)(float f);

	// system stuff
	void	(*Sys_SetEnv)( const char *name, const char *value );
	void	(*Sys_GLimpSafeInit)( void );
	void	(*Sys_GLimpInit)( void );
	qboolean (*Sys_LowPhysicalMemory)( void );
} refimport_t;


// this is the only function actually exported at the linker level
// If the module can't init to a valid rendering state, NULL will be
// returned.
#ifdef USE_RENDERER_DLOPEN
typedef	refexport_t* (QDECL *GetRefAPI_t) (int apiVersion, refimport_t * rimp);
#else
refexport_t*GetRefAPI( int apiVersion, refimport_t *rimp );
#endif

#endif	// __TR_PUBLIC_H
