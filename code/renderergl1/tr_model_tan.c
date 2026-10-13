/*
===========================================================================
FAKK2 models from their .tik definitions.

A .tik definition names the model's animations, its scale and the shader of
each surface; the engine's TIKI code reads that and hands it to
RE_RegisterTiki. Animation files are loaded here the first time an animation
is drawn.

Vertex animations (.tan): the entity's frame and old frame are decoded and
interpolated the way Quake III interpolates MD3 frames.

Skeletal models (.skb with .ska animations): the skeleton's surfaces hold
vertexes weighted to bones; every frame the bones are composed from the
animation's per-bone quaternions and offsets, root first, and the vertexes
are skinned on the CPU. The composition follows the retail engine's
TIKI_Tag_Orientation (fakk2.exe 0x44fe40): a bone's rotation is its
quaternion times its parent's, in that order, then the entity's bone
controllers; its origin is the parent's origin plus the bone's offset turned
by the parent's axes; the root gets the definition's origin; everything is
scaled by the definition's scale at the end. The retail renderer's own
skinning has not been read: normals are turned with the vertex's bones and
frames are interpolated by normalised quaternion lerp, which are assumptions.

Level of detail, tags, cross-blending and torso animations are not handled.
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

#define	SKB_IDENT				((' '<<24)+('L'<<16)+('K'<<8)+'S')	// "SKL "
#define	SKA_IDENT				(('N'<<24)+('A'<<16)+('K'<<8)+'S')	// "SKAN"
#define	SKEL_VERSION			3
#define	SKB_HEADER_SIZE			92
#define	SKB_BONE_SIZE			72
#define	SKB_SURFACE_HEADER_SIZE	96
#define	SKB_VERT_SIZE			24		// before its weights
#define	SKB_WEIGHT_SIZE			20
#define	SKA_HEADER_SIZE			108
#define	SKA_FRAME_HEADER_SIZE	40
#define	SKA_BONE_SIZE			16
#define	SKEL_MAX_BONES			100
#define	SKEL_MAX_SURFACES		32
#define	SKEL_MAX_VERTS			4000
#define	SKEL_MAX_TRIANGLES		8000
#define	SKEL_MAX_WEIGHTS		8

// a surface is cut into pieces the tesselator can take in one go
#define	PIECE_VERTS				( SHADER_MAX_VERTEXES - 100 )
#define	PIECE_INDEXES			( SHADER_MAX_INDEXES - 600 )

#define	SKEL_QUAT_SCALE			( 1.0f / 32768.0f )
#define	SKEL_OFFSET_SCALE		( 1.0f / 63.0f )

typedef struct {
	vec3_t		bounds[2];		// of the model as placed, after scaling
	vec3_t		localOrigin;
	float		radius;
	vec3_t		scale;			// tan: decoded vertex = packed * scale + offset
	vec3_t		offset;
} tikiFrame_t;

typedef struct tikiModel_s tikiModel_t;
typedef struct tanAnim_s tanAnim_t;
typedef struct skelAnim_s skelAnim_t;

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
	tikiFrame_t		*frames;
	int				numSurfaces;
	tanSurface_t	*surfaces;
	float			radius;			// farthest any frame reaches from the origin
};

typedef struct {
	int			bone;
	float		weight;
	vec3_t		offset;			// in the bone's frame
} skelWeight_t;

typedef struct {
	vec3_t		normal;
	float		st[2];
	int			numWeights;
	int			firstWeight;
} skelVert_t;

typedef struct {
	surfaceType_t	surfaceType;	// SF_SKEL
	tikiModel_t		*model;
	int				surfaceNum;
	int				numVerts;
	int				numTris;
	skelVert_t		*verts;
	skelWeight_t	*weights;
	int				*indexes;
} skelSurface_t;

typedef struct {
	int				numBones;
	int				*parents;		// -1 for a root; always earlier than the bone
	qboolean		*legBone;		// takes the legs animation when a torso animation plays too
	int				numSurfaces;
	skelSurface_t	*surfaces;
} skelModel_t;

struct skelAnim_s {
	int				numFrames;
	int				numBones;
	tikiFrame_t		*frames;
	short			*bones;			// [frame][bone]: quaternion x y z w, offset x y z, padding
	float			radius;
};

struct tikiModel_s {
	char		path[MAX_QPATH];
	float		scale;
	vec3_t		origin;
	qboolean	skeletal;
	vec3_t		mins, maxs;
	float		radius;

	int			numAnims;
	char		**animFiles;
	tanAnim_t	**anims;		// loaded on first use; tanFailed when the file is bad
	skelAnim_t	**skelAnims;	// likewise, for skeletal models

	skelModel_t	*skel;

	int			numSurfaces;
	char		(*surfaceNames)[MAX_QPATH];
	shader_t	**shaders;		// numSurfaces * TIKI_MODEL_MAX_SKINS, NULL = hidden
};

static tanAnim_t	tanFailed;
static skelAnim_t	skelFailed;

static int TAN_Int( const byte *p ) {
	return (int)( p[0] | ( p[1] << 8 ) | ( p[2] << 16 ) | ( (unsigned)p[3] << 24 ) );
}

static float TAN_Float( const byte *p ) {
	floatint_t	fi;

	fi.i = TAN_Int( p );
	return fi.f;
}

static short TAN_Short( const byte *p ) {
	return (short)( p[0] | ( p[1] << 8 ) );
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

static int R_TikiSurfaceNum( const tikiModel_t *tiki, const char *name ) {
	int		i;

	for ( i = 0 ; i < tiki->numSurfaces ; i++ ) {
		if ( !Q_stricmp( tiki->surfaceNames[i], name ) ) {
			return i;
		}
	}
	return -1;
}

/*
===============
R_TikiFinishFrame

Fills in a frame's centre and radius from its bounds, and widens the
animation's reach.
===============
*/
static void R_TikiFinishFrame( tikiFrame_t *frame, float *animRadius ) {
	vec3_t	corner;

	if ( frame->bounds[0][0] > frame->bounds[1][0] ) {
		VectorClear( frame->bounds[0] );
		VectorClear( frame->bounds[1] );
	}
	VectorAdd( frame->bounds[0], frame->bounds[1], frame->localOrigin );
	VectorScale( frame->localOrigin, 0.5f, frame->localOrigin );
	VectorSubtract( frame->bounds[1], frame->localOrigin, corner );
	frame->radius = VectorLength( corner );
	if ( VectorLength( frame->localOrigin ) + frame->radius > *animRadius ) {
		*animRadius = VectorLength( frame->localOrigin ) + frame->radius;
	}
}

