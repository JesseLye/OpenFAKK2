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

#include "tr_local.h"

int			r_firstSceneDrawSurf;

int			r_numdlights;
int			r_firstSceneDlight;

int			r_numentities;
int			r_firstSceneEntity;

int			r_numpolys;
int			r_firstScenePoly;

int			r_numpolyverts;


/*
====================
R_InitNextFrame

====================
*/
void R_InitNextFrame( void ) {
	backEndData->commands.used = 0;

	r_firstSceneDrawSurf = 0;

	r_numdlights = 0;
	r_firstSceneDlight = 0;

	r_numentities = 0;
	r_firstSceneEntity = 0;

	r_numpolys = 0;
	r_firstScenePoly = 0;

	r_numpolyverts = 0;
}


/*
====================
RE_ClearScene

====================
*/
void RE_ClearScene( void ) {
	r_firstSceneDlight = r_numdlights;
	r_firstSceneEntity = r_numentities;
	r_firstScenePoly = r_numpolys;
}

/*
===========================================================================

DISCRETE POLYS

===========================================================================
*/

/*
=====================
R_AddPolygonSurfaces

Adds all the scene's polys into this view's drawsurf list
=====================
*/
void R_AddPolygonSurfaces( void ) {
	int			i;
	shader_t	*sh;
	srfPoly_t	*poly;

	tr.currentEntityNum = REFENTITYNUM_WORLD;
	tr.shiftedEntityNum = tr.currentEntityNum << QSORT_REFENTITYNUM_SHIFT;

	for ( i = 0, poly = tr.refdef.polys; i < tr.refdef.numPolys ; i++, poly++ ) {
		// FAKK2 (fakk2.exe 0x47b3c0): the client game module's render flags,
		// a third person poly kept out of the main view as an entity is
		if ( ( poly->renderfx & RF_THIRD_PERSON ) && !tr.viewParms.isPortal ) {
			continue;
		}
		if ( R_ViewLeavesOut( poly->renderfx ) ) {
			continue;
		}
		sh = R_GetShaderByHandle( poly->hShader );
		R_AddDrawSurf( ( void * )poly, sh, poly->fogIndex, qfalse );
	}
}

/*
=====================
RE_AddPolyToScene

=====================
*/
void RE_AddPolyToScene( qhandle_t hShader, int numVerts, const polyVert_t *verts, int numPolys, int renderfx ) {
	srfPoly_t	*poly;
	int			i, j;
	int			fogIndex;
	fog_t		*fog;
	vec3_t		bounds[2];

	if ( !tr.registered ) {
		return;
	}

	if ( !hShader ) {
		ri.Printf( PRINT_WARNING, "WARNING: RE_AddPolyToScene: NULL poly shader\n");
		return;
	}

	for ( j = 0; j < numPolys; j++ ) {
		if ( r_numpolyverts + numVerts > max_polyverts || r_numpolys >= max_polys ) {
      /*
      NOTE TTimo this was initially a PRINT_WARNING
      but it happens a lot with high fighting scenes and particles
      since we don't plan on changing the const and making for room for those effects
      simply cut this message to developer only
      */
			ri.Printf( PRINT_DEVELOPER, "WARNING: RE_AddPolyToScene: r_max_polys or r_max_polyverts reached\n");
			return;
		}

		poly = &backEndData->polys[r_numpolys];
		poly->surfaceType = SF_POLY;
		poly->hShader = hShader;
		poly->renderfx = renderfx;
		poly->numVerts = numVerts;
		poly->verts = &backEndData->polyVerts[r_numpolyverts];
		
		Com_Memcpy( poly->verts, &verts[numVerts*j], numVerts * sizeof( *verts ) );

		if ( glConfig.hardwareType == GLHW_RAGEPRO ) {
			poly->verts->modulate[0] = 255;
			poly->verts->modulate[1] = 255;
			poly->verts->modulate[2] = 255;
			poly->verts->modulate[3] = 255;
		}
		// done.
		r_numpolys++;
		r_numpolyverts += numVerts;

		// if no world is loaded
		if ( tr.world == NULL ) {
			fogIndex = 0;
		}
		// see if it is in a fog volume
		else if ( tr.world->numfogs == 1 ) {
			fogIndex = 0;
		} else {
			// find which fog volume the poly is in
			VectorCopy( poly->verts[0].xyz, bounds[0] );
			VectorCopy( poly->verts[0].xyz, bounds[1] );
			for ( i = 1 ; i < poly->numVerts ; i++ ) {
				AddPointToBounds( poly->verts[i].xyz, bounds[0], bounds[1] );
			}
			for ( fogIndex = 1 ; fogIndex < tr.world->numfogs ; fogIndex++ ) {
				fog = &tr.world->fogs[fogIndex]; 
				if ( bounds[1][0] >= fog->bounds[0][0]
					&& bounds[1][1] >= fog->bounds[0][1]
					&& bounds[1][2] >= fog->bounds[0][2]
					&& bounds[0][0] <= fog->bounds[1][0]
					&& bounds[0][1] <= fog->bounds[1][1]
					&& bounds[0][2] <= fog->bounds[1][2] ) {
					break;
				}
			}
			if ( fogIndex == tr.world->numfogs ) {
				fogIndex = 0;
			}
		}
		poly->fogIndex = fogIndex;
	}
}


