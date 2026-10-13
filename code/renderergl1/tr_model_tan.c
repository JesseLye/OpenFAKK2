/*
===========================================================================
FAKK2 models from their .tik definitions.

A .tik definition names the model's animations, its scale and the shader of
each surface; the engine's TIKI code reads that and hands it to
RE_RegisterTiki. Vertex animations (.tan files) are loaded here the first
time an animation is drawn, and the frames the entity asks for are
interpolated the way Quake III interpolates MD3 frames. Level of detail,
tags, cross-blending and torso animations are not handled, and skeletal
models (.skb) register but draw nothing.
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

typedef struct {
	vec3_t		bounds[2];		// of the model as placed, after scaling
	vec3_t		localOrigin;
	float		radius;
	vec3_t		scale;			// decoded vertex = packed * scale + offset
	vec3_t		offset;
} tanFrame_t;

typedef struct tanAnim_s tanAnim_t;

typedef struct {
	surfaceType_t	surfaceType;	// SF_TAN
	tanAnim_t		*anim;
	int				surfaceNum;		// index in the definition's surfaces, -1 if not named there
	int				numVerts;
	int				numTris;
	int				*indexes;		// numTris * 3
	float			*st;			// numVerts * 2
	unsigned short	*xyz;			// numFrames * numVerts * 4: packed x, y, z and the normal
} tanSurface_t;

struct tanAnim_s {
	int				numFrames;
	tanFrame_t		*frames;
	int				numSurfaces;
	tanSurface_t	*surfaces;
	float			radius;			// farthest any frame reaches from the origin
};

typedef struct {
	char		path[MAX_QPATH];
	float		scale;
	vec3_t		origin;
	qboolean	skeletal;
	vec3_t		mins, maxs;
	float		radius;

	int			numAnims;
	char		**animFiles;
	tanAnim_t	**anims;		// loaded on first use; tanFailed when the file is bad

	int			numSurfaces;
	char		(*surfaceNames)[MAX_QPATH];
	shader_t	**shaders;		// numSurfaces * TIKI_MODEL_MAX_SKINS, NULL = hidden
} tikiModel_t;

static tanAnim_t	tanFailed;

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
R_TikiShader

A surface's shader is either a shader script's name or an image beside the
model.
===============
*/
static shader_t *R_TikiShader( const char *name, const char *path ) {
	char		stripped[MAX_QPATH], local[MAX_QPATH];
	shader_t	*sh;

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
RE_RegisterTiki
===============
*/
qhandle_t RE_RegisterTiki( const tikiModelDesc_t *desc ) {
	model_t		*mod;
	tikiModel_t	*tiki;
	qhandle_t	hModel;
	int			i, j;

	if ( !desc || !desc->name || !desc->name[0] || strlen( desc->name ) >= MAX_QPATH ) {
		return 0;
	}

	// search the currently loaded models
	for ( hModel = 1 ; hModel < tr.numModels ; hModel++ ) {
		mod = tr.models[hModel];
		if ( !strcmp( mod->name, desc->name ) ) {
			return mod->type == MOD_BAD ? 0 : hModel;
		}
	}

	if ( ( mod = R_AllocModel() ) == NULL ) {
		ri.Printf( PRINT_WARNING, "RE_RegisterTiki: R_AllocModel() failed for '%s'\n", desc->name );
		return 0;
	}
	Q_strncpyz( mod->name, desc->name, sizeof( mod->name ) );
	mod->type = MOD_BAD;

	if ( desc->numAnims < 0 || desc->numSurfaces < 0 || desc->numSurfaces > TAN_MAX_SURFACES ) {
		return 0;
	}

	tiki = ri.Hunk_Alloc( sizeof( *tiki ), h_low );
	if ( desc->path ) {
		Q_strncpyz( tiki->path, desc->path, sizeof( tiki->path ) );
	}
	tiki->scale = desc->scale ? desc->scale : 1;
	VectorCopy( desc->origin, tiki->origin );
	tiki->skeletal = desc->skeletal;
	VectorCopy( desc->mins, tiki->mins );
	VectorCopy( desc->maxs, tiki->maxs );
	tiki->radius = desc->radius;

	tiki->numAnims = desc->numAnims;
	if ( desc->numAnims ) {
		tiki->animFiles = ri.Hunk_Alloc( desc->numAnims * sizeof( *tiki->animFiles ), h_low );
		tiki->anims = ri.Hunk_Alloc( desc->numAnims * sizeof( *tiki->anims ), h_low );
		for ( i = 0 ; i < desc->numAnims ; i++ ) {
			const char	*file = desc->animFiles[i] ? desc->animFiles[i] : "";

			tiki->animFiles[i] = ri.Hunk_Alloc( strlen( file ) + 1, h_low );
			strcpy( tiki->animFiles[i], file );
		}
	}

	tiki->numSurfaces = desc->numSurfaces;
	if ( desc->numSurfaces ) {
		tiki->surfaceNames = ri.Hunk_Alloc( desc->numSurfaces * sizeof( *tiki->surfaceNames ), h_low );
		tiki->shaders = ri.Hunk_Alloc( desc->numSurfaces * TIKI_MODEL_MAX_SKINS * sizeof( *tiki->shaders ), h_low );
		for ( i = 0 ; i < desc->numSurfaces ; i++ ) {
			Q_strncpyz( tiki->surfaceNames[i], desc->surfaceNames[i] ? desc->surfaceNames[i] : "", MAX_QPATH );
			for ( j = 0 ; j < TIKI_MODEL_MAX_SKINS ; j++ ) {
				const char	*shaderName = desc->shaderNames[i * TIKI_MODEL_MAX_SKINS + j];

				if ( shaderName && shaderName[0] ) {
					tiki->shaders[i * TIKI_MODEL_MAX_SKINS + j] = R_TikiShader( shaderName, tiki->path );
				}
			}
		}
	}

	mod->type = MOD_TIKI;
	mod->modelData = tiki;
	return mod->index;
}

/*
===============
R_TanLoad

Reads one animation file. Vertexes stay packed as the file has them; the
frame's scale and offset, with the model's own scale and origin folded in,
decode them when they are drawn.
===============
*/
static tanAnim_t *R_TanLoad( const tikiModel_t *tiki, const char *tanFile ) {
	union {
		byte	*b;
		void	*v;
	} buffer;
	tanAnim_t	*anim;
	const byte	*data, *surf;
	int			length, numFrames, numSurfaces, ofsFrames, ofsSurfaces;
	int			i, j, k;

	if ( !tanFile[0] ) {
		return NULL;
	}
	length = ri.FS_ReadFile( tanFile, &buffer.v );
	if ( !buffer.b || length < TAN_HEADER_SIZE ) {
		if ( buffer.b ) {
			ri.FS_FreeFile( buffer.v );
		}
		ri.Printf( PRINT_WARNING, "R_TanLoad: couldn't load %s\n", tanFile );
		return NULL;
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
		ri.Printf( PRINT_WARNING, "R_TanLoad: %s is not a version %i TAN file\n", tanFile, TAN_VERSION );
		return NULL;
	}

	anim = ri.Hunk_Alloc( sizeof( *anim ) + numFrames * sizeof( tanFrame_t ) + numSurfaces * sizeof( tanSurface_t ), h_low );
	anim->numFrames = numFrames;
	anim->frames = (tanFrame_t *)( anim + 1 );
	anim->surfaces = (tanSurface_t *)( anim->frames + numFrames );

	for ( i = 0 ; i < numFrames ; i++ ) {
		const byte	*f = data + ofsFrames + i * TAN_FRAME_SIZE;
		tanFrame_t	*frame = &anim->frames[i];

		for ( k = 0 ; k < 3 ; k++ ) {
			frame->scale[k] = TAN_Float( f + 24 + k * 4 ) * tiki->scale;
			frame->offset[k] = TAN_Float( f + 36 + k * 4 ) * tiki->scale + tiki->origin[k];
		}
		ClearBounds( frame->bounds[0], frame->bounds[1] );
	}

	surf = data + ofsSurfaces;
	for ( i = 0 ; i < numSurfaces ; i++ ) {
		tanSurface_t	*out = &anim->surfaces[anim->numSurfaces];
		const char		*surfName;
		int				surfFrames, numVerts, numTris, ofsTris, ofsSt, ofsXyz, ofsEnd;
		int				surfLength = length - (int)( surf - data );
		qboolean		bad = qfalse;

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
			ri.Printf( PRINT_WARNING, "R_TanLoad: %s has a malformed surface\n", tanFile );
			break;
		}

		out->surfaceType = SF_TAN;
		out->anim = anim;
		out->surfaceNum = -1;
		for ( j = 0 ; j < tiki->numSurfaces ; j++ ) {
			if ( !Q_stricmp( tiki->surfaceNames[j], surfName ) ) {
				out->surfaceNum = j;
				break;
			}
		}
		out->numVerts = numVerts;
		out->numTris = numTris;
		out->indexes = ri.Hunk_Alloc( numTris * 3 * sizeof( int ), h_low );
		out->st = ri.Hunk_Alloc( numVerts * 2 * sizeof( float ), h_low );
		out->xyz = ri.Hunk_Alloc( numFrames * numVerts * 4 * sizeof( unsigned short ), h_low );

		for ( j = 0 ; j < numTris * 3 ; j++ ) {
			int		index = TAN_Int( surf + ofsTris + j * 4 );

			if ( index < 0 || index >= numVerts ) {
				bad = qtrue;
				break;
			}
			out->indexes[j] = index;
		}
		if ( bad ) {
			ri.Printf( PRINT_WARNING, "R_TanLoad: %s has a triangle outside its vertexes\n", tanFile );
			break;
		}
		for ( j = 0 ; j < numVerts ; j++ ) {
			out->st[j * 2] = TAN_Float( surf + ofsSt + j * 8 );
			out->st[j * 2 + 1] = TAN_Float( surf + ofsSt + j * 8 + 4 );
		}
		for ( j = 0 ; j < numFrames * numVerts ; j++ ) {
			const byte		*packed = surf + ofsXyz + j * 8;
			unsigned short	*xyz = out->xyz + j * 4;
			tanFrame_t		*frame = &anim->frames[j / numVerts];
			vec3_t			point;

			for ( k = 0 ; k < 4 ; k++ ) {
				xyz[k] = packed[k * 2] | ( packed[k * 2 + 1] << 8 );
			}
			for ( k = 0 ; k < 3 ; k++ ) {
				point[k] = xyz[k] * frame->scale[k] + frame->offset[k];
			}
			AddPointToBounds( point, frame->bounds[0], frame->bounds[1] );
		}

		anim->numSurfaces++;
		surf += ofsEnd;
	}

	ri.FS_FreeFile( buffer.v );

	for ( i = 0 ; i < numFrames ; i++ ) {
		tanFrame_t	*frame = &anim->frames[i];
		vec3_t		corner;

		if ( frame->bounds[0][0] > frame->bounds[1][0] ) {
			VectorClear( frame->bounds[0] );
			VectorClear( frame->bounds[1] );
		}
		VectorAdd( frame->bounds[0], frame->bounds[1], frame->localOrigin );
		VectorScale( frame->localOrigin, 0.5f, frame->localOrigin );
		VectorSubtract( frame->bounds[1], frame->localOrigin, corner );
		frame->radius = VectorLength( corner );
		if ( VectorLength( frame->localOrigin ) + frame->radius > anim->radius ) {
			anim->radius = VectorLength( frame->localOrigin ) + frame->radius;
		}
	}

	if ( !anim->numSurfaces ) {
		return NULL;
	}
	return anim;
}