/*
===============
R_SkelLoad

Reads a skeleton: the bones' parents and the surfaces' weighted vertexes.
Surfaces too big for the tesselator are cut into pieces.
===============
*/
static skelModel_t *R_SkelLoad( tikiModel_t *tiki, const char *skbFile ) {
	static skelVert_t	verts[SKEL_MAX_VERTS];
	static skelWeight_t	weights[SKEL_MAX_VERTS * SKEL_MAX_WEIGHTS];
	static int			tris[SKEL_MAX_TRIANGLES * 3];
	static int			remap[SKEL_MAX_VERTS];
	static byte			seen[SKEL_MAX_VERTS];
	skelSurface_t		pieces[SKEL_MAX_SURFACES * 8];
	union {
		byte	*b;
		void	*v;
	} buffer;
	skelModel_t	*skel;
	const byte	*data, *surf;
	int			length, numSurfaces, numBones, ofsBones, ofsSurfaces;
	int			numPieces = 0;
	int			i, j, k;

	if ( !skbFile || !skbFile[0] ) {
		return NULL;
	}
	length = ri.FS_ReadFile( skbFile, &buffer.v );
	if ( !buffer.b || length < SKB_HEADER_SIZE ) {
		if ( buffer.b ) {
			ri.FS_FreeFile( buffer.v );
		}
		ri.Printf( PRINT_WARNING, "R_SkelLoad: couldn't load %s\n", skbFile );
		return NULL;
	}
	data = buffer.b;

	numSurfaces = TAN_Int( data + 72 );
	numBones = TAN_Int( data + 76 );
	ofsBones = TAN_Int( data + 80 );
	ofsSurfaces = TAN_Int( data + 84 );
	if ( TAN_Int( data ) != SKB_IDENT || TAN_Int( data + 4 ) != SKEL_VERSION
		|| numSurfaces < 0 || numSurfaces > SKEL_MAX_SURFACES || numBones < 0 || numBones > SKEL_MAX_BONES
		|| !TAN_InFile( length, ofsBones, numBones * SKB_BONE_SIZE ) ) {
		ri.FS_FreeFile( buffer.v );
		ri.Printf( PRINT_WARNING, "R_SkelLoad: %s is not a version %i SKB file\n", skbFile, SKEL_VERSION );
		return NULL;
	}

	skel = ri.Hunk_Alloc( sizeof( *skel ) + numBones * ( sizeof( int ) + sizeof( qboolean ) ), h_low );
	skel->numBones = numBones;
	skel->parents = (int *)( skel + 1 );
	skel->legBone = (qboolean *)( skel->parents + numBones );
	for ( i = 0 ; i < numBones ; i++ ) {
		const byte	*bone = data + ofsBones + i * SKB_BONE_SIZE;
		int			parent = TAN_Int( bone );

		// a bone may only hang off an earlier bone, which keeps evaluation a single pass
		skel->parents[i] = ( parent >= 0 && parent < i ) ? parent : -1;
		// retail's R_AddSkelSurfaces (0x4872d0) animates a bone whose flags are
		// exactly 1 from the legs animation and every other from the torso's
		skel->legBone[i] = TAN_Int( bone + 4 ) == 1;
	}

	surf = data + ofsSurfaces;
	for ( i = 0 ; i < numSurfaces ; i++ ) {
		const char	*surfName;
		int			numTris, numVerts, ofsTris, ofsVerts, ofsEnd;
		int			surfLength = length - (int)( surf - data );
		int			numWeights = 0, surfaceNum, firstTri;
		const byte	*p;
		qboolean	bad = qfalse;

		if ( surfLength < SKB_SURFACE_HEADER_SIZE ) {
			break;
		}
		surfName = (const char *)surf + 4;
		numTris = TAN_Int( surf + 68 );
		numVerts = TAN_Int( surf + 72 );
		ofsTris = TAN_Int( surf + 80 );
		ofsVerts = TAN_Int( surf + 84 );
		ofsEnd = TAN_Int( surf + 92 );

		if ( numVerts < 0 || numVerts > SKEL_MAX_VERTS || numTris < 0 || numTris > SKEL_MAX_TRIANGLES
			|| !memchr( surfName, 0, MAX_QPATH ) || !TAN_InFile( surfLength, ofsTris, numTris * 12 )
			|| !TAN_InFile( surfLength, ofsVerts, numVerts * SKB_VERT_SIZE )
			|| ofsEnd < SKB_SURFACE_HEADER_SIZE || ofsEnd > surfLength ) {
			ri.Printf( PRINT_WARNING, "R_SkelLoad: %s has a malformed surface\n", skbFile );
			break;
		}

		p = surf + ofsVerts;
		for ( j = 0 ; j < numVerts && !bad ; j++ ) {
			skelVert_t	*v = &verts[j];
			int			n;

			if ( !TAN_InFile( surfLength, (int)( p - surf ), SKB_VERT_SIZE ) ) {
				bad = qtrue;
				break;
			}
			for ( k = 0 ; k < 3 ; k++ ) {
				v->normal[k] = TAN_Float( p + k * 4 );
			}
			v->st[0] = TAN_Float( p + 12 );
			v->st[1] = TAN_Float( p + 16 );
			n = TAN_Int( p + 20 );
			p += SKB_VERT_SIZE;
			if ( n < 0 || n > SKEL_MAX_WEIGHTS || !TAN_InFile( surfLength, (int)( p - surf ), n * SKB_WEIGHT_SIZE ) ) {
				bad = qtrue;
				break;
			}
			v->numWeights = n;
			v->firstWeight = numWeights;
			for ( k = 0 ; k < n ; k++, p += SKB_WEIGHT_SIZE ) {
				skelWeight_t	*w = &weights[numWeights++];

				w->bone = TAN_Int( p );
				w->weight = TAN_Float( p + 4 );
				w->offset[0] = TAN_Float( p + 8 );
				w->offset[1] = TAN_Float( p + 12 );
				w->offset[2] = TAN_Float( p + 16 );
				if ( w->bone < 0 || w->bone >= numBones ) {
					w->bone = 0;
				}
			}
		}
		for ( j = 0 ; j < numTris * 3 && !bad ; j++ ) {
			int		index = TAN_Int( surf + ofsTris + j * 4 );

			if ( index < 0 || index >= numVerts ) {
				bad = qtrue;
				break;
			}
			tris[j] = index;
		}
		if ( bad ) {
			ri.Printf( PRINT_WARNING, "R_SkelLoad: %s has a malformed surface %s\n", skbFile, surfName );
			break;
		}

		surfaceNum = R_TikiSurfaceNum( tiki, surfName );

		// hand the triangles over in pieces small enough to draw
		firstTri = 0;
		while ( firstTri < numTris && numPieces < ARRAY_LEN( pieces ) ) {
			skelSurface_t	*piece = &pieces[numPieces];
			int				count = 0, used = 0, pieceWeights = 0;

			Com_Memset( seen, 0, numVerts );
			while ( firstTri + count < numTris && ( count + 1 ) * 3 <= PIECE_INDEXES ) {
				int		fresh = 0;

				for ( j = 0 ; j < 3 ; j++ ) {
					if ( !seen[tris[( firstTri + count ) * 3 + j]] ) {
						fresh++;
					}
				}
				if ( used + fresh > PIECE_VERTS ) {
					break;
				}
				for ( j = 0 ; j < 3 ; j++ ) {
					seen[tris[( firstTri + count ) * 3 + j]] = 1;
				}
				used += fresh;
				count++;
			}
			if ( !count ) {
				break;
			}

			used = 0;
			for ( j = 0 ; j < numVerts ; j++ ) {
				remap[j] = -1;
				if ( seen[j] ) {
					remap[j] = used++;
					pieceWeights += verts[j].numWeights;
				}
			}

			Com_Memset( piece, 0, sizeof( *piece ) );
			piece->surfaceType = SF_SKEL;
			piece->model = tiki;
			piece->surfaceNum = surfaceNum;
			piece->numVerts = used;
			piece->numTris = count;
			piece->verts = ri.Hunk_Alloc( used * sizeof( skelVert_t ), h_low );
			piece->weights = ri.Hunk_Alloc( pieceWeights * sizeof( skelWeight_t ), h_low );
			piece->indexes = ri.Hunk_Alloc( count * 3 * sizeof( int ), h_low );
			pieceWeights = 0;
			for ( j = 0 ; j < numVerts ; j++ ) {
				if ( remap[j] < 0 ) {
					continue;
				}
				piece->verts[remap[j]] = verts[j];
				piece->verts[remap[j]].firstWeight = pieceWeights;
				Com_Memcpy( &piece->weights[pieceWeights], &weights[verts[j].firstWeight],
					verts[j].numWeights * sizeof( skelWeight_t ) );
				pieceWeights += verts[j].numWeights;
			}
			for ( j = 0 ; j < count * 3 ; j++ ) {
				piece->indexes[j] = remap[tris[( firstTri ) * 3 + j]];
			}
			numPieces++;
			firstTri += count;
		}

		surf += ofsEnd;
	}

	ri.FS_FreeFile( buffer.v );

	skel->numSurfaces = numPieces;
	skel->surfaces = ri.Hunk_Alloc( numPieces * sizeof( skelSurface_t ), h_low );
	Com_Memcpy( skel->surfaces, pieces, numPieces * sizeof( skelSurface_t ) );
	return skel;
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
		tiki->skelAnims = ri.Hunk_Alloc( desc->numAnims * sizeof( *tiki->skelAnims ), h_low );
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

	if ( tiki->skeletal ) {
		tiki->skel = R_SkelLoad( tiki, desc->skelFile );
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

	anim = ri.Hunk_Alloc( sizeof( *anim ) + numFrames * sizeof( tikiFrame_t ) + numSurfaces * sizeof( tanSurface_t ), h_low );
	anim->numFrames = numFrames;
	anim->frames = (tikiFrame_t *)( anim + 1 );
	anim->surfaces = (tanSurface_t *)( anim->frames + numFrames );

	for ( i = 0 ; i < numFrames ; i++ ) {
		const byte	*f = data + ofsFrames + i * TAN_FRAME_SIZE;
		tikiFrame_t	*frame = &anim->frames[i];

		// the definition's origin is added before its scale is applied, as
		// the retail engine places tags (fakk2.exe 0x44fe40)
		for ( k = 0 ; k < 3 ; k++ ) {
			frame->scale[k] = TAN_Float( f + 24 + k * 4 ) * tiki->scale;
			frame->offset[k] = ( TAN_Float( f + 36 + k * 4 ) + tiki->origin[k] ) * tiki->scale;
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
		out->surfaceNum = R_TikiSurfaceNum( tiki, surfName );
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
			tikiFrame_t		*frame = &anim->frames[j / numVerts];
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
		R_TikiFinishFrame( &anim->frames[i], &anim->radius );
	}

	if ( !anim->numSurfaces ) {
		return NULL;
	}
	return anim;
}

/*
===============
R_SkaLoad

Reads one skeletal animation: per frame, bounds and every bone's quaternion
and offset, kept as the file's shorts.
===============
*/
static skelAnim_t *R_SkaLoad( const tikiModel_t *tiki, const char *skaFile ) {
	union {
		byte	*b;
		void	*v;
	} buffer;
	skelAnim_t	*anim;
	const byte	*data;
	int			length, numFrames, numBones, ofsFrames, frameSize;
	int			i, j, k;

	if ( !skaFile[0] || !tiki->skel ) {
		return NULL;
	}
	length = ri.FS_ReadFile( skaFile, &buffer.v );
	if ( !buffer.b || length < SKA_HEADER_SIZE ) {
		if ( buffer.b ) {
			ri.FS_FreeFile( buffer.v );
		}
		ri.Printf( PRINT_WARNING, "R_SkaLoad: couldn't load %s\n", skaFile );
		return NULL;
	}
	data = buffer.b;

	numFrames = TAN_Int( data + 76 );
	numBones = TAN_Int( data + 80 );
	ofsFrames = TAN_Int( data + 104 );
	frameSize = SKA_FRAME_HEADER_SIZE + numBones * SKA_BONE_SIZE;
	if ( TAN_Int( data ) != SKA_IDENT || TAN_Int( data + 4 ) != SKEL_VERSION
		|| numFrames < 1 || numBones != tiki->skel->numBones
		|| numFrames > length / frameSize || !TAN_InFile( length, ofsFrames, numFrames * frameSize ) ) {
		ri.FS_FreeFile( buffer.v );
		ri.Printf( PRINT_WARNING, "R_SkaLoad: %s is not a version %i SKA file for this skeleton\n", skaFile, SKEL_VERSION );
		return NULL;
	}

	anim = ri.Hunk_Alloc( sizeof( *anim ) + numFrames * sizeof( tikiFrame_t ) + numFrames * numBones * 8 * sizeof( short ), h_low );
	anim->numFrames = numFrames;
	anim->numBones = numBones;
	anim->frames = (tikiFrame_t *)( anim + 1 );
	anim->bones = (short *)( anim->frames + numFrames );

	for ( i = 0 ; i < numFrames ; i++ ) {
		const byte	*f = data + ofsFrames + i * frameSize;
		tikiFrame_t	*frame = &anim->frames[i];

		for ( k = 0 ; k < 3 ; k++ ) {
			frame->bounds[0][k] = ( TAN_Float( f + k * 4 ) + tiki->origin[k] ) * tiki->scale;
			frame->bounds[1][k] = ( TAN_Float( f + 12 + k * 4 ) + tiki->origin[k] ) * tiki->scale;
		}
		R_TikiFinishFrame( frame, &anim->radius );
		for ( j = 0 ; j < numBones * 8 ; j++ ) {
			anim->bones[i * numBones * 8 + j] = TAN_Short( f + SKA_FRAME_HEADER_SIZE + j * 2 );
		}
	}

	ri.FS_FreeFile( buffer.v );
	return anim;
}

/*
===============
R_TikiAnim, R_TikiSkelAnim

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

static skelAnim_t *R_TikiSkelAnim( tikiModel_t *tiki, int animnum ) {
	skelAnim_t	*anim;

	if ( !tiki->skeletal || !tiki->skel || !tiki->numAnims ) {
		return NULL;
	}
	if ( animnum < 0 || animnum >= tiki->numAnims ) {
		animnum = 0;
	}
	anim = tiki->skelAnims[animnum];
	if ( !anim ) {
		anim = R_SkaLoad( tiki, tiki->animFiles[animnum] );
		tiki->skelAnims[animnum] = anim ? anim : &skelFailed;
	}
	if ( anim == &skelFailed ) {
		anim = NULL;
	}
	if ( !anim && animnum != 0 ) {
		return R_TikiSkelAnim( tiki, 0 );
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
	vec3_t		delta;
	float		scale = 1, radius = tiki->radius;

	if ( tiki->skeletal ) {
		skelAnim_t	*anim = R_TikiSkelAnim( tiki, ent->e.anim );

		if ( anim ) {
			radius = anim->radius;
		}
	} else {
		tanAnim_t	*anim = R_TikiAnim( tiki, ent->e.anim );

		if ( anim ) {
			radius = anim->radius;
		}
	}
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
static int R_TikiCull( const tikiFrame_t *newFrame, const tikiFrame_t *oldFrame, const trRefEntity_t *ent ) {
	vec3_t	bounds[2];
	int		i;

	if ( !ent->e.nonNormalizedAxes ) {
		int		sphereCull = R_CullLocalPointAndRadius( (float *)newFrame->localOrigin, newFrame->radius );

		if ( newFrame != oldFrame ) {
			int		sphereCullB = R_CullLocalPointAndRadius( (float *)oldFrame->localOrigin, oldFrame->radius );

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
R_TikiSurfaceShader

The shader a surface draws with for this entity, or NULL to leave it out.
===============
*/
static shader_t *R_TikiSurfaceShader( const tikiModel_t *tiki, const trRefEntity_t *ent, int surfaceNum ) {
	shader_t	*sh;
	int			flags, skin;

	if ( ent->e.customShader ) {
		return R_GetShaderByHandle( ent->e.customShader );
	}
	if ( surfaceNum < 0 ) {
		return tr.defaultShader;
	}
	flags = ent->e.surfaces[surfaceNum];
	if ( flags & MDL_SURFACE_NODRAW ) {
		return NULL;
	}
	skin = flags & ( MDL_SURFACE_SKINOFFSET_BIT0 | MDL_SURFACE_SKINOFFSET_BIT1 );
	sh = tiki->shaders[surfaceNum * TIKI_MODEL_MAX_SKINS + skin];
	if ( !sh && skin ) {
		sh = tiki->shaders[surfaceNum * TIKI_MODEL_MAX_SKINS];
	}
	return sh;		// NULL: hidden by the definition
}

/*
===============
R_AddTikiSurfaces
===============
*/
void R_AddTikiSurfaces( trRefEntity_t *ent ) {
	tikiModel_t		*tiki = tr.currentModel->modelData;
	const tikiFrame_t	*frames;
	int				numFrames, numSurfaces, i;

	// don't add third_person objects if not in a portal
	if ( ( ent->e.renderfx & RF_THIRD_PERSON ) && !tr.viewParms.isPortal ) {
		return;
	}

	if ( tiki->skeletal ) {
		skelAnim_t	*anim = R_TikiSkelAnim( tiki, ent->e.anim );

		if ( !anim ) {
			return;
		}
		frames = anim->frames;
		numFrames = anim->numFrames;
		numSurfaces = tiki->skel->numSurfaces;
	} else {
		tanAnim_t	*anim = R_TikiAnim( tiki, ent->e.anim );

		if ( !anim ) {
			return;
		}
		frames = anim->frames;
		numFrames = anim->numFrames;
		numSurfaces = anim->numSurfaces;
	}

	// an old frame from another animation means nothing here
	if ( ent->e.oldanim != ent->e.anim ) {
		ent->e.oldframe = ent->e.frame;
	}
	if ( ent->e.frame < 0 || ent->e.frame >= numFrames
		|| ent->e.oldframe < 0 || ent->e.oldframe >= numFrames ) {
		ri.Printf( PRINT_DEVELOPER, "R_AddTikiSurfaces: no such frame %d to %d for '%s'\n",
			ent->e.oldframe, ent->e.frame, tr.currentModel->name );
		ent->e.frame = 0;
		ent->e.oldframe = 0;
	}

	// the torso animation moves the bones the legs animation does not
	if ( tiki->skeletal && ent->e.usetorso ) {
		skelAnim_t	*torso = R_TikiSkelAnim( tiki, ent->e.torso_anim );

		if ( !torso ) {
			ent->e.usetorso = qfalse;
		} else {
			if ( ent->e.torso_oldanim != ent->e.torso_anim ) {
				ent->e.torso_oldframe = ent->e.torso_frame;
			}
			if ( ent->e.torso_frame < 0 || ent->e.torso_frame >= torso->numFrames
				|| ent->e.torso_oldframe < 0 || ent->e.torso_oldframe >= torso->numFrames ) {
				ri.Printf( PRINT_DEVELOPER, "R_AddTikiSurfaces: no such torso frame %d to %d for '%s'\n",
					ent->e.torso_oldframe, ent->e.torso_frame, tr.currentModel->name );
				ent->e.torso_frame = 0;
				ent->e.torso_oldframe = 0;
			}
		}
	}

	if ( R_TikiCull( &frames[ent->e.frame], &frames[ent->e.oldframe], ent ) == CULL_OUT ) {
		return;
	}

	if ( !( tr.refdef.rdflags & RDF_NOWORLDMODEL ) ) {
		R_SetupEntityLighting( &tr.refdef, ent );
	}

	for ( i = 0 ; i < numSurfaces ; i++ ) {
		void		*surf;
		int			surfaceNum;
		shader_t	*sh;

		if ( tiki->skeletal ) {
			surf = &tiki->skel->surfaces[i];
			surfaceNum = tiki->skel->surfaces[i].surfaceNum;
		} else {
			tanAnim_t	*anim = R_TikiAnim( tiki, ent->e.anim );

			surf = &anim->surfaces[i];
			surfaceNum = anim->surfaces[i].surfaceNum;
		}
		sh = R_TikiSurfaceShader( tiki, ent, surfaceNum );
		if ( !sh ) {
			continue;
		}
		R_AddDrawSurf( surf, sh, 0, qfalse );
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
	const tikiFrame_t		*newFrame = &surf->anim->frames[e->frame];
	const tikiFrame_t		*oldFrame = &surf->anim->frames[e->oldframe];
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

/*
===========================================================================

Skeletons

===========================================================================
*/

typedef struct {
	vec3_t		origin;			// in the model's space, before the definition's scale
	vec3_t		axis[3];		// rows of the rotation matrix, Quake's axis convention
	vec4_t		quat;			// the rotation so far, for the children
} skelBonePose_t;

// the bones composed for the entity being drawn; its surfaces come in any
// order among other entities', so they are kept until a different entity or
// frame is drawn
static skelBonePose_t	skel_pose[SKEL_MAX_BONES];

// one of the two animations an entity plays at once
typedef struct {
	const skelAnim_t	*anim;
	int					frame, oldframe;
	float				backlerp;
} skelAnimSel_t;

// everything the pose above was composed from
typedef struct {
	const tikiModel_t	*model;
	const trRefEntity_t	*entity;
	skelAnimSel_t		legs, torso;
	int					boneTag[NUM_BONE_CONTROLLERS];
	vec4_t				boneQuat[NUM_BONE_CONTROLLERS];
} skelPoseKey_t;

static skelPoseKey_t	skel_poseKey;

// r = p * q, Hamilton product on x y z w (fakk2.exe 0x44fe40)
static void Skel_QuatMultiply( const vec4_t p, const vec4_t q, vec4_t r ) {
	r[0] = p[3] * q[0] + p[0] * q[3] + p[1] * q[2] - p[2] * q[1];
	r[1] = p[3] * q[1] - p[0] * q[2] + p[1] * q[3] + p[2] * q[0];
	r[2] = p[3] * q[2] + p[0] * q[1] - p[1] * q[0] + p[2] * q[3];
	r[3] = p[3] * q[3] - p[0] * q[0] - p[1] * q[1] - p[2] * q[2];
}

// from towards to by t, the shorter way round (fakk2.exe 0x43ca60)
static void Skel_QuatSlerp( const vec4_t from, const vec4_t to, float t, vec4_t out ) {
	vec4_t	to1;
	float	cosom = from[0] * to[0] + from[1] * to[1] + from[2] * to[2] + from[3] * to[3];
	float	scale0, scale1;
	int		i;

	if ( cosom < 0 ) {
		cosom = -cosom;
		for ( i = 0 ; i < 4 ; i++ ) {
			to1[i] = -to[i];
		}
	} else {
		if ( from[0] == to[0] && from[1] == to[1] && from[2] == to[2] && from[3] == to[3] ) {
			Vector4Copy( to, out );
			return;
		}
		Vector4Copy( to, to1 );
	}
	if ( 1.0f - cosom > 1e-6f ) {
		double	omega = acos( cosom );
		double	sinom = sin( omega );

		scale0 = sin( ( 1.0 - t ) * omega ) / sinom;
		scale1 = sin( t * omega ) / sinom;
	} else {
		scale0 = 1.0f - t;
		scale1 = t;
	}
	for ( i = 0 ; i < 4 ; i++ ) {
		out[i] = scale0 * from[i] + scale1 * to1[i];
	}
}

// the rotation matrix of a quaternion, rows out (fakk2.exe 0x43c980)
static void Skel_QuatToAxis( const vec4_t q, vec3_t axis[3] ) {
	float	x2 = q[0] + q[0], y2 = q[1] + q[1], z2 = q[2] + q[2];
	float	xx = x2 * q[0], xy = y2 * q[0], xz = z2 * q[0];
	float	yy = y2 * q[1], yz = z2 * q[1], zz = z2 * q[2];
	float	wx = x2 * q[3], wy = y2 * q[3], wz = z2 * q[3];

	axis[0][0] = 1.0f - ( yy + zz );
	axis[0][1] = xy - wz;
	axis[0][2] = xz + wy;
	axis[1][0] = xy + wz;
	axis[1][1] = 1.0f - ( xx + zz );
	axis[1][2] = yz - wx;
	axis[2][0] = xz - wy;
	axis[2][1] = yz + wx;
	axis[2][2] = 1.0f - ( xx + yy );
}

// a vector in a bone's frame, in its parent's (fakk2.exe 0x43c800)
static void Skel_RotateByAxis( const vec3_t in, const vec3_t axis[3], vec3_t out ) {
	out[0] = in[0] * axis[0][0] + in[1] * axis[1][0] + in[2] * axis[2][0];
	out[1] = in[0] * axis[0][1] + in[1] * axis[1][1] + in[2] * axis[2][1];
	out[2] = in[0] * axis[0][2] + in[1] * axis[1][2] + in[2] * axis[2][2];
}

static void Skel_FrameBone( const skelAnim_t *anim, int frame, int bone, vec4_t quat, vec3_t offset ) {
	const short	*s = anim->bones + ( frame * anim->numBones + bone ) * 8;

	quat[0] = s[0] * SKEL_QUAT_SCALE;
	quat[1] = s[1] * SKEL_QUAT_SCALE;
	quat[2] = s[2] * SKEL_QUAT_SCALE;
	quat[3] = s[3] * SKEL_QUAT_SCALE;
	offset[0] = s[4] * SKEL_OFFSET_SCALE;
	offset[1] = s[5] * SKEL_OFFSET_SCALE;
	offset[2] = s[6] * SKEL_OFFSET_SCALE;
}

/*
===============
R_SkelComputeBones

Composes the entity's bones for its frame, root first.
===============
*/
static void R_SkelComputeBones( tikiModel_t *tiki, const trRefEntity_t *ent ) {
	const refEntity_t	*e = &ent->e;
	skelPoseKey_t		key;
	int					b, i;

	// retail's R_AddSkelSurfaces (0x4872d0): the legs animation moves the
	// bones flagged for it and the torso animation the rest; either alone
	// moves every bone
	Com_Memset( &key, 0, sizeof( key ) );
	key.model = tiki;
	key.entity = ent;
	key.legs.anim = R_TikiSkelAnim( tiki, e->anim );
	key.legs.frame = e->frame;
	key.legs.oldframe = e->oldframe;
	key.legs.backlerp = e->oldframe == e->frame ? 0 : e->backlerp;
	if ( e->usetorso ) {
		key.torso.anim = R_TikiSkelAnim( tiki, e->torso_anim );
		key.torso.frame = e->torso_frame;
		key.torso.oldframe = e->torso_oldframe;
		key.torso.backlerp = e->torso_oldframe == e->torso_frame ? 0 : e->torso_backlerp;
	}
	if ( !key.torso.anim ) {
		key.torso = key.legs;
	} else if ( !e->uselegs || !key.legs.anim ) {
		key.legs = key.torso;
	}
	// the bone controllers only count when the entity says so (0x4872d0 tests useAngles)
	if ( e->useAngles ) {
		Com_Memcpy( key.boneTag, e->bone_tag, sizeof( key.boneTag ) );
		Com_Memcpy( key.boneQuat, e->bone_quat, sizeof( key.boneQuat ) );
	} else {
		for ( i = 0 ; i < NUM_BONE_CONTROLLERS ; i++ ) {
			key.boneTag[i] = -1;
		}
	}

	if ( !memcmp( &key, &skel_poseKey, sizeof( key ) ) ) {
		return;
	}
	skel_poseKey = key;

	for ( b = 0 ; b < tiki->skel->numBones ; b++ ) {
		skelBonePose_t		*pose = &skel_pose[b];
		const skelAnimSel_t	*sel = tiki->skel->legBone[b] ? &key.legs : &key.torso;
		const skelAnim_t	*anim = sel->anim;
		float				backlerp = sel->backlerp;
		vec4_t				quat, acc;
		vec3_t				offset;
		int					parent = tiki->skel->parents[b];

		Skel_FrameBone( anim, sel->frame, b, quat, offset );
		if ( backlerp ) {
			vec4_t	oldQuat, lerped;
			vec3_t	oldOffset;

			// from the old frame towards this one (0x4872d0 calls 0x43ca60 this way)
			Skel_FrameBone( anim, sel->oldframe, b, oldQuat, oldOffset );
			Skel_QuatSlerp( oldQuat, quat, 1.0f - backlerp, lerped );
			Vector4Copy( lerped, quat );
			for ( i = 0 ; i < 3 ; i++ ) {
				offset[i] = offset[i] * ( 1.0f - backlerp ) + oldOffset[i] * backlerp;
			}
		}

		if ( parent < 0 ) {
			VectorAdd( offset, tiki->origin, pose->origin );
			Vector4Copy( quat, acc );
		} else {
			const skelBonePose_t	*parentPose = &skel_pose[parent];
			vec3_t					turned;

			Skel_RotateByAxis( offset, parentPose->axis, turned );
			VectorAdd( parentPose->origin, turned, pose->origin );
			Skel_QuatMultiply( quat, parentPose->quat, acc );
		}

		// the entity's bone controllers turn their bones further
		for ( i = 0 ; i < NUM_BONE_CONTROLLERS ; i++ ) {
			if ( key.boneTag[i] == b ) {
				vec4_t	turned;

				Skel_QuatMultiply( acc, key.boneQuat[i], turned );
				Vector4Copy( turned, acc );
			}
		}

		Vector4Copy( acc, pose->quat );
		Skel_QuatToAxis( acc, pose->axis );
	}
}

/*
===============
RB_SurfaceSkel

Skins the surface's vertexes with the entity's bones.
===============
*/
void RB_SurfaceSkel( void *surface ) {
	skelSurface_t		*surf = surface;
	const trRefEntity_t	*ent = backEnd.currentEntity;
	tikiModel_t			*tiki = surf->model;
	float				*outXyz, *outNormal;
	int					i, j, numVerts, numIndexes, baseVertex, baseIndex;

	if ( !R_TikiSkelAnim( tiki, ent->e.anim ) ) {
		return;
	}
	R_SkelComputeBones( tiki, ent );

	RB_CHECKOVERFLOW( surf->numVerts, surf->numTris * 3 );

	numVerts = surf->numVerts;
	numIndexes = surf->numTris * 3;
	baseVertex = tess.numVertexes;
	baseIndex = tess.numIndexes;
	outXyz = tess.xyz[baseVertex];
	outNormal = tess.normal[baseVertex];

	for ( i = 0 ; i < numVerts ; i++, outXyz += 4, outNormal += 4 ) {
		const skelVert_t	*v = &surf->verts[i];
		vec3_t				xyz, normal, turned;

		VectorClear( xyz );
		VectorClear( normal );
		for ( j = 0 ; j < v->numWeights ; j++ ) {
			const skelWeight_t		*w = &surf->weights[v->firstWeight + j];
			const skelBonePose_t	*pose = &skel_pose[w->bone];

			Skel_RotateByAxis( w->offset, pose->axis, turned );
			VectorAdd( turned, pose->origin, turned );
			VectorMA( xyz, w->weight, turned, xyz );
			Skel_RotateByAxis( v->normal, pose->axis, turned );
			VectorMA( normal, w->weight, turned, normal );
		}
		VectorScale( xyz, tiki->scale, outXyz );
		VectorNormalizeFast( normal );
		VectorCopy( normal, outNormal );

		tess.texCoords[baseVertex + i][0][0] = v->st[0];
		tess.texCoords[baseVertex + i][0][1] = v->st[1];
	}

	for ( i = 0 ; i < numIndexes ; i++ ) {
		tess.indexes[baseIndex + i] = baseVertex + surf->indexes[i];
	}

	tess.numVertexes += numVerts;
	tess.numIndexes += numIndexes;
}
