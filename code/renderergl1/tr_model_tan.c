/*
===========================================================================
Static models from FAKK2's vertex animation files (.tan).

A .tik model definition names its animations, its scale and the shader of
each surface; the engine's TIKI code reads that and hands the pieces to
RE_RegisterTan. One frame of one animation is turned into triangle surfaces
here, so the model is drawn as it stands in that frame. Animation, level of
detail and tags are not handled, and skeletal models (.skb) are not either.
===========================================================================
*/

#include "tr_local.h"

#define	TAN_IDENT				((' '<<24)+('N'<<16)+('A'<<8)+'T')
#define	TAN_VERSION				2
#define	TAN_HEADER_SIZE			176
#define	TAN_FRAME_SIZE			68
#define	TAN_SURFACE_HEADER_SIZE	104
#define	TAN_MAX_SURFACES		32
#define	TAN_MAX_VERTS			1200
#define	TAN_MAX_TRIANGLES		4096

// a surface is cut into pieces the tesselator can take in one go
#define	TAN_PIECE_VERTS			( SHADER_MAX_VERTEXES - 100 )
#define	TAN_PIECE_INDEXES		( SHADER_MAX_INDEXES - 600 )

typedef struct {
	int				numPieces;
	srfTriangles_t	**pieces;
	shader_t		**shaders;		// one per piece
	float			radius;			// farthest vertex from the model's origin
} tanModel_t;

static int TAN_Int( const byte *p ) {
	return (int)( p[0] | ( p[1] << 8 ) | ( p[2] << 16 ) | ( (unsigned)p[3] << 24 ) );
}

static float TAN_Float( const byte *p ) {
	floatint_t	fi;

	fi.i = TAN_Int( p );
	return fi.f;
}

static qboolean TAN_InFile( int length, int offset, int size ) {
	return offset >= 0 && size >= 0 && offset <= length && size <= length - offset;
}

/*
===============
R_TanShader

A surface's shader is either a shader script's name or an image beside the
model.
===============
*/
static shader_t *R_TanShader( const char *name, const char *path ) {
	char		stripped[MAX_QPATH], local[MAX_QPATH];
	shader_t	*sh;

	if ( !name || !name[0] ) {
		return tr.defaultShader;
	}

	COM_StripExtension( name, stripped, sizeof( stripped ) );
	sh = R_FindShader( stripped, LIGHTMAP_NONE, qtrue );
	if ( !sh->defaultShader ) {
		return sh;
	}

	if ( path && path[0] ) {
		Com_sprintf( local, sizeof( local ), "%s/%s", path, stripped );
		sh = R_FindShader( local, LIGHTMAP_NONE, qtrue );
	}
	return sh;
}

/*
===============
R_TanAddPiece

Copies the triangles [firstTri, firstTri + numTris) of a surface into a new
triangle surface holding only the vertexes they use.
===============
*/
static srfTriangles_t *R_TanAddPiece( const drawVert_t *verts, int numVerts, const int *tris, int firstTri, int numTris ) {
	static int		remap[TAN_MAX_VERTS];
	srfTriangles_t	*srf;
	int				i, j, used = 0;

	for ( i = 0 ; i < numVerts ; i++ ) {
		remap[i] = -1;
	}
	for ( i = 0 ; i < numTris * 3 ; i++ ) {
		j = tris[firstTri * 3 + i];
		if ( remap[j] < 0 ) {
			remap[j] = used++;
		}
	}

	srf = ri.Hunk_Alloc( sizeof( *srf ) + used * sizeof( drawVert_t ) + numTris * 3 * sizeof( int ), h_low );
	srf->surfaceType = SF_TRIANGLES;
	srf->numVerts = used;
	srf->verts = (drawVert_t *)( srf + 1 );
	srf->numIndexes = numTris * 3;
	srf->indexes = (int *)( srf->verts + used );

	ClearBounds( srf->bounds[0], srf->bounds[1] );
	for ( i = 0 ; i < numVerts ; i++ ) {
		if ( remap[i] >= 0 ) {
			srf->verts[remap[i]] = verts[i];
			AddPointToBounds( verts[i].xyz, srf->bounds[0], srf->bounds[1] );
		}
	}
	for ( i = 0 ; i < numTris * 3 ; i++ ) {
		srf->indexes[i] = remap[tris[firstTri * 3 + i]];
	}
	return srf;
}