/*
===============
R_TikiAnim

The animation an entity shows, loading it on first use. An animation the
definition does not have, or whose file is bad, falls back to the first.
===============
*/
static tanAnim_t *R_TikiAnim( tikiModel_t *tiki, int animnum ) {
	tanAnim_t	*anim;

	if ( tiki->skeletal || !tiki->numAnims ) {
		return NULL;
	}
	if ( animnum < 0 || animnum >= tiki->numAnims ) {
		animnum = 0;
	}
	anim = tiki->anims[animnum];
	if ( !anim ) {
		anim = R_TanLoad( tiki, tiki->animFiles[animnum] );
		tiki->anims[animnum] = anim ? anim : &tanFailed;
	}
	if ( anim == &tanFailed ) {
		anim = NULL;
	}
	if ( !anim && animnum != 0 ) {
		return R_TikiAnim( tiki, 0 );
	}
	return anim;
}

/*
===============
R_TikiFarthestPoint

How far from the viewer the model placed as ent can reach. The world sets
the far clipping distance from its own bounds; a model outside them, such as
a sky dome, has to be counted too.
===============
*/
float R_TikiFarthestPoint( const trRefEntity_t *ent, const model_t *mod ) {
	tikiModel_t	*tiki = mod->modelData;
	tanAnim_t	*anim = R_TikiAnim( tiki, ent->e.anim );
	vec3_t		delta;
	float		scale = 1, radius;

	radius = anim ? anim->radius : tiki->radius;
	if ( ent->e.nonNormalizedAxes ) {
		scale = VectorLength( ent->e.axis[0] );
	}
	VectorSubtract( ent->e.origin, tr.viewParms.or.origin, delta );
	return VectorLength( delta ) + radius * scale;
}

