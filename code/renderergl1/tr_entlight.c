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
// tr_entlight.c: FAKK2's entity lights and the light cube they light a model with
//
// The BSP carries the map's static lights ("sphere lights", LUMP_ENTLIGHTS)
// and, for every leaf, the lights that reach it (LUMP_ENTLIGHTSVIS). A model
// the game flags betterlighting (RF_EXTRALIGHT) or invisible is not lit from
// the light grid at all: a six-sided light cube is built in the model's own
// axes from the worldspawn's ambientlight, the sun where a trace towards it
// reaches the sky, and the lights of the leaves the model's bounds touch, and
// every vertex takes the three faces its normal points at, weighted by the
// normal's components. The retail renderer does this in fakk2.exe at
// 0x4830c0 and 0x483220 (the lumps), 0x48e170 (the entry, with 0x48cff0 for
// the light origin, 0x48d1d0 the leaves, 0x48d3d0 the base colour, 0x48cd50
// the sun, 0x48d530 and 0x48d620 the lights, 0x48dd80 and 0x48d990 a point
// and a spot light, 0x48dfe0 the light direction, 0x48e090 the overbright
// scale) and 0x48e6e0 / 0x48e2e0 (a vertex's colour, the second with the
// scene's dynamic lights added per vertex).

#include "tr_local.h"

#define MAX_ENT_LIGHTS			1532		// the retail limit
#define ENT_LIGHT_SCALE			7500.0f		// intensity * this / distance^2
#define SUN_TRACE_DISTANCE		16384.0f
#define MAX_CUBE_LEAVES			4
#define MAX_CUBE_DLIGHTS		32

cvar_t	*r_light_emphasize;
cvar_t	*r_light_emphasizePercent;
cvar_t	*r_entityLights;
extern cvar_t	*r_debugLight;

// the faces of the cube, in the order the retail renderer keeps them
static const vec3_t cubeDirs[6] = {
	{ 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }
};

// the points a model's bounds are sampled at for its leaves: the centre and
// eight points a radius out
static const vec3_t leafProbes[9] = {
	{ 0, 0, 0 }, { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 }, { -1, 0, 0 },
	{ 0, -1, 0 }, { 1, 1, 0 }, { -1, -1, 0 }, { 0, 0, -1 }
};

// a light as the BSP stores it
typedef struct {
	float	origin[3];
	float	color[3];
	float	intensity;
	int		leaf;
	int		needsTrace;
	int		spot;
	float	spotDir[3];
	float	spotRadius;
} dentlight_t;

/*
=================
R_LoadEntLights
=================
*/
void R_LoadEntLights( world_t *w, const byte *fileBase, lump_t *l ) {
	dentlight_t		*in;
	entLight_t		*out;
	int				i, count;

	w->numEntLights = 0;
	w->entLights = NULL;

	if ( l->filelen % sizeof( dentlight_t ) ) {
		ri.Error( ERR_DROP, "LoadMap: funny lump size in spherelights of %s", w->name );
	}
	count = l->filelen / sizeof( dentlight_t );
	if ( count > MAX_ENT_LIGHTS ) {
		ri.Error( ERR_DROP, "LoadMap: Too many spherelights on map %s (max %d)", w->name, MAX_ENT_LIGHTS );
	}
	if ( !count ) {
		return;
	}

	in = (dentlight_t *)(fileBase + l->fileofs);
	out = ri.Hunk_Alloc( count * sizeof( *out ), h_low );
	w->entLights = out;
	w->numEntLights = count;

	for ( i = 0 ; i < count ; i++, in++, out++ ) {
		int		leaf;

		out->origin[0] = LittleFloat( in->origin[0] );
		out->origin[1] = LittleFloat( in->origin[1] );
		out->origin[2] = LittleFloat( in->origin[2] );
		out->color[0] = LittleFloat( in->color[0] );
		out->color[1] = LittleFloat( in->color[1] );
		out->color[2] = LittleFloat( in->color[2] );
		out->intensity = LittleFloat( in->intensity );
		leaf = LittleLong( in->leaf );
		if ( leaf < 0 || leaf >= w->numnodes - w->numDecisionNodes ) {
			ri.Error( ERR_DROP, "LoadMap: spherelight %d of %s is in leaf %d of %d", i, w->name, leaf, w->numnodes - w->numDecisionNodes );
		}
		out->leaf = w->nodes + w->numDecisionNodes + leaf;
		out->needsTrace = LittleLong( in->needsTrace );
		out->spot = LittleLong( in->spot );
		out->spotDir[0] = LittleFloat( in->spotDir[0] );
		out->spotDir[1] = LittleFloat( in->spotDir[1] );
		out->spotDir[2] = LittleFloat( in->spotDir[2] );
		out->spotRadius = LittleFloat( in->spotRadius );
		out->visited = 0;
	}
}