/*
===============
RE_RegisterTan

name identifies the model (the .tik file's name). The surfaces of the
animation file are matched by name against surfaceNames to find their
shaders; a NULL shader name leaves the surface out. Vertexes are scaled and
then moved by origin.
===============
*/
qhandle_t RE_RegisterTan( const char *name, const char *tanFile, int frame, float scale, const vec3_t origin,
						  const char *path, int numSurfaceNames, const char **surfaceNames, const char **shaderNames ) {
	static drawVert_t	verts[TAN_MAX_VERTS];
	static int			tris[TAN_MAX_TRIANGLES * 3];
	srfTriangles_t		*pieces[TAN_MAX_SURFACES * 8];
	shader_t			*shaders[TAN_MAX_SURFACES * 8];
	union {
		byte	*b;
		void	*v;
	} buffer;
	model_t		*mod;
	tanModel_t	*tan;
	qhandle_t	hModel;
	const byte	*data, *surf, *frameData;
	vec3_t		frameScale, frameOffset;
	int			length, numFrames, numSurfaces, ofsFrames, ofsSurfaces;
	int			numPieces = 0;
	float		radius = 0;
	int			i, j, k;

	if ( !name || !name[0] || strlen( name ) >= MAX_QPATH ) {
		return 0;
	}

	// search the currently loaded models
	for ( hModel = 1 ; hModel < tr.numModels ; hModel++ ) {
		mod = tr.models[hModel];
		if ( !strcmp( mod->name, name ) ) {
			return mod->type == MOD_BAD ? 0 : hModel;
		}
	}

	if ( ( mod = R_AllocModel() ) == NULL ) {
		ri.Printf( PRINT_WARNING, "RE_RegisterTan: R_AllocModel() failed for '%s'\n", name );
		return 0;
	}
	Q_strncpyz( mod->name, name, sizeof( mod->name ) );
	mod->type = MOD_BAD;

	length = ri.FS_ReadFile( tanFile, &buffer.v );
	if ( !buffer.b || length < TAN_HEADER_SIZE ) {
		if ( buffer.b ) {
			ri.FS_FreeFile( buffer.v );
		}
		ri.Printf( PRINT_WARNING, "RE_RegisterTan: couldn't load %s\n", tanFile );
		return 0;
	}
	data = buffer.b;

	numFrames = TAN_Int( data + 72 );
	numSurfaces = TAN_Int( data + 80 );
	ofsFrames = TAN_Int( data + 100 );
	ofsSurfaces = TAN_Int( data + 104 );
	if ( TAN_Int( data ) != TAN_IDENT || TAN_Int( data + 4 ) != TAN_VERSION
		|| numFrames < 1 || numSurfaces < 0 || numSurfaces > TAN_MAX_SURFACES
		|| numFrames > length / TAN_FRAME_SIZE || !TAN_InFile( length, ofsFrames, numFrames * TAN_FRAME_SIZE ) ) {
		ri.FS_FreeFile( buffer.v );
		ri.Printf( PRINT_WARNING, "RE_RegisterTan: %s is not a version %i TAN file\n", tanFile, TAN_VERSION );
		return 0;
	}

	if ( frame < 0 || frame >= numFrames ) {
		frame = 0;
	}
	frameData = data + ofsFrames + frame * TAN_FRAME_SIZE;
	for ( i = 0 ; i < 3 ; i++ ) {
		frameScale[i] = TAN_Float( frameData + 24 + i * 4 );
		frameOffset[i] = TAN_Float( frameData + 36 + i * 4 );
	}

	surf = data + ofsSurfaces;
	for ( i = 0 ; i < numSurfaces ; i++ ) {
		const char	*surfName;
		shader_t	*sh = tr.defaultShader;
		int			surfFrames, numVerts, numTris, ofsTris, ofsSt, ofsXyz, ofsEnd;
		int			surfLength = length - (int)( surf - data );
		int			firstTri;

		if ( surfLength < TAN_SURFACE_HEADER_SIZE ) {
			break;
		}
		surfName = (const char *)surf + 4;
		surfFrames = TAN_Int( surf + 68 );
		numVerts = TAN_Int( surf + 72 );
		numTris = TAN_Int( surf + 80 );
		ofsTris = TAN_Int( surf + 84 );
		ofsSt = TAN_Int( surf + 92 );
		ofsXyz = TAN_Int( surf + 96 );
		ofsEnd = TAN_Int( surf + 100 );

		if ( numVerts < 0 || numVerts > TAN_MAX_VERTS || numTris < 0 || numTris > TAN_MAX_TRIANGLES
			|| surfFrames != numFrames || !memchr( surfName, 0, MAX_QPATH )
			|| !TAN_InFile( surfLength, ofsTris, numTris * 12 )
			|| !TAN_InFile( surfLength, ofsSt, numVerts * 8 )
			|| !TAN_InFile( surfLength, ofsXyz, numVerts * 8 )
			|| numFrames > ( surfLength - ofsXyz ) / 8 / ( numVerts ? numVerts : 1 )
			|| ofsEnd < TAN_SURFACE_HEADER_SIZE || ofsEnd > surfLength ) {
			ri.Printf( PRINT_WARNING, "RE_RegisterTan: %s has a malformed surface\n", tanFile );
			break;
		}

		for ( j = 0 ; j < numSurfaceNames ; j++ ) {
			if ( !Q_stricmp( surfaceNames[j], surfName ) ) {
				sh = shaderNames[j] ? R_TanShader( shaderNames[j], path ) : NULL;
				break;
			}
		}
		if ( !sh ) {
			surf += ofsEnd;
			continue;		// hidden by the model's definition
		}

		for ( j = 0 ; j < numVerts ; j++ ) {
			const byte	*xyz = surf + ofsXyz + ( frame * numVerts + j ) * 8;
			const byte	*st = surf + ofsSt + j * 8;
			unsigned	normal = xyz[6] | ( xyz[7] << 8 );
			float		lat, lng;

			for ( k = 0 ; k < 3 ; k++ ) {
				unsigned	v = xyz[k * 2] | ( xyz[k * 2 + 1] << 8 );

				verts[j].xyz[k] = ( v * frameScale[k] + frameOffset[k] ) * scale + origin[k];
			}

			if ( VectorLength( verts[j].xyz ) > radius ) {
				radius = VectorLength( verts[j].xyz );
			}

			// the normal is two angles, as in Quake III's models
			lat = ( ( normal >> 8 ) & 0xff ) * ( 2 * M_PI / 255.0f );
			lng = ( normal & 0xff ) * ( 2 * M_PI / 255.0f );
			verts[j].normal[0] = cos( lat ) * sin( lng );
			verts[j].normal[1] = sin( lat ) * sin( lng );
			verts[j].normal[2] = cos( lng );

			verts[j].st[0] = TAN_Float( st );
			verts[j].st[1] = TAN_Float( st + 4 );
			verts[j].lightmap[0] = verts[j].lightmap[1] = 0;
			verts[j].color[0] = verts[j].color[1] = verts[j].color[2] = verts[j].color[3] = 255;
		}

		k = 0;
		for ( j = 0 ; j < numTris * 3 ; j++ ) {
			int		index = TAN_Int( surf + ofsTris + j * 4 );

			if ( index < 0 || index >= numVerts ) {
				k = 1;
				break;
			}
			tris[j] = index;
		}
		if ( k ) {
			ri.Printf( PRINT_WARNING, "RE_RegisterTan: %s has a triangle outside its vertexes\n", tanFile );
			break;
		}

		// hand the triangles over in pieces small enough to draw
		firstTri = 0;
		while ( firstTri < numTris && numPieces < ARRAY_LEN( pieces ) ) {
			static byte	seen[TAN_MAX_VERTS];
			int			count = 0, used = 0;

			Com_Memset( seen, 0, sizeof( seen ) );
			while ( firstTri + count < numTris && ( count + 1 ) * 3 <= TAN_PIECE_INDEXES ) {
				int		fresh = 0;

				for ( j = 0 ; j < 3 ; j++ ) {
					if ( !seen[tris[( firstTri + count ) * 3 + j]] ) {
						fresh++;
					}
				}
				if ( used + fresh > TAN_PIECE_VERTS ) {
					break;
				}
				for ( j = 0 ; j < 3 ; j++ ) {
					seen[tris[( firstTri + count ) * 3 + j]] = 1;
				}
				used += fresh;
				count++;
			}
			pieces[numPieces] = R_TanAddPiece( verts, numVerts, tris, firstTri, count );
			shaders[numPieces] = sh;
			numPieces++;
			firstTri += count;
		}

		surf += ofsEnd;
	}

	ri.FS_FreeFile( buffer.v );

	if ( !numPieces ) {
		return 0;
	}

	tan = ri.Hunk_Alloc( sizeof( *tan ) + numPieces * ( sizeof( *tan->pieces ) + sizeof( *tan->shaders ) ), h_low );
	tan->numPieces = numPieces;
	tan->radius = radius;
	tan->pieces = (srfTriangles_t **)( tan + 1 );
	tan->shaders = (shader_t **)( tan->pieces + numPieces );
	Com_Memcpy( tan->pieces, pieces, numPieces * sizeof( *tan->pieces ) );
	Com_Memcpy( tan->shaders, shaders, numPieces * sizeof( *tan->shaders ) );

	mod->type = MOD_TAN;
	mod->modelData = tan;
	return mod->index;
}