void R_TikiBounds( const model_t *mod, vec3_t mins, vec3_t maxs ) {
	const tikiModel_t	*tiki = mod->modelData;

	VectorCopy( tiki->mins, mins );
	VectorCopy( tiki->maxs, maxs );
}

float R_TikiRadius( const model_t *mod ) {
	const tikiModel_t	*tiki = mod->modelData;

	return tiki->radius;
}

/*
===============
R_TikiCull

Culls with the frames' bounds, as Quake III does for MD3 models.
===============
*/
static int R_TikiCull( const tanAnim_t *anim, const trRefEntity_t *ent ) {
	const tanFrame_t	*newFrame = &anim->frames[ent->e.frame];
	const tanFrame_t	*oldFrame = &anim->frames[ent->e.oldframe];
	vec3_t				bounds[2];
	int					i;

	if ( !ent->e.nonNormalizedAxes ) {
		int		sphereCull = R_CullLocalPointAndRadius( newFrame->localOrigin, newFrame->radius );

		if ( newFrame != oldFrame ) {
			int		sphereCullB = R_CullLocalPointAndRadius( oldFrame->localOrigin, oldFrame->radius );

			if ( sphereCull != sphereCullB ) {
				sphereCull = CULL_CLIP;
			}
		}
		if ( sphereCull != CULL_CLIP ) {
			return sphereCull;
		}
	}

	for ( i = 0 ; i < 3 ; i++ ) {
		bounds[0][i] = oldFrame->bounds[0][i] < newFrame->bounds[0][i] ? oldFrame->bounds[0][i] : newFrame->bounds[0][i];
		bounds[1][i] = oldFrame->bounds[1][i] > newFrame->bounds[1][i] ? oldFrame->bounds[1][i] : newFrame->bounds[1][i];
	}
	return R_CullLocalBox( bounds );
}