/*
=================
R_LoadEntLightsVis

Each leaf's list of the lights that reach it: light numbers ended by -1,
one list a leaf. A lump of one int a leaf (every list empty) is ignored.
=================
*/
void R_LoadEntLightsVis( world_t *w, const byte *fileBase, lump_t *l ) {
	int			*in;
	int			numInts, numLeafs, leafNum, i;

	w->entLightsVis = qfalse;

	if ( l->filelen & 3 ) {
		ri.Error( ERR_DROP, "LoadMap: funny lump size in spherelight visibility of %s", w->name );
	}
	numInts = l->filelen / 4;
	numLeafs = w->numnodes - w->numDecisionNodes;
	if ( !numInts || numInts == numLeafs || !w->numEntLights ) {
		return;
	}

	in = (int *)(fileBase + l->fileofs);
	for ( i = 0 ; i < numInts ; i++ ) {
		in[i] = LittleLong( in[i] );
	}

	leafNum = 0;
	i = 0;
	while ( i < numInts && leafNum < numLeafs ) {
		mnode_t	*leaf = w->nodes + w->numDecisionNodes + leafNum;
		int		count, j;

		for ( count = 0 ; i + count < numInts && in[i + count] != -1 ; count++ ) {
		}
		if ( count ) {
			leaf->entLights = ri.Hunk_Alloc( count * sizeof( entLight_t * ), h_low );
			leaf->numEntLights = count;
			for ( j = 0 ; j < count ; j++ ) {
				int		num = in[i + j];

				if ( num < 0 || num >= w->numEntLights ) {
					ri.Error( ERR_DROP, "LoadMap: spherelight visibility of %s names light %d of %d", w->name, num, w->numEntLights );
				}
				leaf->entLights[j] = &w->entLights[num];
			}
		}
		i += count + 1;
		leafNum++;
	}
	w->entLightsVis = qtrue;
}

/*
=================
R_EntLightTraceClear

Nothing solid between the model's light origin and a point.
=================
*/
static qboolean R_EntLightTraceClear( const vec3_t from, const vec3_t to, qboolean *sky ) {
	trace_t	trace;

	if ( !ri.CM_BoxTrace ) {
		if ( sky ) {
			*sky = qtrue;
		}
		return qtrue;
	}
	ri.CM_BoxTrace( &trace, from, to, NULL, NULL, 0, CONTENTS_SOLID, qfalse );
	if ( sky ) {
		*sky = ( trace.surfaceFlags & SURF_SKY ) != 0;
	}
	return trace.fraction >= 1.0f;
}

/*
=================
R_EntLightAddPoint

A point light's share of the cube's faces (0x48dd80).
=================
*/
static void R_EntLightAddPoint( trRefEntity_t *ent, const entLight_t *light ) {
	vec3_t	rel, local, d, v;
	float	inv, power, dot;
	int		i;

	VectorSubtract( light->origin, ent->e.origin, rel );
	local[0] = DotProduct( rel, ent->e.axis[0] );
	local[1] = DotProduct( rel, ent->e.axis[1] );
	local[2] = DotProduct( rel, ent->e.axis[2] );
	VectorSubtract( local, ent->lightLocalOrigin, d );

	inv = Q_rsqrt( DotProduct( d, d ) );
	power = fabs( light->intensity ) * ENT_LIGHT_SCALE * inv * inv;
	if ( power < 1.0f ) {
		return;
	}

	for ( i = 0 ; i < 6 ; i++ ) {
		VectorMA( d, ent->lightRadius, cubeDirs[i], v );
		VectorNormalizeFast( v );
		dot = DotProduct( v, cubeDirs[i] );
		if ( dot >= 0 ) {
			VectorMA( ent->lightCube[i], dot * power, light->color, ent->lightCube[i] );
		}
	}
}