/*
===============
R_TanFarthestPoint

How far from the viewer the model placed as ent can reach. The world sets
the far clipping distance from its own bounds; a model outside them, such as
a sky dome, has to be counted too.
===============
*/
float R_TanFarthestPoint( const trRefEntity_t *ent, const model_t *mod ) {
	const tanModel_t	*tan = mod->modelData;
	vec3_t				delta;
	float				scale = 1;

	if ( ent->e.nonNormalizedAxes ) {
		scale = VectorLength( ent->e.axis[0] );
	}
	VectorSubtract( ent->e.origin, tr.viewParms.or.origin, delta );
	return VectorLength( delta ) + tan->radius * scale;
}

/*
===============
R_AddTanSurfaces
===============
*/
void R_AddTanSurfaces( trRefEntity_t *ent ) {
	tanModel_t	*tan = tr.currentModel->modelData;
	int			i;

	// don't add third_person objects if not in a portal
	if ( ( ent->e.renderfx & RF_THIRD_PERSON ) && !tr.viewParms.isPortal ) {
		return;
	}

	if ( !( tr.refdef.rdflags & RDF_NOWORLDMODEL ) ) {
		R_SetupEntityLighting( &tr.refdef, ent );
	}

	for ( i = 0 ; i < tan->numPieces ; i++ ) {
		shader_t	*sh = tan->shaders[i];

		if ( ent->e.customShader ) {
			sh = R_GetShaderByHandle( ent->e.customShader );
		}
		R_AddDrawSurf( (void *)tan->pieces[i], sh, 0, qfalse );
	}
}