//=================================================================================


/*
=====================
RE_AddRefEntityToScene

=====================
*/
void RE_AddRefEntityToScene( const refEntity_t *ent ) {
	if ( !tr.registered ) {
		return;
	}
	if ( r_numentities >= MAX_REFENTITIES ) {
		ri.Printf(PRINT_DEVELOPER, "RE_AddRefEntityToScene: Dropping refEntity, reached MAX_REFENTITIES\n");
		return;
	}
	if ( Q_isnan(ent->origin[0]) || Q_isnan(ent->origin[1]) || Q_isnan(ent->origin[2]) ) {
		static qboolean firstTime = qtrue;
		if (firstTime) {
			firstTime = qfalse;
			ri.Printf( PRINT_WARNING, "RE_AddRefEntityToScene passed a refEntity which has an origin with a NaN component\n");
		}
		return;
	}
	if ( (int)ent->reType < 0 || ent->reType >= RT_MAX_REF_ENTITY_TYPE ) {
		ri.Error( ERR_DROP, "RE_AddRefEntityToScene: bad reType %i", ent->reType );
	}

	backEndData->entities[r_numentities].e = *ent;
	backEndData->entities[r_numentities].lightingCalculated = qfalse;
	backEndData->entities[r_numentities].lod = 1;
	backEndData->entities[r_numentities].portalLod = 1;
	backEndData->entities[r_numentities].lightCubeSet = qfalse;

	r_numentities++;
}

/*
=====================
RE_AddRefSpriteToScene

FAKK2: the entity's model is a .spr, which names the sprite's shader. The
model type sorts it out when the scene is drawn.
=====================
*/
void RE_AddRefSpriteToScene( const refEntity_t *ent ) {
	RE_AddRefEntityToScene( ent );
}

/*
=====================
RE_GetRenderEntity

FAKK2: the entity with that number among those added since the scene was
cleared. The client game module places attached entities from the tags of
the parent it already added.
=====================
*/
refEntity_t *RE_GetRenderEntity( int entityNumber ) {
	int		i;

	for ( i = r_firstSceneEntity ; i < r_numentities ; i++ ) {
		if ( backEndData->entities[i].e.entityNumber == entityNumber ) {
			return &backEndData->entities[i].e;
		}
	}
	return NULL;
}

/*
=====================
RE_DebugLine

FAKK2: the client game module's debug lines (fakk2.exe 0x461fb0) go into the
game module's own array, solid and one pixel wide, up to g_numdebuglines;
R_DrawDebugLines draws them with the game's.
=====================
*/
void RE_DebugLine( const vec3_t start, const vec3_t end, float r, float g, float b, float alpha ) {
	debugline_t	*line;

	if ( !ri.DebugLines || !ri.numDebugLines || !*ri.DebugLines ) {
		return;
	}
	if ( *ri.numDebugLines >= g_numdebuglines->integer ) {
		ri.Printf( PRINT_ALL, "R_DebugLine: Exceeded MAX_DEBUG_LINES\n" );
		return;
	}

	line = &( *ri.DebugLines )[ ( *ri.numDebugLines )++ ];
	VectorCopy( start, line->start );
	VectorCopy( end, line->end );
	VectorSet( line->color, r, g, b );
	line->alpha = alpha;
	line->width = 1;
	line->factor = 1;
	line->pattern = 0xffff;
}

/*
=====================
RE_SwipeBegin, RE_SwipePoint, RE_SwipeEnd

FAKK2: the client game module's swipes (fakk2.exe 0x480eb0, 0x480f50; the
end call does nothing). The first swipe begun in a frame clears the ones
from earlier frames, so a swipe that is no longer handed over stays and
fades out (R_AddSwipeSurfaces) until a new one is begun. Past 32 swipes in
a frame a new one is ignored and its edges go to the last.
=====================
*/
static srfSwipe_t	r_swipes[MAX_SWIPES];
static int			r_numSwipes;
static int			r_swipeFrame = -1;