/*
===============
R_AddTikiSurfaces
===============
*/
void R_AddTikiSurfaces( trRefEntity_t *ent ) {
	tikiModel_t	*tiki = tr.currentModel->modelData;
	tanAnim_t	*anim;
	int			i;

	// don't add third_person objects if not in a portal
	if ( ( ent->e.renderfx & RF_THIRD_PERSON ) && !tr.viewParms.isPortal ) {
		return;
	}

	anim = R_TikiAnim( tiki, ent->e.anim );
	if ( !anim ) {
		return;
	}

	// an old frame from another animation means nothing here
	if ( ent->e.oldanim != ent->e.anim ) {
		ent->e.oldframe = ent->e.frame;
	}
	if ( ent->e.frame < 0 || ent->e.frame >= anim->numFrames
		|| ent->e.oldframe < 0 || ent->e.oldframe >= anim->numFrames ) {
		ri.Printf( PRINT_DEVELOPER, "R_AddTikiSurfaces: no such frame %d to %d for '%s'\n",
			ent->e.oldframe, ent->e.frame, tr.currentModel->name );
		ent->e.frame = 0;
		ent->e.oldframe = 0;
	}

	if ( R_TikiCull( anim, ent ) == CULL_OUT ) {
		return;
	}

	if ( !( tr.refdef.rdflags & RDF_NOWORLDMODEL ) ) {
		R_SetupEntityLighting( &tr.refdef, ent );
	}

	for ( i = 0 ; i < anim->numSurfaces ; i++ ) {
		tanSurface_t	*surf = &anim->surfaces[i];
		shader_t		*sh;

		if ( ent->e.customShader ) {
			sh = R_GetShaderByHandle( ent->e.customShader );
		} else if ( surf->surfaceNum < 0 ) {
			sh = tr.defaultShader;
		} else {
			int		flags = ent->e.surfaces[surf->surfaceNum];
			int		skin = flags & ( MDL_SURFACE_SKINOFFSET_BIT0 | MDL_SURFACE_SKINOFFSET_BIT1 );

			if ( flags & MDL_SURFACE_NODRAW ) {
				continue;
			}
			sh = tiki->shaders[surf->surfaceNum * TIKI_MODEL_MAX_SKINS + skin];
			if ( !sh && skin ) {
				sh = tiki->shaders[surf->surfaceNum * TIKI_MODEL_MAX_SKINS];
			}
			if ( !sh ) {
				continue;		// hidden by the definition
			}
		}
		R_AddDrawSurf( (void *)surf, sh, 0, qfalse );
	}
}

