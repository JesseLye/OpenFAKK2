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
/*
===========================================================================
tr_lod.c: FAKK2's level of detail.

A .tan or .skb surface, and a triangle soup the map compiler baked from a
model, is a progressive mesh: its vertexes are ordered by importance and each
one names an earlier vertex it collapses into (the collapse map; vertex 0
collapses into itself). Drawing a surface at a level of detail between 0 and
1 keeps its first numVerts * lod vertexes, no fewer than its minLod, moves
every later vertex to the kept vertex its collapses lead to, and leaves out
the triangles that lose an edge that way.

The level of detail of a model is how large it looks: the share of the view's
height its radius covers, times r_lodscale (at most 20) and the model's own
scale, less r_lodbias and the model's own bias, kept between 0 and 1.
===========================================================================
*/

#include "tr_local.h"

/*
=================
R_ProjectRadius

How much of the view's height, from its centre to its edge, a sphere of
radius r at location covers, at most 1; 0 when the location is behind the
view (fakk2.exe 0x486aa0, as Quake III's ProjectRadius).
=================
*/
float R_ProjectRadius( const viewParms_t *view, float r, const vec3_t location ) {
	float	pr;
	float	dist;
	float	c;
	vec3_t	p;
	float	projected[4];

	c = DotProduct( view->or.axis[0], view->or.origin );
	dist = DotProduct( view->or.axis[0], location ) - c;

	if ( dist <= 0 )
		return 0;

	p[0] = 0;
	p[1] = fabs( r );
	p[2] = -dist;

	projected[1] = p[0] * view->projectionMatrix[1] +
				   p[1] * view->projectionMatrix[5] +
				   p[2] * view->projectionMatrix[9] +
				   view->projectionMatrix[13];

	projected[3] = p[0] * view->projectionMatrix[3] +
				   p[1] * view->projectionMatrix[7] +
				   p[2] * view->projectionMatrix[11] +
				   view->projectionMatrix[15];

	pr = projected[1] / projected[3];

	if ( pr > 1.0f )
		pr = 1.0f;

	return pr;
}

/*
=================
R_CalcLod

The level of detail of a model of this radius at origin (fakk2.exe 0x487090;
R_AddSkelSurfaces 0x4872d0 and R_AddTikiSurfaces 0x489b40 inline the same).
A model behind the view counts as filling it.
=================
*/
float R_CalcLod( const viewParms_t *view, const vec3_t origin, float radius, float lodScale, float lodBias ) {
	float	pr, scale, lod;

	pr = R_ProjectRadius( view, radius, origin );
	if ( pr == 0 ) {
		lod = 1;
	} else {
		scale = r_lodscale->value;
		if ( scale > 20 ) {
			scale = 20;
		}
		lod = scale * lodScale * pr;
	}
	lod -= r_lodbias->value + lodBias;
	if ( lod < 0 ) {
		return 0;
	}
	if ( lod > 1 ) {
		return 1;
	}
	return lod;
}

/*
=================
R_LodVertexCount

How many of a surface's vertexes a level of detail keeps: the share of them,
cut down to a whole number, no fewer than minLod and no more than there are
(the back end's 0x495b1c, 0x495d3e and 0x48fb44). 0 draws nothing: retail
leaves the surface out when even minLod is 0 or less, and with none kept
every triangle collapses.
=================
*/
int R_LodVertexCount( int numVerts, float minLod, float lod ) {
	int		count;

	count = (int)( numVerts * lod );
	if ( count < minLod ) {
		count = (int)minLod;
	}
	if ( count > numVerts ) {
		count = numVerts;
	}
	if ( count < 0 ) {
		count = 0;
	}
	return count;
}

/*
=================
R_LodTriangles

Writes the triangles of a surface drawn with its first renderCount vertexes
into out, each vertex moved along the collapse map to a kept one, and returns
how many indexes that is. A triangle two of whose vertexes become one is left
out. numVerts is at most LOD_MAX_VERTS.
=================
*/
int R_LodTriangles( const int *collapse, int numVerts, int renderCount, const int *indexes, int numIndexes, int *out ) {
	static int	map[LOD_MAX_VERTS];
	int			i, numOut = 0;

	if ( renderCount <= 0 ) {
		return 0;
	}
	for ( i = 0 ; i < renderCount ; i++ ) {
		map[i] = i;
	}
	// a collapse always leads to an earlier vertex, so one pass settles them
	for ( ; i < numVerts ; i++ ) {
		map[i] = map[collapse[i]];
	}

	for ( i = 0 ; i + 2 < numIndexes ; i += 3 ) {
		int		a = map[indexes[i]], b = map[indexes[i + 1]], c = map[indexes[i + 2]];

		if ( a == b || b == c || c == a ) {
			continue;
		}
		out[numOut++] = a;
		out[numOut++] = b;
		out[numOut++] = c;
	}
	return numOut;
}
