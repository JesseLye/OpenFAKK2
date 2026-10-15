/*
===========================================================================
Decal fragments for Heavy Metal: F.A.K.K. 2.

FAKK2's client game module asks the engine for decal polygons through its
CM_MarkFragments import, and the retail engine answers it from the collision
model, not the renderer (fakk2.exe 0x426a90; Quake III fitted marks to the
drawn surfaces in R_MarkFragments). For each brush the decal's box touches,
each side that faces the projection head on (the cosine between them is
-0.9 or less) and is not flagged SURF_NOIMPACT or SURF_NOMARKS takes the
decal's corners projected onto its plane, stopped one unit short of it, and
clipped by the brush's other sides.

The one-unit stop is what keeps a decal in front of the wall it lies on: the
mark shaders have no polygonOffset, and a mark fitted exactly to the surface
fights it in the depth buffer. Curved patches have no brushes, so they take
no marks, as in retail.
===========================================================================
*/

#include "cm_local.h"

#define	MARK_MAX_POINTS		16		// retail's clip buffers
#define	MARK_MAX_BRUSHES	32		// retail's CM_BoxBrushes list
#define	MARK_FACING			-0.9	// a side must face the projection this squarely
#define	MARK_STANDOFF		1.0f	// how far short of the plane the corners stop
#define	MARK_CLIP_EPSILON	0.5f

#define	SIDE_FRONT	0
#define	SIDE_BACK	1
#define	SIDE_ON		2

/*
=================
CM_ChopPolyBehindPlane

Keeps the part of the polygon in front of the plane (0x426e00). The same as
the renderer's R_ChopPolyBehindPlane, with retail's smaller buffers.
=================
*/
static void CM_ChopPolyBehindPlane( int numInPoints, vec3_t inPoints[MARK_MAX_POINTS],
		int *numOutPoints, vec3_t outPoints[MARK_MAX_POINTS],
		const vec3_t normal, vec_t dist, vec_t epsilon ) {
	float	dists[MARK_MAX_POINTS + 4] = { 0 };
	int		sides[MARK_MAX_POINTS + 4] = { 0 };
	int		counts[3];
	float	dot, d;
	int		i, j;
	float	*p1, *p2, *clip;

	// don't clip if it might overflow
	if ( numInPoints >= MARK_MAX_POINTS - 2 ) {
		*numOutPoints = 0;
		return;
	}

	counts[0] = counts[1] = counts[2] = 0;

	for ( i = 0 ; i < numInPoints ; i++ ) {
		dot = DotProduct( inPoints[i], normal ) - dist;
		dists[i] = dot;
		if ( dot > epsilon ) {
			sides[i] = SIDE_FRONT;
		} else if ( dot < -epsilon ) {
			sides[i] = SIDE_BACK;
		} else {
			sides[i] = SIDE_ON;
		}
		counts[sides[i]]++;
	}
	sides[i] = sides[0];
	dists[i] = dists[0];

	*numOutPoints = 0;

	if ( !counts[SIDE_FRONT] ) {
		return;
	}
	if ( !counts[SIDE_BACK] ) {
		*numOutPoints = numInPoints;
		Com_Memcpy( outPoints, inPoints, numInPoints * sizeof( vec3_t ) );
		return;
	}

	for ( i = 0 ; i < numInPoints ; i++ ) {
		p1 = inPoints[i];
		clip = outPoints[*numOutPoints];

		if ( sides[i] == SIDE_ON ) {
			VectorCopy( p1, clip );
			( *numOutPoints )++;
			continue;
		}

		if ( sides[i] == SIDE_FRONT ) {
			VectorCopy( p1, clip );
			( *numOutPoints )++;
			clip = outPoints[*numOutPoints];
		}

		if ( sides[i + 1] == SIDE_ON || sides[i + 1] == sides[i] ) {
			continue;
		}

		// generate a split point
		p2 = inPoints[( i + 1 ) % numInPoints];

		d = dists[i] - dists[i + 1];
		if ( d == 0 ) {
			dot = 0;
		} else {
			dot = dists[i] / d;
		}

		for ( j = 0 ; j < 3 ; j++ ) {
			clip[j] = p1[j] + dot * ( p2[j] - p1[j] );
		}

		( *numOutPoints )++;
	}
}