/*
===============
RB_SurfaceTan

Decodes and interpolates the entity's two frames into the tesselator, as
RB_SurfaceMesh does for MD3 surfaces.
===============
*/
void RB_SurfaceTan( void *surface ) {
	tanSurface_t			*surf = surface;
	const refEntity_t		*e = &backEnd.currentEntity->e;
	const tanFrame_t		*newFrame = &surf->anim->frames[e->frame];
	const tanFrame_t		*oldFrame = &surf->anim->frames[e->oldframe];
	const unsigned short	*newXyz, *oldXyz;
	float					*outXyz, *outNormal;
	float					backlerp, frontlerp;
	int						i, j, numVerts, numIndexes, baseVertex, baseIndex;

	backlerp = e->oldframe == e->frame ? 0 : e->backlerp;
	frontlerp = 1.0f - backlerp;

	RB_CHECKOVERFLOW( surf->numVerts, surf->numTris * 3 );

	numVerts = surf->numVerts;
	numIndexes = surf->numTris * 3;
	baseVertex = tess.numVertexes;
	baseIndex = tess.numIndexes;

	newXyz = surf->xyz + e->frame * numVerts * 4;
	oldXyz = surf->xyz + e->oldframe * numVerts * 4;
	outXyz = tess.xyz[baseVertex];
	outNormal = tess.normal[baseVertex];

	for ( i = 0 ; i < numVerts ; i++, newXyz += 4, oldXyz += 4, outXyz += 4, outNormal += 4 ) {
		unsigned	lat, lng;
		vec3_t		newNormal;

		for ( j = 0 ; j < 3 ; j++ ) {
			outXyz[j] = newXyz[j] * newFrame->scale[j] + newFrame->offset[j];
		}
		// the normal is two angles, as in Quake III's models
		lat = ( ( newXyz[3] >> 8 ) & 0xff ) * ( FUNCTABLE_SIZE / 256 );
		lng = ( newXyz[3] & 0xff ) * ( FUNCTABLE_SIZE / 256 );
		newNormal[0] = tr.sinTable[( lat + ( FUNCTABLE_SIZE / 4 ) ) & FUNCTABLE_MASK] * tr.sinTable[lng];
		newNormal[1] = tr.sinTable[lat] * tr.sinTable[lng];
		newNormal[2] = tr.sinTable[( lng + ( FUNCTABLE_SIZE / 4 ) ) & FUNCTABLE_MASK];

		if ( backlerp == 0 ) {
			VectorCopy( newNormal, outNormal );
			continue;
		}

		for ( j = 0 ; j < 3 ; j++ ) {
			outXyz[j] = outXyz[j] * frontlerp + ( oldXyz[j] * oldFrame->scale[j] + oldFrame->offset[j] ) * backlerp;
		}
		lat = ( ( oldXyz[3] >> 8 ) & 0xff ) * ( FUNCTABLE_SIZE / 256 );
		lng = ( oldXyz[3] & 0xff ) * ( FUNCTABLE_SIZE / 256 );
		outNormal[0] = newNormal[0] * frontlerp + tr.sinTable[( lat + ( FUNCTABLE_SIZE / 4 ) ) & FUNCTABLE_MASK] * tr.sinTable[lng] * backlerp;
		outNormal[1] = newNormal[1] * frontlerp + tr.sinTable[lat] * tr.sinTable[lng] * backlerp;
		outNormal[2] = newNormal[2] * frontlerp + tr.sinTable[( lng + ( FUNCTABLE_SIZE / 4 ) ) & FUNCTABLE_MASK] * backlerp;
		VectorNormalizeFast( outNormal );
	}

	for ( i = 0 ; i < numIndexes ; i++ ) {
		tess.indexes[baseIndex + i] = baseVertex + surf->indexes[i];
	}
	for ( i = 0 ; i < numVerts ; i++ ) {
		tess.texCoords[baseVertex + i][0][0] = surf->st[i * 2];
		tess.texCoords[baseVertex + i][0][1] = surf->st[i * 2 + 1];
	}

	tess.numVertexes += numVerts;
	tess.numIndexes += numIndexes;
}