/*
=================
R_EntLightAddSpot

A spot light's share of the cube's faces (0x48d990): the cube's centre must
lie inside the cone, which softens over its outer 32 units.
=================
*/
static void R_EntLightAddSpot( trRefEntity_t *ent, const entLight_t *light ) {
	vec3_t	rel, local, d, spotLocal, perp;
	float	coneScale, inv, power, along, dist, cone;
	int		i;

	if ( light->spotRadius >= 0 ) {
		coneScale = light->spotRadius;
		VectorSubtract( light->origin, ent->e.origin, rel );
	} else {
		// a light with a negative radius stands in for a line of light: the
		// nearest point to the model along its direction, up to that far out
		vec3_t	p, off;
		float	t, len;

		t = DotProduct( light->origin, light->spotDir ) - DotProduct( ent->lightOrigin, light->spotDir );
		VectorMA( ent->lightOrigin, t, light->spotDir, p );
		VectorSubtract( p, light->origin, off );
		len = DotProduct( off, off );
		if ( len > light->spotRadius * light->spotRadius ) {
			VectorScale( off, -light->spotRadius * Q_rsqrt( len ), off );
		}
		coneScale = 1.25f;
		VectorSubtract( light->origin, ent->e.origin, rel );
		VectorAdd( rel, off, rel );
	}

	local[0] = DotProduct( rel, ent->e.axis[0] );
	local[1] = DotProduct( rel, ent->e.axis[1] );
	local[2] = DotProduct( rel, ent->e.axis[2] );
	VectorSubtract( local, ent->lightLocalOrigin, d );

	inv = Q_rsqrt( DotProduct( d, d ) );
	power = light->intensity * ENT_LIGHT_SCALE * inv * inv;
	if ( power < 1.0f ) {
		return;
	}

	spotLocal[0] = DotProduct( light->spotDir, ent->e.axis[0] );
	spotLocal[1] = DotProduct( light->spotDir, ent->e.axis[1] );
	spotLocal[2] = DotProduct( light->spotDir, ent->e.axis[2] );

	along = -DotProduct( spotLocal, d );
	if ( along < -ent->lightRadius ) {
		return;
	}

	for ( i = 0 ; i < 6 ; i++ ) {
		float	facing = -DotProduct( spotLocal, cubeDirs[i] );
		float	f;

		if ( facing < 0 || along < 0 ) {
			continue;
		}
		VectorMA( d, along, spotLocal, perp );
		cone = along * coneScale;
		dist = VectorLength( perp );
		if ( dist >= cone ) {
			continue;
		}
		f = ( cone - 32.0f < dist ) ? ( cone - dist ) * ( 1.0f / 32.0f ) : 1.0f;
		f = facing * f * power;
		if ( f > 0 ) {
			VectorMA( ent->lightCube[i], f, light->color, ent->lightCube[i] );
		}
	}
}

/*
=================
R_EntLightAdd
=================
*/
static void R_EntLightAdd( trRefEntity_t *ent, const entLight_t *light ) {
	qboolean	invisible = ( ent->e.renderfx & RF_INVISIBLE ) != 0;

	// a light of negative intensity lights only invisible models, one of
	// positive intensity everything else
	if ( ( light->intensity > 0 && invisible ) || ( light->intensity < 0 && !invisible ) ) {
		return;
	}
	if ( light->needsTrace && !R_EntLightTraceClear( ent->lightOrigin, light->origin, NULL ) ) {
		if ( r_debugLight->integer > 2 ) {
			ri.Printf( PRINT_ALL, "cube: ent %d light %d at %.0f %.0f %.0f blocked\n", ent->e.entityNumber,
				(int)( light - tr.world->entLights ), light->origin[0], light->origin[1], light->origin[2] );
		}
		return;
	}
	if ( r_debugLight->integer > 2 ) {
		ri.Printf( PRINT_ALL, "cube: ent %d light %d at %.0f %.0f %.0f %s intensity %.0f\n", ent->e.entityNumber,
			(int)( light - tr.world->entLights ), light->origin[0], light->origin[1], light->origin[2],
			light->spot ? "spot" : "point", light->intensity );
	}
	if ( light->spot ) {
		R_EntLightAddSpot( ent, light );
	} else {
		R_EntLightAddPoint( ent, light );
	}
}