void RE_SwipeBegin( float thistime, float life, qhandle_t shader ) {
	srfSwipe_t	*swipe;

	if ( tr.frameCount != r_swipeFrame ) {
		r_numSwipes = 0;
	} else if ( r_numSwipes >= MAX_SWIPES ) {
		return;
	}

	swipe = &r_swipes[r_numSwipes++];
	swipe->surfaceType = SF_SWIPE;
	swipe->life = life;
	swipe->thistime = thistime;
	swipe->lastDrawTime = 0;
	swipe->shader = shader;
	swipe->numPoints = 0;

	r_swipeFrame = tr.frameCount;
}

void RE_SwipePoint( const vec3_t p1, const vec3_t p2, float time ) {
	srfSwipe_t		*swipe;
	swipePoint_t	*point;

	if ( !r_numSwipes ) {
		return;
	}
	swipe = &r_swipes[r_numSwipes - 1];
	if ( swipe->numPoints >= MAX_SWIPE_POINTS ) {
		return;
	}
	point = &swipe->points[swipe->numPoints++];
	VectorCopy( p1, point->point1 );
	VectorCopy( p2, point->point2 );
	point->time = time;
}

void RE_SwipeEnd( void ) {
}

/*
=====================
R_AddSwipeSurfaces

FAKK2: adds the swipes that still have edges to the view, as world surfaces
with no fog (fakk2.exe 0x480fd0). A swipe drawn again in a later frame
without being handed over anew is aged by the time that has passed, which
fades it out.
=====================
*/
void R_AddSwipeSurfaces( void ) {
	int		i;

	tr.currentEntityNum = REFENTITYNUM_WORLD;
	tr.shiftedEntityNum = tr.currentEntityNum << QSORT_REFENTITYNUM_SHIFT;

	for ( i = 0 ; i < r_numSwipes ; i++ ) {
		srfSwipe_t	*swipe = &r_swipes[i];
		int			elapsed = 0;

		if ( !swipe->numPoints ) {
			continue;
		}
		if ( swipe->lastDrawTime ) {
			elapsed = tr.refdef.time - swipe->lastDrawTime;
		}
		swipe->lastDrawTime = tr.refdef.time;
		if ( elapsed ) {
			swipe->thistime += elapsed;
		}
		R_AddDrawSurf( &swipe->surfaceType, R_GetShaderByHandle( swipe->shader ), 0, 0 );
	}
}


/*
=====================
RE_AddDynamicLightToScene

=====================
*/
static void RE_AddDynamicLightToScene( const vec3_t org, float intensity, float r, float g, float b, int additive ) {
	dlight_t	*dl;

	if ( !tr.registered ) {
		return;
	}
	if ( r_numdlights >= MAX_DLIGHTS ) {
		return;
	}
	if ( intensity <= 0 ) {
		return;
	}
	// these cards don't have the correct blend mode
	if ( glConfig.hardwareType == GLHW_RIVA128 || glConfig.hardwareType == GLHW_PERMEDIA2 ) {
		return;
	}
	dl = &backEndData->dlights[r_numdlights++];
	VectorCopy (org, dl->origin);
	dl->radius = intensity;
	dl->color[0] = r;
	dl->color[1] = g;
	dl->color[2] = b;
	dl->additive = additive;
	dl->flags = 0;
}

/*
=====================
RE_AddLightToScene

FAKK2's type bits: "additive" selects an additive light; the lensflare
and viewlensflare bits give the light a lens flare (tr_lensflare.c).
=====================
*/
void RE_AddLightToScene( const vec3_t org, float intensity, float r, float g, float b, int type ) {
	int		n = r_numdlights;

	RE_AddDynamicLightToScene( org, intensity, r, g, b, ( type & additive ) != 0 );
	if ( r_numdlights > n ) {
		backEndData->dlights[n].flags = type;
	}
}