/*
=================
CM_MarkFragments

The polygons a decal with these corners makes when projected along
projection (whose length is how far it reaches either side of a surface).
Returns the number of fragments; their points are in pointBuffer.
=================
*/
int CM_MarkFragments( int numPoints, const vec3_t *points, const vec3_t projection,
		int maxPoints, vec3_t pointBuffer, int maxFragments, markFragment_t *fragmentBuffer ) {
	vec3_t			projectionDir, mins, maxs, temp, normal;
	vec3_t			clipPoints[2][MARK_MAX_POINTS];
	cbrush_t		*brushes[MARK_MAX_BRUSHES];
	float			projectionLength;
	int				numBrushes;
	int				returnedPoints, returnedFragments;
	int				b, s, o, i;

	if ( numPoints <= 0 || numPoints > MARK_MAX_POINTS ) {
		return 0;
	}

	projectionLength = VectorNormalize2( projection, projectionDir );

	ClearBounds( mins, maxs );
	for ( i = 0 ; i < numPoints ; i++ ) {
		AddPointToBounds( points[i], mins, maxs );
		VectorAdd( points[i], projection, temp );
		AddPointToBounds( temp, mins, maxs );
	}

	numBrushes = CM_BoxBrushes( mins, maxs, brushes, MARK_MAX_BRUSHES );

	returnedPoints = 0;
	returnedFragments = 0;

	for ( b = 0 ; b < numBrushes ; b++ ) {
		cbrush_t	*brush = brushes[b];

		for ( s = 0 ; s < brush->numsides ; s++ ) {
			cbrushside_t	*side = &brush->sides[s];
			cplane_t		*plane = side->plane;
			float			facing;
			int				numClipPoints, pingPong;

			if ( side->surfaceFlags & ( SURF_NOIMPACT | SURF_NOMARKS ) ) {
				continue;
			}
			facing = DotProduct( projectionDir, plane->normal );
			if ( facing > MARK_FACING ) {
				continue;
			}

			// move each corner along the projection to just short of the plane;
			// a corner that would have to go further than the projection
			// reaches rules the side out
			for ( i = 0 ; i < numPoints ; i++ ) {
				float	t = -( DotProduct( plane->normal, points[i] ) - plane->dist ) / facing - MARK_STANDOFF;

				if ( t > projectionLength || t < -projectionLength ) {
					break;
				}
				VectorMA( points[i], t, projectionDir, clipPoints[0][i] );
			}
			if ( i != numPoints ) {
				continue;
			}

			// keep the part inside the brush's other sides
			numClipPoints = numPoints;
			pingPong = 0;
			for ( o = 0 ; o < brush->numsides ; o++ ) {
				cplane_t	*other;

				if ( o == s ) {
					continue;
				}
				other = brush->sides[o].plane;
				VectorNegate( other->normal, normal );
				CM_ChopPolyBehindPlane( numClipPoints, clipPoints[pingPong], &numClipPoints,
					clipPoints[!pingPong], normal, -other->dist, MARK_CLIP_EPSILON );
				pingPong ^= 1;
				if ( !numClipPoints ) {
					break;
				}
			}
			if ( !numClipPoints ) {
				continue;
			}

			if ( returnedPoints + numClipPoints > maxPoints ) {
				return returnedFragments;	// not enough space for this polygon
			}

			fragmentBuffer->firstPoint = returnedPoints;
			fragmentBuffer->numPoints = numClipPoints;
			Com_Memcpy( pointBuffer + returnedPoints * 3, clipPoints[pingPong], numClipPoints * sizeof( vec3_t ) );
			returnedPoints += numClipPoints;
			fragmentBuffer++;
			returnedFragments++;
			if ( returnedFragments == maxFragments ) {
				return returnedFragments;
			}
		}
	}

	return returnedFragments;
}