/*
=================
R_SetupEntityLightCube

Builds the light cube of a model flagged betterlighting or invisible.
origin is where the model is lit from in the world, localOrigin the same
point in the model's axes (the centre of its bounds), radius half its
bounds' radius.
=================
*/
void R_SetupEntityLightCube( trRefEntity_t *ent, const vec3_t origin, const vec3_t localOrigin, float radius ) {
	world_t		*w = tr.world;
	mnode_t		*leaves[MAX_CUBE_LEAVES];
	int			numLeaves, i, j;
	vec3_t		offset;
	qboolean	hasOffset;
	float		scale;

	VectorCopy( origin, ent->lightOrigin );
	VectorCopy( localOrigin, ent->lightLocalOrigin );
	ent->lightRadius = radius;
	ent->lightCubeSet = qtrue;
	ent->lightCubeDlights = qfalse;
	VectorCopy( tr.sunDirection, ent->lightDir );

	if ( ( tr.refdef.rdflags & RDF_NOWORLDMODEL ) || ( ent->e.renderfx & RF_FULLBRIGHT ) || !w ) {
		for ( i = 0 ; i < 6 ; i++ ) {
			VectorSet( ent->lightCube[i], tr.identityLightByte, tr.identityLightByte, tr.identityLightByte );
		}
		return;
	}

	// the leaves the model's bounds reach into, at most four
	numLeaves = 0;
	for ( i = 0 ; i < 9 ; i++ ) {
		vec3_t	p;
		mnode_t	*leaf;

		VectorMA( origin, radius, leafProbes[i], p );
		leaf = R_PointInLeaf( p );
		if ( leaf->area == -1 ) {
			continue;
		}
		for ( j = 0 ; j < numLeaves ; j++ ) {
			if ( leaves[j] == leaf ) {
				break;
			}
		}
		if ( j == numLeaves && numLeaves < MAX_CUBE_LEAVES ) {
			leaves[numLeaves++] = leaf;
		}
	}

	// the base: the game's light offset or minimum light, over the
	// worldspawn's ambient when the map has lights
	hasOffset = qfalse;
	VectorClear( offset );
	if ( ent->e.renderfx & RF_LIGHTOFFSET ) {
		VectorSet( offset, ent->e.shaderRGBA[0], ent->e.shaderRGBA[1], ent->e.shaderRGBA[2] );
		hasOffset = qtrue;
	} else if ( ent->e.renderfx & RF_MINLIGHT ) {
		VectorSet( offset, tr.identityLight * 32, tr.identityLight * 32, tr.identityLight * 32 );
		hasOffset = qtrue;
	}

	if ( !w->numEntLights || r_fullbright->integer || !r_entityLights->integer || !numLeaves ) {
		for ( i = 0 ; i < 6 ; i++ ) {
			VectorSet( ent->lightCube[i], tr.identityLightByte, tr.identityLightByte, tr.identityLightByte );
			if ( hasOffset ) {
				VectorAdd( ent->lightCube[i], offset, ent->lightCube[i] );
			}
		}
		return;
	}
	for ( i = 0 ; i < 6 ; i++ ) {
		VectorAdd( tr.entAmbient, offset, ent->lightCube[i] );
	}

	// the sun, where the sky can be seen from the model
	if ( tr.hasSun ) {
		vec3_t		end, sunLocal;
		qboolean	sky;

		VectorMA( origin, SUN_TRACE_DISTANCE, tr.sunDir, end );
		R_EntLightTraceClear( origin, end, &sky );
		if ( r_debugLight->integer > 2 ) {
			ri.Printf( PRINT_ALL, "cube: ent %d renderfx %x sun %s (dir %.2f %.2f %.2f colour %.0f %.0f %.0f)\n", ent->e.entityNumber, ent->e.renderfx,
				sky ? "seen" : "hidden", tr.sunDir[0], tr.sunDir[1], tr.sunDir[2], tr.sunColor[0], tr.sunColor[1], tr.sunColor[2] );
		}
		if ( sky ) {
			sunLocal[0] = DotProduct( tr.sunDir, ent->e.axis[0] );
			sunLocal[1] = DotProduct( tr.sunDir, ent->e.axis[1] );
			sunLocal[2] = DotProduct( tr.sunDir, ent->e.axis[2] );
			VectorNormalizeFast( sunLocal );
			for ( i = 0 ; i < 6 ; i++ ) {
				float	d = DotProduct( sunLocal, cubeDirs[i] );

				if ( d > 0 ) {
					VectorMA( ent->lightCube[i], d, tr.sunColor, ent->lightCube[i] );
				}
			}
		}
	}

	// the map's lights: those of the model's leaves, or all of them when the
	// map has no visibility lists; each once, and none from an area the view
	// cannot see
	tr.entLightVisit++;
	if ( !w->entLightsVis ) {
		for ( i = 0 ; i < w->numEntLights ; i++ ) {
			R_EntLightAdd( ent, &w->entLights[i] );
		}
	} else {
		for ( i = 0 ; i < numLeaves ; i++ ) {
			for ( j = 0 ; j < leaves[i]->numEntLights ; j++ ) {
				entLight_t	*light = leaves[i]->entLights[j];
				int			area = light->leaf->area;

				if ( area >= 0 && ( tr.refdef.areamask[area >> 3] & ( 1 << ( area & 7 ) ) ) ) {
					continue;
				}
				if ( light->visited == tr.entLightVisit ) {
					continue;
				}
				light->visited = tr.entLightVisit;
				R_EntLightAdd( ent, light );
			}
		}
	}

	// with dynamic lights in the scene they are added to each vertex instead
	if ( tr.refdef.num_dlights > 0 ) {
		ent->lightCubeDlights = qtrue;
	}

	// the direction most of the light comes from, for the shadow
	{
		vec3_t	dir;

		VectorClear( dir );
		for ( i = 0 ; i < 6 ; i++ ) {
			VectorMA( dir, VectorLength( ent->lightCube[i] ), cubeDirs[i], dir );
		}
		if ( VectorNormalize( dir ) > 0 ) {
			VectorCopy( dir, ent->lightDir );
		}
	}

	if ( r_light_emphasizePercent->value ) {
		scale = r_light_emphasizePercent->value * 0.01f + 1.0f;
		for ( i = 0 ; i < 6 ; i++ ) {
			VectorScale( ent->lightCube[i], scale, ent->lightCube[i] );
		}
	}
	if ( r_light_emphasize->integer > 1 ) {
		for ( i = 0 ; i < 6 ; i++ ) {
			ent->lightCube[i][0] += r_light_emphasize->value;
			ent->lightCube[i][1] += r_light_emphasize->value;
			ent->lightCube[i][2] += r_light_emphasize->value;
		}
	}

	// the overbright shift the lightmaps get, and a face brighter than 255
	// is brought back to that length, keeping its hue
	if ( !ent->lightCubeDlights ) {
		scale = 1 << ( r_mapOverBrightBits->integer - tr.overbrightBits );
		for ( i = 0 ; i < 6 ; i++ ) {
			VectorScale( ent->lightCube[i], scale, ent->lightCube[i] );
			if ( ent->lightCube[i][0] > 255 || ent->lightCube[i][1] > 255 || ent->lightCube[i][2] > 255 ) {
				VectorNormalize( ent->lightCube[i] );
				VectorScale( ent->lightCube[i], 255, ent->lightCube[i] );
			}
		}
	}

	if ( r_debugLight->integer > 1 ) {
		ri.Printf( PRINT_ALL, "cube: ent %d at %.0f %.0f %.0f lit at %.0f %.0f %.0f radius %.0f leaves %d%s: +x %.0f %.0f %.0f -x %.0f %.0f %.0f +y %.0f %.0f %.0f -y %.0f %.0f %.0f +z %.0f %.0f %.0f -z %.0f %.0f %.0f\n",
			ent->e.entityNumber, ent->e.origin[0], ent->e.origin[1], ent->e.origin[2],
			origin[0], origin[1], origin[2], radius, numLeaves, ent->lightCubeDlights ? " (dlights per vertex)" : "",
			ent->lightCube[0][0], ent->lightCube[0][1], ent->lightCube[0][2],
			ent->lightCube[1][0], ent->lightCube[1][1], ent->lightCube[1][2],
			ent->lightCube[2][0], ent->lightCube[2][1], ent->lightCube[2][2],
			ent->lightCube[3][0], ent->lightCube[3][1], ent->lightCube[3][2],
			ent->lightCube[4][0], ent->lightCube[4][1], ent->lightCube[4][2],
			ent->lightCube[5][0], ent->lightCube[5][1], ent->lightCube[5][2] );
	}
}