/*
@@@@@@@@@@@@@@@@@@@@@
RE_RenderScene

Draw a 3D view into a part of the window, then return
to 2D drawing.

Rendering a scene may require multiple views to be rendered
to handle mirrors,
@@@@@@@@@@@@@@@@@@@@@
*/
void RE_RenderScene( const refdef_t *fd ) {
	viewParms_t		parms;
	int				startTime;

	if ( !tr.registered ) {
		return;
	}
	GLimp_LogComment( "====== RE_RenderScene =====\n" );

	if ( r_norefresh->integer ) {
		return;
	}

	startTime = ri.Milliseconds();

	if (!tr.world && !( fd->rdflags & RDF_NOWORLDMODEL ) ) {
		ri.Error (ERR_DROP, "R_RenderScene: NULL worldmodel");
	}

	Com_Memcpy( tr.refdef.text, fd->text, sizeof( tr.refdef.text ) );

	tr.refdef.x = fd->x;
	tr.refdef.y = fd->y;
	tr.refdef.width = fd->width;
	tr.refdef.height = fd->height;
	tr.refdef.fov_x = fd->fov_x;
	tr.refdef.fov_y = fd->fov_y;

	VectorCopy( fd->vieworg, tr.refdef.vieworg );
	VectorCopy( fd->viewaxis[0], tr.refdef.viewaxis[0] );
	VectorCopy( fd->viewaxis[1], tr.refdef.viewaxis[1] );
	VectorCopy( fd->viewaxis[2], tr.refdef.viewaxis[2] );

	tr.refdef.time = fd->time;
	tr.refdef.rdflags = fd->rdflags;

	tr.refdef.sky_portal = fd->sky_portal;
	tr.refdef.sky_alpha = fd->sky_alpha;
	VectorCopy( fd->sky_origin, tr.refdef.sky_origin );
	AxisCopy( fd->sky_axis, tr.refdef.sky_axis );

	tr.refdef.farplane_distance = fd->farplane_distance;
	VectorCopy( fd->farplane_color, tr.refdef.farplane_color );
	tr.refdef.farplane_cull = fd->farplane_cull;

	// copy the areamask data over and note if it has changed, which
	// will force a reset of the visible leafs even if the view hasn't moved
	tr.refdef.areamaskModified = qfalse;
	if ( ! (tr.refdef.rdflags & RDF_NOWORLDMODEL) ) {
		int		areaDiff;
		int		i;

		// compare the area bits
		areaDiff = 0;
		for (i = 0 ; i < MAX_MAP_AREA_BYTES/4 ; i++) {
			areaDiff |= ((int *)tr.refdef.areamask)[i] ^ ((int *)fd->areamask)[i];
			((int *)tr.refdef.areamask)[i] = ((int *)fd->areamask)[i];
		}

		if ( areaDiff ) {
			// a door just opened or something
			tr.refdef.areamaskModified = qtrue;
		}
	}


	// derived info

	tr.refdef.floatTime = tr.refdef.time * 0.001;

	tr.refdef.numDrawSurfs = r_firstSceneDrawSurf;
	tr.refdef.drawSurfs = backEndData->drawSurfs;

	tr.refdef.num_entities = r_numentities - r_firstSceneEntity;
	tr.refdef.entities = &backEndData->entities[r_firstSceneEntity];

	tr.refdef.num_dlights = r_numdlights - r_firstSceneDlight;
	tr.refdef.dlights = &backEndData->dlights[r_firstSceneDlight];

	tr.refdef.numPolys = r_numpolys - r_firstScenePoly;
	tr.refdef.polys = &backEndData->polys[r_firstScenePoly];

	// turn off dynamic lighting globally by clearing all the
	// dlights if it needs to be disabled or if vertex lighting is enabled
	if ( r_dynamiclight->integer == 0 ||
		 r_vertexLight->integer == 1 ||
		 glConfig.hardwareType == GLHW_PERMEDIA2 ) {
		tr.refdef.num_dlights = 0;
	}

	// a single frame may have multiple scenes draw inside it --
	// a 3D game view, 3D status bar renderings, 3D menus, etc.
	// They need to be distinguished by the light flare code, because
	// the visibility state for a given surface may be different in
	// each scene / view.
	tr.frameSceneNum++;
	tr.sceneCount++;

	// setup view parms for the initial view
	//
	// set up viewport
	// The refdef takes 0-at-the-top y coordinates, so
	// convert to GL's 0-at-the-bottom space
	//
	Com_Memset( &parms, 0, sizeof( parms ) );
	parms.viewportX = tr.refdef.x;
	parms.viewportY = glConfig.vidHeight - ( tr.refdef.y + tr.refdef.height );
	parms.viewportWidth = tr.refdef.width;
	parms.viewportHeight = tr.refdef.height;
	parms.isPortal = qfalse;

	parms.fovX = tr.refdef.fov_x;
	parms.fovY = tr.refdef.fov_y;
	
	parms.stereoFrame = tr.refdef.stereoFrame;

	VectorCopy( fd->vieworg, parms.or.origin );
	VectorCopy( fd->viewaxis[0], parms.or.axis[0] );
	VectorCopy( fd->viewaxis[1], parms.or.axis[1] );
	VectorCopy( fd->viewaxis[2], parms.or.axis[2] );

	VectorCopy( fd->vieworg, parms.pvsOrigin );

	R_RenderView( &parms );

	// FAKK2: the sun's and the lensflare lights' flares over the world view
	if ( r_lensflare->integer && !( fd->rdflags & RDF_NOWORLDMODEL ) ) {
		R_DrawLensFlares( &parms );
	}

	// the next scene rendered in this frame will tack on after this one
	r_firstSceneDrawSurf = tr.refdef.numDrawSurfs;
	r_firstSceneEntity = r_numentities;
	r_firstSceneDlight = r_numdlights;
	r_firstScenePoly = r_numpolys;

	tr.frontEndMsec += ri.Milliseconds() - startTime;
}