/*
=================
RB_CalcLightCubeColors

The colour of every vertex from the entity's light cube, and the scene's
dynamic lights per vertex when there are any (0x48e6e0, 0x48e2e0).
=================
*/
void RB_CalcLightCubeColors( unsigned char *colors ) {
	const trRefEntity_t	*ent = backEnd.currentEntity;
	float				*normal = tess.normal[0];
	float				*v = tess.xyz[0];
	int					i, k;
	struct {
		vec3_t	origin;
		vec3_t	color;
		float	radius;
	}					dlights[MAX_CUBE_DLIGHTS];
	int					numDlights = 0;
	float				factor = 1.0f;

	if ( ent->lightCubeDlights ) {
		for ( i = 0 ; i < backEnd.refdef.num_dlights && numDlights < MAX_CUBE_DLIGHTS ; i++ ) {
			const dlight_t	*dl = &backEnd.refdef.dlights[i];
			vec3_t			rel;

			VectorSubtract( dl->origin, ent->lightOrigin, rel );
			if ( VectorLength( rel ) > ent->lightRadius + dl->radius ) {
				continue;
			}
			VectorSubtract( dl->origin, ent->e.origin, rel );
			dlights[numDlights].origin[0] = DotProduct( rel, ent->e.axis[0] );
			dlights[numDlights].origin[1] = DotProduct( rel, ent->e.axis[1] );
			dlights[numDlights].origin[2] = DotProduct( rel, ent->e.axis[2] );
			VectorCopy( dl->color, dlights[numDlights].color );
			dlights[numDlights].radius = dl->radius;
			numDlights++;
		}
		factor = 1 << ( r_mapOverBrightBits->integer - tr.overbrightBits );
	}

	for ( i = 0 ; i < tess.numVertexes ; i++, normal += 4, v += 4, colors += 4 ) {
		vec3_t	c;
		int		ic[3];

		VectorClear( c );
		for ( k = 0 ; k < 3 ; k++ ) {
			const float	*face = ent->lightCube[ k * 2 + ( normal[k] < 0 ? 1 : 0 )];

			VectorMA( c, fabs( normal[k] ), face, c );
		}

		if ( !ent->lightCubeDlights ) {
			for ( k = 0 ; k < 3 ; k++ ) {
				colors[k] = c[k] > 255 ? 255 : (int)c[k];
			}
			colors[3] = 255;
			continue;
		}

		for ( k = 0 ; k < 3 ; k++ ) {
			ic[k] = (int)( c[k] * factor );
		}
		for ( k = 0 ; k < numDlights ; k++ ) {
			vec3_t	dir;
			float	dot, inv, f;

			VectorSubtract( dlights[k].origin, v, dir );
			dot = DotProduct( dir, normal );
			if ( dot <= 0 ) {
				continue;
			}
			inv = Q_rsqrt( DotProduct( dir, dir ) );
			f = ENT_LIGHT_SCALE * dlights[k].radius * inv * inv * inv * dot;
			ic[0] = (int)( f * dlights[k].color[0] + ic[0] );
			ic[1] = (int)( f * dlights[k].color[1] + ic[1] );
			ic[2] = (int)( f * dlights[k].color[2] + ic[2] );
		}
		if ( ( ic[0] | ic[1] | ic[2] ) & ~255 ) {
			int		max = ic[0] > ic[1] ? ic[0] : ic[1];
			float	s;

			if ( ic[2] > max ) {
				max = ic[2];
			}
			s = 255.0f / max;
			ic[0] = (int)( ic[0] * s );
			ic[1] = (int)( ic[1] * s );
			ic[2] = (int)( ic[2] * s );
		}
		colors[0] = ic[0];
		colors[1] = ic[1];
		colors[2] = ic[2];
		colors[3] = 255;
	}
}
