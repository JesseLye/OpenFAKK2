/*
===========================================================================
TIKI model definitions for Heavy Metal: F.A.K.K. 2. See tiki.h.

Three derivation rules are not documented anywhere and were established by
comparing against the definitions the retail engine cached in the game's
.cik files (matching 742 of 743 models):
  - bounds are the union of every frame of every animation, unscaled, plus
    the setup origin; a model with no geometry gets a box of +-4
  - radius is the setup radius if given, otherwise the radius of those
    bounds times the setup scale
  - lod_scale is stored divided by 5
===========================================================================
*/

#include "tiki.h"
#include "../qcommon/qcommon.h"
#include "../qcommon/alias.h"

#define	TIKI_MAX_SURFACES		32
#define	TIKI_MAX_SKINS			4
#define	TIKI_MAX_NAME			128
#define	TIKI_MAX_LINE_TOKENS	64
#define	TIKI_MAX_MACROS			128
#define	TIKI_MAX_INCLUDE_DEPTH	8
#define	TIKI_MAX_LINE			2048

// .tan vertex animation. The sizes and offsets match tikiHeader_t, tikiFrame_t,
// tikiTag_t, tikiTagData_t and tikiSurface_t in qfiles.h of Ritual's tools
// release, and the layout max2tan writes (models.c); every .tan a retail .tik
// names fits them.
#define	TAN_IDENT				((' '<<24)+('N'<<16)+('A'<<8)+'T')	// "TAN " on disk
#define	TAN_VERSION				2
#define	TAN_MAX_TAGS			16
#define	TAN_HEADER_SIZE			176
#define	TAN_FRAME_SIZE			68
#define	TAN_TAG_NAME_SIZE		64
#define	TAN_TAG_DATA_SIZE		48
#define	TAN_SURFACE_HEADER_SIZE	104

// .skb skeleton and .ska skeletal animation: skelHeader_t, skelBoneName_t,
// skelSurface_t, skelAnimHeader_t, skelFrame_t and skelBone_t in qfiles.h, as
// max2skl writes them. The retail archives also hold version 1 and 2 files that
// no .tik names.
#define	SKB_IDENT				((' '<<24)+('L'<<16)+('K'<<8)+'S')	// "SKL " on disk
#define	SKA_IDENT				(('N'<<24)+('A'<<16)+('K'<<8)+'S')	// "SKAN" on disk
#define	SKEL_VERSION			3
#define	SKEL_MAX_BONES			100
#define	SKB_HEADER_SIZE			92
#define	SKB_BONE_SIZE			72
#define	SKB_SURFACE_HEADER_SIZE	96
#define	SKA_HEADER_SIZE			108
#define	SKA_FRAME_HEADER_SIZE	40
#define	SKA_BONE_SIZE			16

// A bone's quaternion is stored times 32767 and its offset times 63
// (qfiles.h TIKI_BONE_QUAT_MULTIPLIER, TIKI_BONE_OFFSET_MULTIPLIER); fakk2.exe
// decodes with 1/32767 (0x4d57dc, read by the tag code and R_AddSkelSurfaces)
// and 1/63.
#define	SKEL_QUAT_SCALE			( 1.0f / 32767.0f )
#define	SKEL_OFFSET_SCALE		( 1.0f / 63.0f )

// a "last" frame selector, resolved to a frame number once the animation is loaded
#define	TIKI_FRAME_CMD_LAST		-4

// Blend time given to an animation that does not set one. In the retail
// engine's cached definitions 8933 of 8951 animations carry this value, and
// only four (in one obsolete model) are flagged delta driven; the keyword
// that sets that flag has not been identified.
#define	TIKI_DEFAULT_CROSSBLEND_MS	200

typedef struct {
	int		frame;
	int		numArgs;
	char	**args;
} tikiCommand_t;

typedef struct {
	int				count;
	int				capacity;
	tikiCommand_t	*cmds;
} tikiCommandList_t;

typedef struct {
	vec3_t	mins, maxs;
	vec3_t	delta;
	float	time;
} tikiAnimFrame_t;

typedef struct {
	char				alias[48];
	char				file[TIKI_MAX_NAME];		// as the definition names it
	char				fullname[TIKI_MAX_NAME];
	int					flags;
	int					crossblend;
	float				weight;		// share of random picks among variants

	int					numFrames;
	float				totalTime;
	vec3_t				totalDelta;
	tikiAnimFrame_t		*frames;
	int					fileSize;	// the animation file's length, for "tiki" and "tikilist"

	// vertex animations carry tag placements per frame: [tag][frame] of
	// origin followed by three axis vectors
	int					numTags;
	float				*tagData;

	// skeletal animations carry bones per frame: [frame][bone] of a short
	// quaternion, a short offset and a padding short
	int					numBones;
	short				*boneData;

	tikiCommandList_t	serverCmds;
	tikiCommandList_t	clientCmds;
} tikiAnim_t;

typedef struct {
	char	name[MAX_QPATH];
	char	shader[TIKI_MAX_SKINS][MAX_QPATH];
	int		numSkins;
	int		flags;
	float	damageMultiplier;
	int		numTriangles;	// from the first animation or the skeleton, for "tiki"
} tikiSurface_t;

struct tiki_s {
	struct tiki_s		*next;
	char				name[TIKI_MAX_NAME];
	qboolean			valid;

	float				loadScale;
	float				lodScale;
	float				lodBias;
	vec3_t				loadOrigin;
	vec3_t				lightOffset;	// "lightoffset"
	vec3_t				mins, maxs;
	float				radius;
	qboolean			radiusSet;

	char				path[MAX_QPATH];
	char				skelName[TIKI_MAX_NAME];
	char				skelFile[TIKI_MAX_NAME];	// skelName under the model's path

	int					numSurfaces;
	tikiSurface_t		surfaces[TIKI_MAX_SURFACES];

	// tag names for vertex-animated models, bone names for skeletal ones
	int					numTags;
	char				( *tagNames )[MAX_QPATH];
	int					*boneParents;		// NULL unless skeletal
	qboolean			*legBones;			// bones the legs animation moves when a torso animation plays too

	int					numAnims;
	int					animCapacity;
	tikiAnim_t			*anims;

	tikiCommandList_t	serverInit;
	tikiCommandList_t	clientInit;

	AliasList_t			*aliases;
};

typedef struct {
	char	name[MAX_QPATH];
	char	value[MAX_QPATH * 2];
} tikiMacro_t;

typedef enum { SECTION_NONE, SECTION_SETUP, SECTION_INIT, SECTION_ANIMATIONS, SECTION_OTHER } tikiSection_t;
typedef enum { SUB_NONE, SUB_SERVER, SUB_CLIENT } tikiSubSection_t;

typedef struct {
	tiki_t				*tiki;
	int					numMacros;
	tikiMacro_t			macros[TIKI_MAX_MACROS];
	tikiCommandList_t	surfaceCmds;	// applied once the surface names are known

	// where the reader is, carried through $include
	tikiSection_t		section;
	tikiSubSection_t	sub;
	int					level;			// brace depth
	int					animIndex;		// the animation whose commands are being read
	qboolean			inEffectBlock;	// between the "(" and ")" lines of a client effect
	int					lastFrame;		// the frame of the command line above
} tikiParse_t;

static tiki_t	*tiki_list;

/*
===========================================================================

Memory and small helpers

===========================================================================
*/

static void *TIKI_Alloc( size_t size ) {
	void	*p = calloc( 1, size ? size : 1 );

	if ( !p ) {
		Com_Error( ERR_FATAL, "TIKI_Alloc: failed on allocation of %u bytes", (unsigned)size );
	}
	return p;
}

static char *TIKI_CopyString( const char *s ) {
	size_t	len = strlen( s ) + 1;
	char	*out = TIKI_Alloc( len );

	memcpy( out, s, len );
	return out;
}

static tikiCommand_t *TIKI_AddCommand( tikiCommandList_t *list, int frame, char **tokens, int numTokens ) {
	tikiCommand_t	*cmd;
	int				i;

	if ( list->count == list->capacity ) {
		int				capacity = list->capacity ? list->capacity * 2 : 8;
		tikiCommand_t	*grown = TIKI_Alloc( capacity * sizeof( *grown ) );

		if ( list->cmds ) {
			memcpy( grown, list->cmds, list->count * sizeof( *grown ) );
			free( list->cmds );
		}
		list->cmds = grown;
		list->capacity = capacity;
	}

	cmd = &list->cmds[list->count++];
	cmd->frame = frame;
	cmd->numArgs = numTokens;
	cmd->args = TIKI_Alloc( ( numTokens ? numTokens : 1 ) * sizeof( char * ) );
	for ( i = 0 ; i < numTokens ; i++ ) {
		cmd->args[i] = TIKI_CopyString( tokens[i] );
	}
	return cmd;
}

static void TIKI_FreeCommands( tikiCommandList_t *list ) {
	int		i, j;

	for ( i = 0 ; i < list->count ; i++ ) {
		for ( j = 0 ; j < list->cmds[i].numArgs ; j++ ) {
			free( list->cmds[i].args[j] );
		}
		free( list->cmds[i].args );
	}
	free( list->cmds );
	Com_Memset( list, 0, sizeof( *list ) );
}

static void TIKI_Free( tiki_t *tiki ) {
	int		i;

	for ( i = 0 ; i < tiki->numAnims ; i++ ) {
		tikiAnim_t	*anim = &tiki->anims[i];

		free( anim->frames );
		free( anim->tagData );
		free( anim->boneData );
		TIKI_FreeCommands( &anim->serverCmds );
		TIKI_FreeCommands( &anim->clientCmds );
	}
	free( tiki->anims );
	free( tiki->tagNames );
	free( tiki->boneParents );
	free( tiki->legBones );
	TIKI_FreeCommands( &tiki->serverInit );
	TIKI_FreeCommands( &tiki->clientInit );
	Alias_ListDelete( tiki->aliases );
	free( tiki );
}

void TIKI_Shutdown( void ) {
	while ( tiki_list ) {
		tiki_t	*next = tiki_list->next;

		TIKI_Free( tiki_list );
		tiki_list = next;
	}
}

static int TIKI_ReadInt( const byte *p ) {
	int		v;

	memcpy( &v, p, sizeof( v ) );
	return LittleLong( v );
}

static float TIKI_ReadFloat( const byte *p ) {
	floatint_t	v;

	memcpy( &v.i, p, sizeof( v.i ) );
	v.i = LittleLong( v.i );
	return v.f;
}

static void TIKI_ReadVec3( const byte *p, vec3_t out ) {
	out[0] = TIKI_ReadFloat( p );
	out[1] = TIKI_ReadFloat( p + 4 );
	out[2] = TIKI_ReadFloat( p + 8 );
}

static qboolean TIKI_HasExtension( const char *name, const char *ext ) {
	size_t	len = strlen( name );
	size_t	extLen = strlen( ext );

	return len >= extLen && !Q_stricmp( name + len - extLen, ext );
}

/*
===========================================================================

Animation and skeleton files

===========================================================================
*/

// true when [offset, offset + size) lies inside a file of the given length
static qboolean TIKI_InFile( int length, int offset, int size ) {
	return offset >= 0 && size >= 0 && offset <= length && size <= length - offset;
}

static qboolean TIKI_LoadTan( tiki_t *tiki, tikiAnim_t *anim, const byte *data, int length, qboolean first ) {
	int		numFrames, numTags, numSurfaces, ofsFrames, ofsSurfaces;
	int		i, j;

	if ( length < TAN_HEADER_SIZE || TIKI_ReadInt( data ) != TAN_IDENT || TIKI_ReadInt( data + 4 ) != TAN_VERSION ) {
		Com_Printf( "WARNING: %s is not a version %i TAN file\n", anim->fullname, TAN_VERSION );
		return qfalse;
	}

	numFrames = TIKI_ReadInt( data + 72 );
	numTags = TIKI_ReadInt( data + 76 );
	numSurfaces = TIKI_ReadInt( data + 80 );
	ofsFrames = TIKI_ReadInt( data + 100 );
	ofsSurfaces = TIKI_ReadInt( data + 104 );

	if ( numFrames < 1 || numTags < 0 || numTags > TAN_MAX_TAGS || numSurfaces < 0 || numSurfaces > TIKI_MAX_SURFACES
		|| numFrames > ( length / TAN_FRAME_SIZE ) || !TIKI_InFile( length, ofsFrames, numFrames * TAN_FRAME_SIZE ) ) {
		Com_Printf( "WARNING: %s has an invalid header\n", anim->fullname );
		return qfalse;
	}

	anim->numFrames = numFrames;
	anim->totalTime = TIKI_ReadFloat( data + 84 );
	TIKI_ReadVec3( data + 88, anim->totalDelta );
	anim->frames = TIKI_Alloc( numFrames * sizeof( *anim->frames ) );
	for ( i = 0 ; i < numFrames ; i++ ) {
		const byte	*f = data + ofsFrames + i * TAN_FRAME_SIZE;

		TIKI_ReadVec3( f, anim->frames[i].mins );
		TIKI_ReadVec3( f + 12, anim->frames[i].maxs );
		TIKI_ReadVec3( f + 48, anim->frames[i].delta );
		anim->frames[i].time = TIKI_ReadFloat( f + 64 );
	}

	anim->numTags = numTags;
	if ( numTags ) {
		anim->tagData = TIKI_Alloc( numTags * numFrames * 12 * sizeof( float ) );
	}
	for ( i = 0 ; i < numTags ; i++ ) {
		int		ofsTag = TIKI_ReadInt( data + 108 + i * 4 );

		if ( !TIKI_InFile( length, ofsTag, TAN_TAG_NAME_SIZE + numFrames * TAN_TAG_DATA_SIZE ) ) {
			Com_Printf( "WARNING: %s has a tag outside the file\n", anim->fullname );
			return qfalse;
		}
		for ( j = 0 ; j < numFrames * 12 ; j++ ) {
			anim->tagData[i * numFrames * 12 + j] = TIKI_ReadFloat( data + ofsTag + TAN_TAG_NAME_SIZE + j * 4 );
		}
		if ( first ) {
			Q_strncpyz( tiki->tagNames[i], (const char *)data + ofsTag, MAX_QPATH );
		}
	}

	// the first animation of a vertex-animated model defines its surfaces and tags
	if ( first ) {
		int		ofs = ofsSurfaces;

		tiki->numTags = numTags;
		for ( i = 0 ; i < numSurfaces ; i++ ) {
			int		ofsEnd;

			if ( !TIKI_InFile( length, ofs, TAN_SURFACE_HEADER_SIZE ) ) {
				Com_Printf( "WARNING: %s has a surface outside the file\n", anim->fullname );
				return qfalse;
			}
			Q_strncpyz( tiki->surfaces[i].name, (const char *)data + ofs + 4, MAX_QPATH );
			tiki->surfaces[i].numTriangles = TIKI_ReadInt( data + ofs + 80 );
			ofsEnd = TIKI_ReadInt( data + ofs + 100 );
			if ( ofsEnd <= 0 ) {
				Com_Printf( "WARNING: %s has a malformed surface\n", anim->fullname );
				return qfalse;
			}
			ofs += ofsEnd;
		}
		tiki->numSurfaces = numSurfaces;
	}

	return qtrue;
}

static qboolean TIKI_LoadSka( tikiAnim_t *anim, const byte *data, int length ) {
	int		numFrames, numBones, ofsFrames, frameSize;
	float	frameTime;
	int		i, j;

	if ( length < SKA_HEADER_SIZE || TIKI_ReadInt( data ) != SKA_IDENT || TIKI_ReadInt( data + 4 ) != SKEL_VERSION ) {
		Com_Printf( "WARNING: %s is not a version %i SKA file\n", anim->fullname, SKEL_VERSION );
		return qfalse;
	}

	numFrames = TIKI_ReadInt( data + 76 );
	numBones = TIKI_ReadInt( data + 80 );
	frameTime = TIKI_ReadFloat( data + 88 );
	ofsFrames = TIKI_ReadInt( data + 104 );

	if ( numFrames < 1 || numBones < 0 || numBones > SKEL_MAX_BONES ) {
		Com_Printf( "WARNING: %s has an invalid header\n", anim->fullname );
		return qfalse;
	}
	frameSize = SKA_FRAME_HEADER_SIZE + numBones * SKA_BONE_SIZE;
	if ( numFrames > length / frameSize || !TIKI_InFile( length, ofsFrames, numFrames * frameSize ) ) {
		Com_Printf( "WARNING: %s is truncated\n", anim->fullname );
		return qfalse;
	}

	anim->numFrames = numFrames;
	anim->totalTime = TIKI_ReadFloat( data + 84 );
	TIKI_ReadVec3( data + 92, anim->totalDelta );
	anim->frames = TIKI_Alloc( numFrames * sizeof( *anim->frames ) );
	anim->numBones = numBones;
	if ( numBones ) {
		anim->boneData = TIKI_Alloc( numFrames * numBones * 8 * sizeof( short ) );
	}

	for ( i = 0 ; i < numFrames ; i++ ) {
		const byte	*f = data + ofsFrames + i * frameSize;

		TIKI_ReadVec3( f, anim->frames[i].mins );
		TIKI_ReadVec3( f + 12, anim->frames[i].maxs );
		TIKI_ReadVec3( f + 28, anim->frames[i].delta );
		anim->frames[i].time = frameTime;

		for ( j = 0 ; j < numBones * 8 ; j++ ) {
			short	s;

			memcpy( &s, f + SKA_FRAME_HEADER_SIZE + j * 2, sizeof( s ) );
			anim->boneData[i * numBones * 8 + j] = LittleShort( s );
		}
	}

	return qtrue;
}

static qboolean TIKI_LoadSkb( tiki_t *tiki, const char *name ) {
	void		*buffer;
	const byte	*data;
	int			length, numSurfaces, numBones, ofsBones, ofs;
	int			i;
	qboolean	ok = qfalse;

	length = (int)FS_ReadFile( name, &buffer );
	if ( !buffer ) {
		Com_Printf( "WARNING: couldn't load skeleton %s for %s\n", name, tiki->name );
		return qfalse;
	}
	data = buffer;

	do {
		if ( length < SKB_HEADER_SIZE || TIKI_ReadInt( data ) != SKB_IDENT || TIKI_ReadInt( data + 4 ) != SKEL_VERSION ) {
			Com_Printf( "WARNING: %s is not a version %i SKB file\n", name, SKEL_VERSION );
			break;
		}
		numSurfaces = TIKI_ReadInt( data + 72 );
		numBones = TIKI_ReadInt( data + 76 );
		ofsBones = TIKI_ReadInt( data + 80 );
		ofs = TIKI_ReadInt( data + 84 );

		if ( numSurfaces < 0 || numSurfaces > TIKI_MAX_SURFACES || numBones < 0 || numBones > SKEL_MAX_BONES
			|| !TIKI_InFile( length, ofsBones, numBones * SKB_BONE_SIZE ) ) {
			Com_Printf( "WARNING: %s has an invalid header\n", name );
			break;
		}

		tiki->boneParents = TIKI_Alloc( ( numBones ? numBones : 1 ) * sizeof( int ) );
		tiki->legBones = TIKI_Alloc( ( numBones ? numBones : 1 ) * sizeof( qboolean ) );
		for ( i = 0 ; i < numBones ; i++ ) {
			const byte	*b = data + ofsBones + i * SKB_BONE_SIZE;
			int			parent = TIKI_ReadInt( b );

			// a bone may only hang off an earlier bone, which keeps evaluation a single pass
			tiki->boneParents[i] = ( parent >= 0 && parent < i ) ? parent : -1;
			// a bone whose flags are exactly 1 belongs to the legs (fakk2.exe 0x44ee40, 0x4872d0);
			// max2skl writes 1 (qfiles.h TIKI_BONEFLAG_LEG) for a LEGBONE and 0 otherwise
			tiki->legBones[i] = TIKI_ReadInt( b + 4 ) == 1;
			Q_strncpyz( tiki->tagNames[i], (const char *)b + 8, MAX_QPATH );
		}
		tiki->numTags = numBones;

		for ( i = 0 ; i < numSurfaces ; i++ ) {
			int		ofsEnd;

			if ( !TIKI_InFile( length, ofs, SKB_SURFACE_HEADER_SIZE ) ) {
				break;
			}
			Q_strncpyz( tiki->surfaces[i].name, (const char *)data + ofs + 4, MAX_QPATH );
			tiki->surfaces[i].numTriangles = TIKI_ReadInt( data + ofs + 68 );
			ofsEnd = TIKI_ReadInt( data + ofs + 92 );
			if ( ofsEnd <= 0 ) {
				break;
			}
			ofs += ofsEnd;
		}
		if ( i != numSurfaces ) {
			Com_Printf( "WARNING: %s has a malformed surface\n", name );
			break;
		}
		tiki->numSurfaces = numSurfaces;
		ok = qtrue;
	} while ( 0 );

	FS_FreeFile( buffer );
	return ok;
}

/*
===========================================================================

Definition text

===========================================================================
*/

// blanks out comments in place, keeping newlines so line structure survives
static void TIKI_StripComments( char *text ) {
	char	*p = text;

	while ( *p ) {
		if ( p[0] == '/' && p[1] == '/' ) {
			while ( *p && *p != '\n' ) {
				*p++ = ' ';
			}
		} else if ( p[0] == '/' && p[1] == '*' ) {
			while ( *p && !( p[0] == '*' && p[1] == '/' ) ) {
				if ( *p != '\n' ) {
					*p = ' ';
				}
				p++;
			}
			if ( *p ) {
				p[0] = ' ';
				p[1] = ' ';
				p += 2;
			}
		} else if ( *p == '"' ) {
			// comment markers inside a quoted string are text
			p++;
			while ( *p && *p != '"' && *p != '\n' ) {
				p++;
			}
			if ( *p == '"' ) {
				p++;
			}
		} else {
			p++;
		}
	}
}

static const char *TIKI_FindMacro( const tikiParse_t *parse, const char *name, int nameLen ) {
	int		i;

	for ( i = parse->numMacros - 1 ; i >= 0 ; i-- ) {
		if ( (int)strlen( parse->macros[i].name ) == nameLen && !Q_stricmpn( parse->macros[i].name, name, nameLen ) ) {
			return parse->macros[i].value;
		}
	}
	return NULL;
}

// expands $name$ references; an unknown name is left as written
static void TIKI_ExpandMacros( const tikiParse_t *parse, const char *in, char *out, int outSize ) {
	int		len = 0;

	while ( *in && len < outSize - 1 ) {
		if ( *in == '$' ) {
			const char	*end = in + 1;

			while ( isalnum( (unsigned char)*end ) || *end == '_' ) {
				end++;
			}
			if ( *end == '$' && end > in + 1 ) {
				const char	*value = TIKI_FindMacro( parse, in + 1, (int)( end - in - 1 ) );

				if ( value ) {
					while ( *value && len < outSize - 1 ) {
						out[len++] = *value++;
					}
					in = end + 1;
					continue;
				}
			}
		}
		out[len++] = *in++;
	}
	out[len] = 0;
}

// splits a line into whitespace-separated tokens in place; a quoted run is one token
static int TIKI_Tokenize( char *line, char **tokens, int maxTokens ) {
	int		count = 0;
	char	*p = line;

	while ( *p && count < maxTokens ) {
		while ( *p && (unsigned char)*p <= ' ' ) {
			p++;
		}
		if ( !*p ) {
			break;
		}
		if ( *p == '"' ) {
			tokens[count++] = ++p;
			while ( *p && *p != '"' ) {
				p++;
			}
		} else {
			tokens[count++] = p;
			while ( *p && (unsigned char)*p > ' ' ) {
				p++;
			}
		}
		if ( *p ) {
			*p++ = 0;
		}
	}
	return count;
}

static tikiAnim_t *TIKI_NewAnim( tiki_t *tiki ) {
	if ( tiki->numAnims == tiki->animCapacity ) {
		int			capacity = tiki->animCapacity ? tiki->animCapacity * 2 : 16;
		tikiAnim_t	*grown = TIKI_Alloc( capacity * sizeof( *grown ) );

		if ( tiki->anims ) {
			memcpy( grown, tiki->anims, tiki->numAnims * sizeof( *grown ) );
			free( tiki->anims );
		}
		tiki->anims = grown;
		tiki->animCapacity = capacity;
	}
	return &tiki->anims[tiki->numAnims++];
}

// the frame a command line's first token names (fakk2.exe 0x44b760): "start" and
// "first" are the first frame, "last" and "end" the last, "every", "exit" and
// "entry" or "enter" the special frames, and anything else is read as a number,
// so a word there is frame 0
static int TIKI_ParseFrameSelector( const char *token ) {
	if ( !Q_stricmp( token, "start" ) || !Q_stricmp( token, "first" ) ) {
		return 0;
	}
	if ( !Q_stricmp( token, "last" ) || !Q_stricmp( token, "end" ) ) {
		return TIKI_FRAME_CMD_LAST;
	}
	if ( !Q_stricmp( token, "every" ) ) {
		return TIKI_FRAME_CMD_EVERY_FRAME;
	}
	if ( !Q_stricmp( token, "exit" ) ) {
		return TIKI_FRAME_CMD_EXIT;
	}
	if ( !Q_stricmp( token, "entry" ) || !Q_stricmp( token, "enter" ) ) {
		return TIKI_FRAME_CMD_ENTRY;
	}
	return atoi( token );
}

static void TIKI_ParseSetup( tikiParse_t *parse, char **tokens, int numTokens ) {
	tiki_t		*tiki = parse->tiki;
	const char	*key = tokens[0];

	if ( !Q_stricmp( key, "surface" ) ) {
		// A shader name with an extension is a texture under the path in
		// force on that line, as the retail parser resolves it (TIKI_ParseSetup,
		// fakk2.exe 0x44c170: strstr for '.', then path + name); a name without
		// one is a shader script's. ammo_gasplant.tik names its texture before
		// its path line, so it resolves under the definition's own directory,
		// models/, and not under the path.
		char	resolved[TIKI_MAX_SKINS][MAX_QPATH];
		char	*args[TIKI_MAX_LINE_TOKENS];
		int		i, n = 0;

		for ( i = 1 ; i < numTokens ; i++ ) {
			args[i - 1] = tokens[i];
		}
		if ( numTokens >= 3 && !Q_stricmp( tokens[2], "shader" ) && tiki->path[0] ) {
			for ( i = 3 ; i < numTokens && n < TIKI_MAX_SKINS ; i++ ) {
				if ( strchr( tokens[i], '.' ) ) {
					Com_sprintf( resolved[n], sizeof( resolved[n] ), "%s/%s", tiki->path, tokens[i] );
					args[i - 1] = resolved[n++];
				}
			}
		}
		TIKI_AddCommand( &parse->surfaceCmds, 0, args, numTokens - 1 );
		return;
	}
	if ( numTokens < 2 ) {
		return;
	}

	if ( !Q_stricmp( key, "scale" ) ) {
		tiki->loadScale = atof( tokens[1] );
	} else if ( !Q_stricmp( key, "lod_scale" ) ) {
		// the engine keeps this relative to the default r_lodscale of 5
		tiki->lodScale = atof( tokens[1] ) / 5.0f;
	} else if ( !Q_stricmp( key, "lod_bias" ) ) {
		tiki->lodBias = atof( tokens[1] );
	} else if ( !Q_stricmp( key, "radius" ) ) {
		tiki->radius = atof( tokens[1] );
		tiki->radiusSet = qtrue;
	} else if ( !Q_stricmp( key, "path" ) ) {
		int		len;

		Q_strncpyz( tiki->path, tokens[1], sizeof( tiki->path ) );
		len = strlen( tiki->path );
		while ( len > 0 && ( tiki->path[len - 1] == '/' || tiki->path[len - 1] == '\\' ) ) {
			tiki->path[--len] = 0;
		}
	} else if ( !Q_stricmp( key, "skelmodel" ) ) {
		Q_strncpyz( tiki->skelName, tokens[1], sizeof( tiki->skelName ) );
	} else if ( !Q_stricmp( key, "origin" ) && numTokens >= 4 ) {
		tiki->loadOrigin[0] = atof( tokens[1] );
		tiki->loadOrigin[1] = atof( tokens[2] );
		tiki->loadOrigin[2] = atof( tokens[3] );
	} else if ( !Q_stricmp( key, "lightoffset" ) && numTokens >= 4 ) {
		// where the model is lit from, moved in its own axes (TIKI_ParseSetup,
		// fakk2.exe 0x44c3eb; the renderer adds it at 0x48d0b2)
		tiki->lightOffset[0] = atof( tokens[1] );
		tiki->lightOffset[1] = atof( tokens[2] );
		tiki->lightOffset[2] = atof( tokens[3] );
	} else {
		Com_DPrintf( "TIKI: unknown setup keyword '%s' in %s\n", key, tiki->name );
	}
}

static qboolean TIKI_ParseFile( tikiParse_t *parse, const char *filename, int depth );

/*
===============
TIKI_ParseText

Sections, braces and commands are all line oriented: a brace stands alone on
its line, and a command is the tokens of one line. An included file's text
continues where the $include line stands, as in retail (fakk2.exe 0x4529f0
opens it as a child script, given the parent's $defines, that the reader then
takes its tokens from): the reader's state is the parse's, not the file's.
weapon_uzi.tik and weapon_dualuzi.tik include uzi_emitters.txt, a bare list of
tagemitter blocks, inside their init client block; read as a definition of
its own, every line of it was dropped, and with it the uzi's muzzle burst,
smoke and shells.
===============
*/
static void TIKI_ParseText( tikiParse_t *parse, char *text, int depth ) {
	tiki_t		*tiki = parse->tiki;
	char		*line = text;

	while ( line && *line ) {
		char	expanded[TIKI_MAX_LINE];
		char	*tokens[TIKI_MAX_LINE_TOKENS];
		char	*next = strchr( line, '\n' );
		int		numTokens;
		const char	*head;

		if ( next ) {
			*next++ = 0;
		}
		TIKI_ExpandMacros( parse, line, expanded, sizeof( expanded ) );
		line = next;

		numTokens = TIKI_Tokenize( expanded, tokens, TIKI_MAX_LINE_TOKENS );
		if ( !numTokens ) {
			continue;
		}
		head = tokens[0];

		if ( !Q_stricmp( head, "$define" ) ) {
			if ( numTokens >= 3 && parse->numMacros < TIKI_MAX_MACROS ) {
				tikiMacro_t	*macro = &parse->macros[parse->numMacros++];

				Q_strncpyz( macro->name, tokens[1], sizeof( macro->name ) );
				Q_strncpyz( macro->value, tokens[2], sizeof( macro->value ) );
			}
			continue;
		}
		if ( !Q_stricmp( head, "$include" ) ) {
			if ( numTokens >= 2 ) {
				TIKI_ParseFile( parse, tokens[1], depth + 1 );
			}
			continue;
		}
		if ( !strcmp( head, "{" ) ) {
			parse->level++;
			continue;
		}
		if ( !strcmp( head, "}" ) ) {
			if ( parse->level > 0 ) {
				parse->level--;
			}
			if ( parse->level == 0 ) {
				parse->section = SECTION_NONE;
			}
			if ( parse->level == 1 ) {
				parse->sub = SUB_NONE;
			}
			continue;
		}

		if ( parse->level == 0 ) {
			if ( !Q_stricmp( head, "setup" ) ) {
				parse->section = SECTION_SETUP;
			} else if ( !Q_stricmp( head, "init" ) ) {
				parse->section = SECTION_INIT;
			} else if ( !Q_stricmp( head, "animations" ) ) {
				parse->section = SECTION_ANIMATIONS;
			} else {
				parse->section = SECTION_OTHER;	// includes the leading TIKI identifier
			}
			parse->sub = SUB_NONE;
			continue;
		}

		switch ( parse->section ) {
		case SECTION_SETUP:
			TIKI_ParseSetup( parse, tokens, numTokens );
			break;

		case SECTION_INIT:
			if ( parse->level == 1 ) {
				parse->sub = !Q_stricmp( head, "server" ) ? SUB_SERVER : SUB_CLIENT;
			} else if ( parse->level == 2 ) {
				TIKI_AddCommand( parse->sub == SUB_SERVER ? &tiki->serverInit : &tiki->clientInit, 0, tokens, numTokens );
			}
			break;

		case SECTION_ANIMATIONS:
			if ( parse->level == 1 ) {
				tikiAnim_t	*anim;
				int			i;

				if ( numTokens < 2 ) {
					break;
				}
				anim = TIKI_NewAnim( tiki );
				parse->animIndex = tiki->numAnims - 1;
				anim->weight = 1.0f;
				anim->crossblend = TIKI_DEFAULT_CROSSBLEND_MS;
				Q_strncpyz( anim->alias, tokens[0], sizeof( anim->alias ) );
				Q_strncpyz( anim->file, tokens[1], sizeof( anim->file ) );
				if ( tiki->path[0] ) {
					Com_sprintf( anim->fullname, sizeof( anim->fullname ), "%s/%s", tiki->path, tokens[1] );
				} else {
					Q_strncpyz( anim->fullname, tokens[1], sizeof( anim->fullname ) );
				}
				for ( i = 2 ; i < numTokens ; i++ ) {
					if ( !Q_stricmp( tokens[i], "default_angles" ) ) {
						anim->flags |= TIKI_ANIM_DEFAULT_ANGLES;
					} else if ( !Q_stricmp( tokens[i], "notimecheck" ) ) {
						anim->flags |= TIKI_ANIM_NO_TIMECHECK;
					} else if ( !Q_stricmp( tokens[i], "crossblend" ) && i + 1 < numTokens ) {
						anim->crossblend = (int)( atof( tokens[++i] ) * 1000 );
					}
				}
			} else if ( parse->level == 2 ) {
				parse->sub = !Q_stricmp( head, "server" ) ? SUB_SERVER : SUB_CLIENT;
				parse->inEffectBlock = qfalse;
				parse->lastFrame = 0;
			} else if ( parse->level == 3 && parse->animIndex >= 0 ) {
				tikiAnim_t			*anim = &tiki->anims[parse->animIndex];
				tikiCommandList_t	*list = parse->sub == SUB_SERVER ? &anim->serverCmds : &anim->clientCmds;

				// As the retail engine reads them (fakk2.exe 0x44b760): a client
				// effect spreads its parameters over the lines between a "(" line
				// and a ")" line, and each of those lines, the brackets included,
				// is a command of its own at the frame of the line above, with no
				// frame selector. The client game module needs it so: "(" and ")"
				// are commands (StartBlock, EndBlock in cg_commands.cpp), and ")"
				// is what makes the effect. Any other line starts with its frame.
				if ( !strcmp( head, "(" ) ) {
					parse->inEffectBlock = qtrue;
					TIKI_AddCommand( list, parse->lastFrame, tokens, numTokens );
				} else if ( !strcmp( head, ")" ) ) {
					parse->inEffectBlock = qfalse;
					TIKI_AddCommand( list, parse->lastFrame, tokens, numTokens );
				} else if ( parse->inEffectBlock ) {
					TIKI_AddCommand( list, parse->lastFrame, tokens, numTokens );
				} else {
					parse->lastFrame = TIKI_ParseFrameSelector( head );
					TIKI_AddCommand( list, parse->lastFrame, tokens + 1, numTokens - 1 );
				}
			}
			break;

		default:
			break;
		}
	}
}

static qboolean TIKI_ParseFile( tikiParse_t *parse, const char *filename, int depth ) {
	void	*buffer;
	char	*text;
	int		length;

	if ( depth > TIKI_MAX_INCLUDE_DEPTH ) {
		Com_Printf( "WARNING: %s: $include nested too deeply at %s\n", parse->tiki->name, filename );
		return qfalse;
	}

	length = (int)FS_ReadFile( filename, &buffer );
	if ( !buffer ) {
		return qfalse;
	}

	// FS_ReadFile terminates the buffer, but parsing edits it, so work on a copy
	text = TIKI_Alloc( length + 1 );
	memcpy( text, buffer, length );
	FS_FreeFile( buffer );

	TIKI_StripComments( text );
	TIKI_ParseText( parse, text, depth );
	free( text );
	return qtrue;
}

static qboolean TIKI_SurfaceMatches( const char *pattern, const char *name ) {
	const char	*star;

	if ( !Q_stricmp( pattern, "all" ) ) {
		return qtrue;
	}
	star = strchr( pattern, '*' );
	if ( star ) {
		return !Q_stricmpn( pattern, name, (int)( star - pattern ) );
	}
	return !Q_stricmp( pattern, name );
}

static int TIKI_SurfaceFlag( const char *keyword ) {
	if ( !Q_stricmp( keyword, "skin1" ) ) return 1 << 0;
	if ( !Q_stricmp( keyword, "skin2" ) ) return 1 << 1;
	if ( !Q_stricmp( keyword, "skin3" ) ) return ( 1 << 0 ) | ( 1 << 1 );
	if ( !Q_stricmp( keyword, "nodraw" ) ) return 1 << 2;
	if ( !Q_stricmp( keyword, "crossfade" ) ) return 1 << 6;
	if ( !Q_stricmp( keyword, "nodamage" ) ) return 1 << 7;
	if ( !Q_stricmp( keyword, "nomipmaps" ) ) return 1 << 8;
	if ( !Q_stricmp( keyword, "nopicmip" ) ) return 1 << 9;
	return 0;
}

// surface <name|pattern> shader <names...> | flags <keywords...> | damage <multiplier>
static void TIKI_ApplySurfaceCommands( tiki_t *tiki, const tikiCommandList_t *cmds ) {
	int		i, j, k;

	for ( i = 0 ; i < cmds->count ; i++ ) {
		const tikiCommand_t	*cmd = &cmds->cmds[i];

		if ( cmd->numArgs < 3 ) {
			continue;
		}
		for ( j = 0 ; j < tiki->numSurfaces ; j++ ) {
			tikiSurface_t	*surface = &tiki->surfaces[j];

			if ( !TIKI_SurfaceMatches( cmd->args[0], surface->name ) ) {
				continue;
			}
			if ( !Q_stricmp( cmd->args[1], "shader" ) ) {
				// further skins repeat the keyword, on the line ("shader <first>
				// shader <second>") or on lines of their own (obj_button.tik
				// names its four, red and green, inactive and lit); the first
				// named is skin 0
				for ( k = 2 ; k < cmd->numArgs && surface->numSkins < TIKI_MAX_SKINS ; k++ ) {
					if ( !Q_stricmp( cmd->args[k], "shader" ) ) {
						continue;
					}
					Q_strncpyz( surface->shader[surface->numSkins++], cmd->args[k], MAX_QPATH );
				}
			} else if ( !Q_stricmp( cmd->args[1], "flags" ) ) {
				for ( k = 2 ; k < cmd->numArgs ; k++ ) {
					surface->flags |= TIKI_SurfaceFlag( cmd->args[k] );
				}
			} else if ( !Q_stricmp( cmd->args[1], "damage" ) ) {
				surface->damageMultiplier = atof( cmd->args[2] );
			}
		}
	}
}

/*
===============
TIKI_Finish

Loads the files the definition names and derives the values that depend on
them.
===============
*/
static void TIKI_Finish( tiki_t *tiki, const tikiCommandList_t *surfaceCmds ) {
	qboolean	skeletal = tiki->skelName[0] != 0;
	qboolean	haveBounds = qfalse;
	qboolean	firstTan = qtrue;
	int			i, j;

	tiki->tagNames = TIKI_Alloc( SKEL_MAX_BONES * sizeof( *tiki->tagNames ) );

	if ( skeletal ) {
		if ( tiki->path[0] ) {
			Com_sprintf( tiki->skelFile, sizeof( tiki->skelFile ), "%s/%s", tiki->path, tiki->skelName );
		} else {
			Q_strncpyz( tiki->skelFile, tiki->skelName, sizeof( tiki->skelFile ) );
		}
		TIKI_LoadSkb( tiki, tiki->skelFile );
	}

	ClearBounds( tiki->mins, tiki->maxs );

	for ( i = 0 ; i < tiki->numAnims ; i++ ) {
		tikiAnim_t	*anim = &tiki->anims[i];
		void		*buffer;
		int			length;
		qboolean	ok;

		length = (int)FS_ReadFile( anim->fullname, &buffer );
		if ( !buffer ) {
			Com_DPrintf( "%s: dropping animation %s, couldn't load %s\n", tiki->name, anim->alias, anim->fullname );
			continue;
		}

		if ( TIKI_HasExtension( anim->fullname, ".ska" ) ) {
			ok = TIKI_LoadSka( anim, buffer, length );
		} else {
			ok = TIKI_LoadTan( tiki, anim, buffer, length, !skeletal && firstTan );
			if ( ok ) {
				firstTan = qfalse;
			}
		}
		FS_FreeFile( buffer );
		anim->fileSize = ok ? length : 0;

		if ( !ok ) {
			free( anim->frames );
			free( anim->tagData );
			free( anim->boneData );
			anim->frames = NULL;
			anim->tagData = NULL;
			anim->boneData = NULL;
			anim->numFrames = anim->numTags = anim->numBones = 0;
			continue;
		}

		// mins and maxs are accumulated separately: a frame without geometry
		// stores mins above its maxs and must not widen the box
		for ( j = 0 ; j < anim->numFrames ; j++ ) {
			int		k;

			for ( k = 0 ; k < 3 ; k++ ) {
				if ( anim->frames[j].mins[k] < tiki->mins[k] ) {
					tiki->mins[k] = anim->frames[j].mins[k];
				}
				if ( anim->frames[j].maxs[k] > tiki->maxs[k] ) {
					tiki->maxs[k] = anim->frames[j].maxs[k];
				}
			}
			haveBounds = qtrue;
		}

		for ( j = 0 ; j < anim->serverCmds.count ; j++ ) {
			if ( anim->serverCmds.cmds[j].frame == TIKI_FRAME_CMD_LAST ) {
				anim->serverCmds.cmds[j].frame = anim->numFrames - 1;
			}
		}
		for ( j = 0 ; j < anim->clientCmds.count ; j++ ) {
			if ( anim->clientCmds.cmds[j].frame == TIKI_FRAME_CMD_LAST ) {
				anim->clientCmds.cmds[j].frame = anim->numFrames - 1;
			}
		}
	}

	// An animation that failed to load is removed, so later animations move
	// down a number. The retail engine does the same: its cached definitions
	// count only the animations whose files exist.
	for ( i = 0, j = 0 ; i < tiki->numAnims ; i++ ) {
		tikiAnim_t	*anim = &tiki->anims[i];

		if ( !anim->frames ) {
			TIKI_FreeCommands( &anim->serverCmds );
			TIKI_FreeCommands( &anim->clientCmds );
			continue;
		}
		if ( j != i ) {
			tiki->anims[j] = *anim;
		}
		j++;
	}
	tiki->numAnims = j;

	// Animations are numbered in name order, compared the way Q_stricmp does
	// (case folded to upper, so '_' sorts after letters). All 205 retail
	// models with more than one animation are cached in exactly this order,
	// and the numbers are shared with the game modules. Insertion sort keeps
	// definitions with the same name in file order.
	for ( i = 1 ; i < tiki->numAnims ; i++ ) {
		tikiAnim_t	moved = tiki->anims[i];

		for ( j = i ; j > 0 && Q_stricmp( tiki->anims[j - 1].alias, moved.alias ) > 0 ; j-- ) {
			tiki->anims[j] = tiki->anims[j - 1];
		}
		tiki->anims[j] = moved;
	}
	// models without geometry report frames whose mins exceed their maxs
	if ( !haveBounds || tiki->mins[0] > tiki->maxs[0] || tiki->mins[1] > tiki->maxs[1] || tiki->mins[2] > tiki->maxs[2] ) {
		VectorSet( tiki->mins, -4, -4, -4 );
		VectorSet( tiki->maxs, 4, 4, 4 );
	} else {
		VectorAdd( tiki->mins, tiki->loadOrigin, tiki->mins );
		VectorAdd( tiki->maxs, tiki->loadOrigin, tiki->maxs );
	}

	if ( !tiki->radiusSet ) {
		tiki->radius = RadiusFromBounds( tiki->mins, tiki->maxs ) * tiki->loadScale;
	}

	TIKI_ApplySurfaceCommands( tiki, surfaceCmds );
}

tiki_t *TIKI_Register( const char *name ) {
	tikiParse_t	*parse;
	tiki_t		*tiki;

	if ( !name || !name[0] ) {
		return NULL;
	}

	for ( tiki = tiki_list ; tiki ; tiki = tiki->next ) {
		if ( !Q_stricmp( tiki->name, name ) ) {
			return tiki->valid ? tiki : NULL;
		}
	}

	tiki = TIKI_Alloc( sizeof( *tiki ) );
	Q_strncpyz( tiki->name, name, sizeof( tiki->name ) );
	tiki->loadScale = 1.0f;
	tiki->lodScale = 1.0f;
	// the path starts as the definition file's own directory, until a path
	// line replaces it (TIKI_LoadTIKIfile, fakk2.exe 0x44ca70, through 0x44a440)
	{
		const char	*slash = strrchr( name, '/' );
		const char	*backslash = strrchr( name, '\\' );

		if ( backslash > slash ) {
			slash = backslash;
		}
		if ( slash && slash - name < (int)sizeof( tiki->path ) ) {
			Q_strncpyz( tiki->path, name, (int)( slash - name ) + 1 );
		}
	}
	tiki->next = tiki_list;
	tiki_list = tiki;

	parse = TIKI_Alloc( sizeof( *parse ) );
	parse->tiki = tiki;
	parse->animIndex = -1;

	if ( !TIKI_ParseFile( parse, name, 0 ) ) {
		Com_Printf( "Can't find %s\n", name );
		Com_Printf( "Tiki:LoadFile Couldn't load %s\n", name );
	} else {
		TIKI_Finish( tiki, &parse->surfaceCmds );
		tiki->valid = qtrue;
	}

	TIKI_FreeCommands( &parse->surfaceCmds );
	free( parse );

	return tiki->valid ? tiki : NULL;
}

/*
===========================================================================

Queries

Every query accepts a NULL definition and out-of-range numbers and answers
with the value that means "nothing": the game calls these with whatever the
map and scripts supplied.

===========================================================================
*/

static const tikiAnim_t *TIKI_GetAnim( const tiki_t *tiki, int animnum ) {
	if ( !tiki || animnum < 0 || animnum >= tiki->numAnims ) {
		return NULL;
	}
	return &tiki->anims[animnum];
}

static const tikiAnimFrame_t *TIKI_GetFrame( const tiki_t *tiki, int animnum, int framenum ) {
	const tikiAnim_t	*anim = TIKI_GetAnim( tiki, animnum );

	if ( !anim || framenum < 0 || framenum >= anim->numFrames ) {
		return NULL;
	}
	return &anim->frames[framenum];
}

const char *TIKI_Name( const tiki_t *tiki ) {
	return tiki ? tiki->name : "";
}

struct AliasList_s **TIKI_AliasList( tiki_t *tiki ) {
	return tiki ? &tiki->aliases : NULL;
}

int TIKI_NumAnims( const tiki_t *tiki ) {
	return tiki ? tiki->numAnims : 0;
}

const char *TIKI_SkelFile( const tiki_t *tiki ) {
	return tiki ? tiki->skelFile : "";
}

qboolean TIKI_IsSkeletal( const tiki_t *tiki ) {
	return tiki && tiki->skelName[0];
}

const char *TIKI_Path( const tiki_t *tiki ) {
	return tiki ? tiki->path : "";
}

float TIKI_Scale( const tiki_t *tiki ) {
	return tiki ? tiki->loadScale : 1.0f;
}

void TIKI_Origin( const tiki_t *tiki, vec3_t origin ) {
	if ( tiki ) {
		VectorCopy( tiki->loadOrigin, origin );
	} else {
		VectorClear( origin );
	}
}

void TIKI_LightOffset( const tiki_t *tiki, vec3_t offset ) {
	if ( tiki ) {
		VectorCopy( tiki->lightOffset, offset );
	} else {
		VectorClear( offset );
	}
}

const char *TIKI_Anim_File( const tiki_t *tiki, int animnum ) {
	if ( !tiki || animnum < 0 || animnum >= tiki->numAnims ) {
		return "";
	}
	return tiki->anims[animnum].fullname;
}

const char *TIKI_Surface_Shader( const tiki_t *tiki, int num, int skin ) {
	if ( !tiki || num < 0 || num >= tiki->numSurfaces || skin < 0 || skin >= tiki->surfaces[num].numSkins ) {
		return "";
	}
	return tiki->surfaces[num].shader[skin];
}

int TIKI_NumSurfaces( const tiki_t *tiki ) {
	return tiki ? tiki->numSurfaces : 0;
}

int TIKI_NumTags( const tiki_t *tiki ) {
	return tiki ? tiki->numTags : 0;
}

int TIKI_NumSkins( const tiki_t *tiki ) {
	int		i, most = 0;

	if ( !tiki ) {
		return 0;
	}
	for ( i = 0 ; i < tiki->numSurfaces ; i++ ) {
		if ( tiki->surfaces[i].numSkins > most ) {
			most = tiki->surfaces[i].numSkins;
		}
	}
	return most;
}

// copies commands into the fixed-size structure the game modules use, which
// holds fewer commands and arguments than a definition may contain
static void TIKI_OutputCommand( const tikiCommand_t *cmd, tiki_cmd_t *out ) {
	tiki_singlecmd_t	*single;
	int					i;

	if ( out->num_cmds >= TIKI_CMD_MAX_CMDS ) {
		return;
	}
	single = &out->cmds[out->num_cmds++];
	single->num_args = cmd->numArgs < TIKI_CMD_MAX_ARGS ? cmd->numArgs : TIKI_CMD_MAX_ARGS;
	for ( i = 0 ; i < single->num_args ; i++ ) {
		single->args[i] = cmd->args[i];
	}
}

qboolean TIKI_InitCommands( const tiki_t *tiki, qboolean server, tiki_cmd_t *out ) {
	const tikiCommandList_t	*list;
	int						i;

	out->num_cmds = 0;
	if ( !tiki ) {
		return qfalse;
	}
	list = server ? &tiki->serverInit : &tiki->clientInit;
	for ( i = 0 ; i < list->count ; i++ ) {
		TIKI_OutputCommand( &list->cmds[i], out );
	}
	// success means the model is valid, even when it defines no commands
	return qtrue;
}

float TIKI_Radius( const tiki_t *tiki ) {
	return tiki ? tiki->radius : 0;
}

void TIKI_CalculateBounds( const tiki_t *tiki, float scale, vec3_t mins, vec3_t maxs ) {
	if ( !tiki ) {
		VectorClear( mins );
		VectorClear( maxs );
		return;
	}
	VectorScale( tiki->mins, tiki->loadScale * scale, mins );
	VectorScale( tiki->maxs, tiki->loadScale * scale, maxs );
}

const char *TIKI_Anim_NameForNum( const tiki_t *tiki, int animnum ) {
	const tikiAnim_t	*anim = TIKI_GetAnim( tiki, animnum );

	return anim ? anim->alias : "";
}

int TIKI_Anim_NumForName( const tiki_t *tiki, const char *name ) {
	int		i;

	if ( !tiki || !name ) {
		return -1;
	}
	for ( i = 0 ; i < tiki->numAnims ; i++ ) {
		if ( !Q_stricmp( tiki->anims[i].alias, name ) ) {
			return i;
		}
	}
	return -1;
}

/*
===============
TIKI_Anim_Random

Picks among the animations whose name starts with the given one, weighted by
their weights. Variants are written as the name plus a suffix (idle1, idle2);
a name continuing with an underscore is a different animation, not a variant.
At most TIKI_MAX_RANDOM_ANIMS variants are considered. Matches fakk2.exe.
===============
*/
#define	TIKI_MAX_RANDOM_ANIMS	16

int TIKI_Anim_Random( const tiki_t *tiki, const char *name ) {
	int		matches[TIKI_MAX_RANDOM_ANIMS];
	float	weights[TIKI_MAX_RANDOM_ANIMS];
	float	total = 0;
	float	pick;
	int		numMatches = 0;
	int		len, i;

	if ( !tiki || !name || !name[0] ) {
		return -1;
	}
	len = strlen( name );
	for ( i = 0 ; i < tiki->numAnims ; i++ ) {
		const tikiAnim_t	*anim = &tiki->anims[i];
		int					cmp = Q_stricmpn( anim->alias, name, len );

		if ( cmp > 0 ) {
			break;		// sorted by name: nothing later can match
		}
		if ( cmp < 0 || anim->alias[len] == '_' ) {
			continue;
		}
		if ( numMatches == TIKI_MAX_RANDOM_ANIMS ) {
			break;
		}
		matches[numMatches] = i;
		weights[numMatches] = anim->weight;
		total += anim->weight;
		numMatches++;
	}
	if ( !numMatches ) {
		return -1;
	}

	pick = (float)Com_UnseededRand() * total * ( 1.0f / 32767.0f );
	for ( i = 0 ; i < numMatches - 1 ; i++ ) {
		pick -= weights[i];
		if ( pick <= 0 ) {
			break;
		}
	}
	return matches[i];
}

int TIKI_Anim_NumFrames( const tiki_t *tiki, int animnum ) {
	const tikiAnim_t	*anim = TIKI_GetAnim( tiki, animnum );

	return anim ? anim->numFrames : 0;
}

float TIKI_Anim_Time( const tiki_t *tiki, int animnum ) {
	const tikiAnim_t	*anim = TIKI_GetAnim( tiki, animnum );

	return anim ? anim->totalTime : 0;
}

void TIKI_Anim_Delta( const tiki_t *tiki, int animnum, vec3_t delta ) {
	const tikiAnim_t	*anim = TIKI_GetAnim( tiki, animnum );

	if ( !anim ) {
		VectorClear( delta );
		return;
	}
	VectorScale( anim->totalDelta, tiki->loadScale, delta );
}

int TIKI_Anim_Flags( const tiki_t *tiki, int animnum ) {
	const tikiAnim_t	*anim = TIKI_GetAnim( tiki, animnum );

	return anim ? anim->flags : 0;
}

// milliseconds, the definition's cached value (fakk2.exe 0x44e3b0 reads it straight)
int TIKI_Anim_CrossblendTime( const tiki_t *tiki, int animnum ) {
	const tikiAnim_t	*anim = TIKI_GetAnim( tiki, animnum );

	return anim ? anim->crossblend : 0;
}

qboolean TIKI_Anim_HasCommands( const tiki_t *tiki, int animnum, qboolean server ) {
	const tikiAnim_t	*anim = TIKI_GetAnim( tiki, animnum );

	if ( !anim ) {
		return qfalse;
	}
	return ( server ? anim->serverCmds.count : anim->clientCmds.count ) > 0;
}

qboolean TIKI_Frame_Commands( const tiki_t *tiki, int animnum, int framenum, qboolean server, tiki_cmd_t *out ) {
	const tikiAnim_t		*anim = TIKI_GetAnim( tiki, animnum );
	const tikiCommandList_t	*list;
	int						i;

	out->num_cmds = 0;
	if ( !anim ) {
		return qfalse;
	}
	list = server ? &anim->serverCmds : &anim->clientCmds;
	for ( i = 0 ; i < list->count ; i++ ) {
		int		frame = list->cmds[i].frame;

		// commands marked "every" run on each real frame, not on entry or exit
		if ( frame == framenum || ( frame == TIKI_FRAME_CMD_EVERY_FRAME && framenum >= 0 ) ) {
			TIKI_OutputCommand( &list->cmds[i], out );
		}
	}
	return out->num_cmds > 0;
}

void TIKI_Frame_Delta( const tiki_t *tiki, int animnum, int framenum, vec3_t delta ) {
	const tikiAnimFrame_t	*frame = TIKI_GetFrame( tiki, animnum, framenum );

	if ( !frame ) {
		VectorClear( delta );
		return;
	}
	VectorScale( frame->delta, tiki->loadScale, delta );
}

float TIKI_Frame_Time( const tiki_t *tiki, int animnum, int framenum ) {
	const tikiAnimFrame_t	*frame = TIKI_GetFrame( tiki, animnum, framenum );

	return frame ? frame->time : 0;
}

void TIKI_Frame_Bounds( const tiki_t *tiki, int animnum, int framenum, float scale, vec3_t mins, vec3_t maxs ) {
	const tikiAnimFrame_t	*frame = TIKI_GetFrame( tiki, animnum, framenum );

	if ( !frame ) {
		VectorClear( mins );
		VectorClear( maxs );
		return;
	}
	VectorAdd( frame->mins, tiki->loadOrigin, mins );
	VectorAdd( frame->maxs, tiki->loadOrigin, maxs );
	VectorScale( mins, tiki->loadScale * scale, mins );
	VectorScale( maxs, tiki->loadScale * scale, maxs );
}

int TIKI_Surface_NameToNum( const tiki_t *tiki, const char *name ) {
	int		i;

	if ( !tiki || !name ) {
		return -1;
	}
	for ( i = 0 ; i < tiki->numSurfaces ; i++ ) {
		if ( !Q_stricmp( tiki->surfaces[i].name, name ) ) {
			return i;
		}
	}
	return -1;
}

const char *TIKI_Surface_NumToName( const tiki_t *tiki, int num ) {
	if ( !tiki || num < 0 || num >= tiki->numSurfaces ) {
		return "";
	}
	return tiki->surfaces[num].name;
}

int TIKI_Surface_Flags( const tiki_t *tiki, int num ) {
	if ( !tiki || num < 0 || num >= tiki->numSurfaces ) {
		return 0;
	}
	return tiki->surfaces[num].flags;
}

int TIKI_Surface_NumSkins( const tiki_t *tiki, int num ) {
	if ( !tiki || num < 0 || num >= tiki->numSurfaces ) {
		return 0;
	}
	return tiki->surfaces[num].numSkins;
}

int TIKI_Tag_NumForName( const tiki_t *tiki, const char *name ) {
	int		i;

	if ( !tiki || !name ) {
		return -1;
	}
	for ( i = 0 ; i < tiki->numTags ; i++ ) {
		if ( !Q_stricmp( tiki->tagNames[i], name ) ) {
			return i;
		}
	}
	return -1;
}

const char *TIKI_Tag_NameForNum( const tiki_t *tiki, int num ) {
	if ( !tiki || num < 0 || num >= tiki->numTags ) {
		return "";
	}
	return tiki->tagNames[num];
}

// rotation matrix for a unit quaternion stored x, y, z, w
static void TIKI_QuatToAxis( const float q[4], vec3_t axis[3] ) {
	float	x2 = q[0] + q[0], y2 = q[1] + q[1], z2 = q[2] + q[2];
	float	xx = q[0] * x2, xy = q[0] * y2, xz = q[0] * z2;
	float	yy = q[1] * y2, yz = q[1] * z2, zz = q[2] * z2;
	float	wx = q[3] * x2, wy = q[3] * y2, wz = q[3] * z2;

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

// r = p * q, Hamilton product on x y z w (as fakk2.exe 0x44fe40 composes bones)
static void TIKI_QuatMultiply( const float p[4], const float q[4], float r[4] ) {
	r[0] = p[3] * q[0] + p[0] * q[3] + p[1] * q[2] - p[2] * q[1];
	r[1] = p[3] * q[1] - p[0] * q[2] + p[1] * q[3] + p[2] * q[0];
	r[2] = p[3] * q[2] + p[0] * q[1] - p[1] * q[0] + p[2] * q[3];
	r[3] = p[3] * q[3] - p[0] * q[0] - p[1] * q[1] - p[2] * q[2];
}

// a vector in a bone's frame, in its parent's (fakk2.exe 0x43c800)
static void TIKI_RotateByAxis( const vec3_t in, const vec3_t axis[3], vec3_t out ) {
	out[0] = in[0] * axis[0][0] + in[1] * axis[1][0] + in[2] * axis[2][0];
	out[1] = in[0] * axis[0][1] + in[1] * axis[1][1] + in[2] * axis[2][1];
	out[2] = in[0] * axis[0][2] + in[1] * axis[1][2] + in[2] * axis[2][2];
}

// from towards to by t, the shorter way round (fakk2.exe 0x43ca60)
static void TIKI_QuatSlerp( const float from[4], const float to[4], float t, float out[4] ) {
	float	to1[4];
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
			for ( i = 0 ; i < 4 ; i++ ) {
				out[i] = to[i];
			}
			return;
		}
		for ( i = 0 ; i < 4 ; i++ ) {
			to1[i] = to[i];
		}
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

/*
===============================================================================

Tag placement, as fakk2.exe does it.

TIKI_Tag_Orientation (0x44fe40), TIKI_Tag_OrientationEx (0x44fc30, skeletal
part 0x44ee40) and the client's Tag_LerpedOrientation (0x451c90, skeletal
part 0x4505e0, vertex part 0x451a30) share one scheme. For a skeletal model
the tag is a bone: the bones from it up to the root each get a quaternion and
an offset from the animations that move them, and are composed root first. A
bone's rotation is its quaternion times its parent's (Hamilton product, x y z
w), then times the entity's bone controller for it if there is one; its origin
is the parent's plus its offset turned by the parent's axes. The root adds the
definition's origin, and the result is scaled by the definition's scale and
the entity's. When a torso animation plays with the legs animation, bones
flagged for the legs take the legs animation and the rest the torso's; either
alone moves every bone. A frame is blended towards the old frame by backlerp
and then towards the cross-blend frame by the cross-blend lerp, quaternions
by slerp and offsets linearly. For a vertex-animated model the tag's placement
is read from the frame, blended towards the old frame's the same way.

===============================================================================
*/

typedef struct {
	float	quat[4];
	vec3_t	offset;
} tikiBonePose_t;

// a frame of a skeletal animation
typedef struct {
	const tikiAnim_t	*anim;
	int					frame;
} tikiFrameRef_t;

#define	TIKI_BONES_ALL		0
#define	TIKI_BONES_LEGS		1
#define	TIKI_BONES_TORSO	2

static qboolean TIKI_SkelFrame( const tiki_t *tiki, int animnum, int framenum, tikiFrameRef_t *ref ) {
	const tikiAnim_t	*anim = TIKI_GetAnim( tiki, animnum );

	ref->anim = NULL;
	ref->frame = 0;
	if ( !anim || !anim->boneData || framenum < 0 || framenum >= anim->numFrames ) {
		return qfalse;
	}
	ref->anim = anim;
	ref->frame = framenum;
	return qtrue;
}

static qboolean TIKI_SameFrame( const tikiFrameRef_t *a, const tikiFrameRef_t *b ) {
	return a->anim == b->anim && a->frame == b->frame;
}

static void TIKI_SkelBone( const tikiFrameRef_t *ref, int bone, float quat[4], vec3_t offset ) {
	const short	*s;
	int			i;

	if ( !ref->anim || bone >= ref->anim->numBones ) {
		quat[0] = quat[1] = quat[2] = 0;
		quat[3] = 1;
		VectorClear( offset );
		return;
	}
	s = &ref->anim->boneData[( ref->frame * ref->anim->numBones + bone ) * 8];
	for ( i = 0 ; i < 4 ; i++ ) {
		quat[i] = s[i] * SKEL_QUAT_SCALE;
	}
	for ( i = 0 ; i < 3 ; i++ ) {
		offset[i] = s[4 + i] * SKEL_OFFSET_SCALE;
	}
}

/*
===============
TIKI_SkelBlendBones

Gives the bones of the chain that belong to the part their quaternion and
offset: the current frame's, blended towards the old frame's by backlerp
(0x4505e0), then towards the cross-blend frame's by lerp (0x44ee40).
===============
*/
static void TIKI_SkelBlendBones( const tiki_t *tiki, const int *chain, int depth, int part,
	const tikiFrameRef_t *cur, const tikiFrameRef_t *old, float backlerp,
	const tikiFrameRef_t *cross, float lerp, tikiBonePose_t *pose, qboolean *set ) {
	int		i;

	for ( i = 0 ; i < depth ; i++ ) {
		int				bone = chain[i];
		tikiBonePose_t	*p = &pose[bone];
		float			quat[4];
		vec3_t			offset;

		if ( part == TIKI_BONES_LEGS && !tiki->legBones[bone] ) {
			continue;
		}
		if ( part == TIKI_BONES_TORSO && tiki->legBones[bone] ) {
			continue;
		}

		TIKI_SkelBone( cur, bone, p->quat, p->offset );
		if ( old ) {
			TIKI_SkelBone( old, bone, quat, offset );
			TIKI_QuatSlerp( quat, p->quat, 1.0f - backlerp, p->quat );
			VectorScale( p->offset, 1.0f - backlerp, p->offset );
			VectorMA( p->offset, backlerp, offset, p->offset );
		}
		if ( cross ) {
			TIKI_SkelBone( cross, bone, quat, offset );
			TIKI_QuatSlerp( quat, p->quat, lerp, p->quat );
			VectorScale( p->offset, lerp, p->offset );
			VectorMA( p->offset, 1.0f - lerp, offset, p->offset );
		}
		set[bone] = qtrue;
	}
}

/*
===============
TIKI_SkelCompose

Composes the chain root first into the tag's placement (0x44fe40).
===============
*/
static void TIKI_SkelCompose( const tiki_t *tiki, const int *chain, int depth, const tikiBonePose_t *pose,
	const int *bone_tag, const vec4_t *bone_quat, float scale, orientation_t *out ) {
	float	acc[4] = { 0, 0, 0, 1 };
	vec3_t	axis[3];
	vec3_t	origin;
	int		i, c;

	VectorClear( origin );
	AxisClear( axis );

	for ( i = depth - 1 ; i >= 0 ; i-- ) {
		int						bone = chain[i];
		const tikiBonePose_t	*p = &pose[bone];
		float					turned[4];

		if ( i == depth - 1 ) {
			Vector4Copy( p->quat, acc );
			VectorAdd( p->offset, tiki->loadOrigin, origin );
		} else {
			TIKI_QuatMultiply( p->quat, acc, turned );
			Vector4Copy( turned, acc );
			TIKI_RotateByAxis( p->offset, axis, turned );
			VectorAdd( origin, turned, origin );
		}

		if ( bone_tag && bone_quat ) {
			for ( c = 0 ; c < NUM_BONE_CONTROLLERS ; c++ ) {
				if ( bone_tag[c] == bone ) {
					TIKI_QuatMultiply( acc, bone_quat[c], turned );
					Vector4Copy( turned, acc );
				}
			}
		}
		TIKI_QuatToAxis( acc, axis );
	}

	VectorScale( origin, scale * tiki->loadScale, out->origin );
	AxisCopy( axis, out->axis );
}

/*
===============
TIKI_SkelTagOrientation

The tag of a skeletal model for the entity's animation state. Bones that no
animation of the state moves (retail leaves them zero) take the main
animation's frame.
===============
*/
static qboolean TIKI_SkelTagOrientation( const tiki_t *tiki, const tikiAnimState_t *st, int tagnum, orientation_t *out ) {
	tikiBonePose_t	pose[SKEL_MAX_BONES];
	qboolean		set[SKEL_MAX_BONES];
	int				chain[SKEL_MAX_BONES];
	int				depth = 0;
	tikiFrameRef_t	cur, old, cross;
	int				bone, i;
	qboolean		uselegs = st->uselegs, usetorso = st->usetorso;

	if ( !tiki->boneParents || tagnum < 0 || tagnum >= tiki->numTags ) {
		return qfalse;
	}
	bone = tagnum;
	while ( bone >= 0 && depth < SKEL_MAX_BONES ) {
		chain[depth++] = bone;
		bone = tiki->boneParents[bone];
	}
	Com_Memset( set, 0, sizeof( set ) );

	if ( !uselegs && !usetorso ) {
		uselegs = qtrue;
	}
	if ( uselegs ) {
		if ( !TIKI_SkelFrame( tiki, st->anim, st->frame, &cur ) ) {
			uselegs = qfalse;
		} else {
			float		backlerp = st->backlerp;
			qboolean	haveOld = TIKI_SkelFrame( tiki, st->oldanim, st->oldframe, &old ) && !TIKI_SameFrame( &old, &cur ) && backlerp != 0;
			qboolean	haveCross = st->crossblend_lerp >= 0 && TIKI_SkelFrame( tiki, st->crossblend_anim, st->crossblend_frame, &cross ) && !TIKI_SameFrame( &cross, &cur );

			TIKI_SkelBlendBones( tiki, chain, depth, usetorso ? TIKI_BONES_LEGS : TIKI_BONES_ALL,
				&cur, haveOld ? &old : NULL, backlerp, haveCross ? &cross : NULL, st->crossblend_lerp, pose, set );
		}
	}
	if ( usetorso ) {
		if ( !TIKI_SkelFrame( tiki, st->torso_anim, st->torso_frame, &cur ) ) {
			usetorso = qfalse;
		} else {
			float		backlerp = st->torso_backlerp;
			qboolean	haveOld = TIKI_SkelFrame( tiki, st->torso_oldanim, st->torso_oldframe, &old ) && !TIKI_SameFrame( &old, &cur ) && backlerp != 0;
			qboolean	haveCross = st->torso_crossblend_lerp >= 0 && TIKI_SkelFrame( tiki, st->torso_crossblend_anim, st->torso_crossblend_frame, &cross ) && !TIKI_SameFrame( &cross, &cur );

			TIKI_SkelBlendBones( tiki, chain, depth, uselegs ? TIKI_BONES_TORSO : TIKI_BONES_ALL,
				&cur, haveOld ? &old : NULL, backlerp, haveCross ? &cross : NULL, st->torso_crossblend_lerp, pose, set );
		}
	}

	// whatever is left over
	for ( i = 0 ; i < depth ; i++ ) {
		if ( !set[chain[i]] ) {
			if ( !TIKI_SkelFrame( tiki, st->anim, st->frame, &cur ) && !TIKI_SkelFrame( tiki, st->torso_anim, st->torso_frame, &cur ) ) {
				return qfalse;
			}
			TIKI_SkelBlendBones( tiki, chain, depth, TIKI_BONES_ALL, &cur, NULL, 0, NULL, 0, pose, set );
			break;
		}
	}

	TIKI_SkelCompose( tiki, chain, depth, pose, st->bone_tag, st->bone_quat, st->scale, out );
	return qtrue;
}

/*
===============
TIKI_TanTagOrientation

The tag of a vertex-animated model, blended towards the old frame's by
backlerp (0x451a30).
===============
*/
static qboolean TIKI_TanTagOrientation( const tiki_t *tiki, const tikiAnimState_t *st, int tagnum, orientation_t *out ) {
	const tikiAnim_t	*anim = TIKI_GetAnim( tiki, st->anim );
	const tikiAnim_t	*oldAnim = TIKI_GetAnim( tiki, st->oldanim );
	const float			*data, *oldData;
	float				backlerp = st->backlerp, frontlerp;
	int					i;

	if ( !anim || !anim->tagData || st->frame < 0 || st->frame >= anim->numFrames || tagnum < 0 || tagnum >= anim->numTags ) {
		return qfalse;
	}
	data = &anim->tagData[( tagnum * anim->numFrames + st->frame ) * 12];
	if ( !oldAnim || !oldAnim->tagData || st->oldframe < 0 || st->oldframe >= oldAnim->numFrames || tagnum >= oldAnim->numTags
		|| ( oldAnim == anim && st->oldframe == st->frame ) ) {
		oldData = data;
		backlerp = 0;
	} else {
		oldData = &oldAnim->tagData[( tagnum * oldAnim->numFrames + st->oldframe ) * 12];
	}
	frontlerp = 1.0f - backlerp;

	for ( i = 0 ; i < 3 ; i++ ) {
		out->origin[i] = tiki->loadOrigin[i] + data[i] * frontlerp + oldData[i] * backlerp;
		out->axis[0][i] = data[3 + i] * frontlerp + oldData[3 + i] * backlerp;
		out->axis[1][i] = data[6 + i] * frontlerp + oldData[6 + i] * backlerp;
		out->axis[2][i] = data[9 + i] * frontlerp + oldData[9 + i] * backlerp;
	}
	VectorScale( out->origin, tiki->loadScale * st->scale, out->origin );
	if ( backlerp ) {
		VectorNormalize( out->axis[0] );
		VectorNormalize( out->axis[1] );
		VectorNormalize( out->axis[2] );
	}
	return qtrue;
}

/*
===============
TIKI_Tag_OrientationEx

The tag's placement for an animation state. An invalid state gives a zero
origin and the identity axes.
===============
*/
orientation_t TIKI_Tag_OrientationEx( const tiki_t *tiki, const tikiAnimState_t *st, int tagnum ) {
	const tikiAnim_t	*anim = TIKI_GetAnim( tiki, st->anim );
	orientation_t		orient;
	qboolean			ok = qfalse;

	VectorClear( orient.origin );
	AxisClear( orient.axis );

	if ( anim ) {
		if ( anim->boneData ) {
			ok = TIKI_SkelTagOrientation( tiki, st, tagnum, &orient );
		} else if ( anim->tagData ) {
			ok = TIKI_TanTagOrientation( tiki, st, tagnum, &orient );
		}
	}
	if ( !ok ) {
		VectorClear( orient.origin );
		AxisClear( orient.axis );
	}
	return orient;
}

/*
===============
TIKI_AnimStateFrame

An animation state showing one frame of one animation, as Tag_Orientation
and Tag_OrientationEx see it (nothing to blend towards).
===============
*/
void TIKI_AnimStateFrame( tikiAnimState_t *st, int anim, int frame, float scale, const int *bone_tag, const vec4_t *bone_quat ) {
	Com_Memset( st, 0, sizeof( *st ) );
	st->anim = st->oldanim = anim;
	st->frame = st->oldframe = frame;
	st->crossblend_lerp = -1;
	st->torso_crossblend_lerp = -1;
	st->uselegs = qtrue;
	st->scale = scale;
	st->bone_tag = bone_tag;
	st->bone_quat = bone_quat;
}

orientation_t TIKI_Tag_Orientation( const tiki_t *tiki, int animnum, int framenum, int tagnum, float scale, const int *bone_tag, const vec4_t *bone_quat ) {
	tikiAnimState_t	st;

	TIKI_AnimStateFrame( &st, animnum, framenum, scale, bone_tag, bone_quat );
	return TIKI_Tag_OrientationEx( tiki, &st, tagnum );
}

/*
===========================================================================

Console commands

The retail client's tiki, tikilist and cachealltikis (fakk2.exe 0x421430,
0x451e00, 0x452490), printed as retail prints them. Retail's sizes are those
of its own structures; here an animation is counted at its file's length,
as retail counts it, and a definition at what this module keeps for it.

===========================================================================
*/

static int QDECL TIKI_CompareAnimAliases( const void *a, const void *b ) {
	return Q_stricmp( ( *(const tikiAnim_t * const *)a )->alias, ( *(const tikiAnim_t * const *)b )->alias );
}

/*
===============
TIKI_PrintInfo

What "tiki" prints (0x451f30): the animations by alias, the surfaces with
their triangles and skins, the tags or the bones, and the totals. Retail
prints a vertex-animated surface's triangle count twice, as its vertex
count too.
===============
*/
static void TIKI_PrintInfo( const tiki_t *tiki ) {
	const tikiAnim_t	**sorted;
	int					totalSize = 0, totalFrames = 0, totalTris = 0;
	int					i, j;
	qboolean			skeletal = tiki->boneParents != NULL;

	Com_Printf( "MODEL: %s\n", tiki->name );
	Com_Printf( "Total Anims: %i\n", tiki->numAnims );

	sorted = TIKI_Alloc( ( tiki->numAnims ? tiki->numAnims : 1 ) * sizeof( *sorted ) );
	for ( i = 0 ; i < tiki->numAnims ; i++ ) {
		sorted[i] = &tiki->anims[i];
	}
	qsort( (void *)sorted, tiki->numAnims, sizeof( *sorted ), TIKI_CompareAnimAliases );
	for ( i = 0 ; i < tiki->numAnims ; i++ ) {
		const tikiAnim_t	*anim = sorted[i];

		Com_Printf( "   %s -> %s : Frames %d Time %.2f Size %i Delta %.2f %.2f %.2f\n", anim->alias, anim->file,
			anim->numFrames, anim->totalTime, anim->fileSize, anim->totalDelta[0], anim->totalDelta[1], anim->totalDelta[2] );
		totalSize += anim->fileSize;
		totalFrames += anim->numFrames;
	}
	free( (void *)sorted );

	Com_Printf( "Surfaces:  %i\n", tiki->numSurfaces );
	for ( i = 0 ; i < tiki->numSurfaces ; i++ ) {
		const tikiSurface_t	*surf = &tiki->surfaces[i];

		if ( skeletal ) {
			Com_Printf( "  Surface %s Triangles %i skins", surf->name, surf->numTriangles );
		} else {
			Com_Printf( "  Surface %s Vertices %i Triangles %i skins", surf->name, surf->numTriangles, surf->numTriangles );
		}
		for ( j = 0 ; j < surf->numSkins ; j++ ) {
			Com_Printf( " %s", surf->shader[j] );
		}
		Com_Printf( "\n" );
		totalTris += surf->numTriangles;
	}

	if ( skeletal ) {
		Com_Printf( "Bones:  %i\n", tiki->numTags );
		for ( i = 0 ; i < tiki->numTags ; i++ ) {
			Com_Printf( "  %d : %s -> %d\n", i, tiki->tagNames[i], tiki->boneParents[i] );
		}
	} else {
		Com_Printf( "Tags:  %i\n", tiki->numTags );
		for ( i = 0 ; i < tiki->numTags ; i++ ) {
			Com_Printf( "  %s\n", tiki->tagNames[i] );
		}
	}

	Com_Printf( "\n" );
	Com_Printf( "Total Triangles:  %i\n", totalTris );
	Com_Printf( "\n" );
	Com_Printf( "Total Anims Size: %i\n", totalSize );
	Com_Printf( "\n" );
	Com_Printf( "Total Number of Frames: %i\n", totalFrames );
}

/*
===============
TIKI_Info_f

tiki <name>: the name goes under models/ and gets .tik unless it has them
(0x421430).
===============
*/
void TIKI_Info_f( void ) {
	char		name[MAX_QPATH];
	const char	*arg;
	tiki_t		*tiki;

	if ( Cmd_Argc() != 2 ) {
		Com_Printf( "usage: tiki tikiname\n" );
		return;
	}
	arg = Cmd_Argv( 1 );
	if ( Q_stricmpn( arg, "models/", 7 ) ) {
		Com_sprintf( name, sizeof( name ), "models/%s", arg );
	} else {
		Q_strncpyz( name, arg, sizeof( name ) );
	}
	COM_DefaultExtension( name, sizeof( name ), ".tik" );

	tiki = TIKI_Register( name );
	if ( !tiki ) {
		Com_Printf( "TIKI_GetTiki: handle -1 out of range\n" );
		Com_Printf( "ModelInfo: model not found.\n" );
		return;
	}
	TIKI_PrintInfo( tiki );
}

static int TIKI_Size( const tiki_t *tiki ) {
	int		size = sizeof( *tiki ) + tiki->animCapacity * sizeof( tikiAnim_t );
	int		i;

	size += tiki->numTags * MAX_QPATH;
	if ( tiki->boneParents ) {
		size += tiki->numTags * ( sizeof( int ) + sizeof( qboolean ) );
	}
	for ( i = 0 ; i < tiki->numAnims ; i++ ) {
		const tikiAnim_t	*anim = &tiki->anims[i];

		size += anim->numFrames * sizeof( tikiAnimFrame_t );
		size += anim->numTags * anim->numFrames * 12 * sizeof( float );
		size += anim->numFrames * anim->numBones * 8 * sizeof( short );
	}
	return size;
}

/*
===============
TIKI_AnimListedBefore

Whether an earlier animation in the list's walk loaded the same file, which
tikilist counts once.
===============
*/
static qboolean TIKI_AnimListedBefore( const tiki_t *tiki, int animnum ) {
	const tiki_t	*other;
	int				j;

	for ( other = tiki_list ; other ; other = other->next ) {
		for ( j = 0 ; j < other->numAnims ; j++ ) {
			if ( other == tiki && j == animnum ) {
				return qfalse;
			}
			if ( other->anims[j].fileSize && !Q_stricmp( other->anims[j].fullname, tiki->anims[animnum].fullname ) ) {
				return qtrue;
			}
		}
	}
	return qfalse;
}

/*
===============
TIKI_List_f

tikilist (0x451e00): every animation file loaded, once, then every
definition, with their sizes and the totals.
===============
*/
void TIKI_List_f( void ) {
	const tiki_t	*tiki;
	int				totalAnims = 0, totalTikis = 0, numAnims = 0, numTikis = 0;
	int				i;

	Com_Printf( "==============\n" );
	Com_Printf( "= Animations =\n" );
	Com_Printf( "==============\n" );
	for ( tiki = tiki_list ; tiki ; tiki = tiki->next ) {
		for ( i = 0 ; i < tiki->numAnims ; i++ ) {
			const tikiAnim_t	*anim = &tiki->anims[i];

			if ( !anim->fileSize || TIKI_AnimListedBefore( tiki, i ) ) {
				continue;
			}
			Com_Printf( "A %6d : %s\n", anim->fileSize, anim->fullname );
			totalAnims += anim->fileSize;
			numAnims++;
		}
	}
	Com_Printf( "Total Anim: %i\n\n", totalAnims );

	Com_Printf( "=========\n" );
	Com_Printf( "= Tikis =\n" );
	Com_Printf( "=========\n" );
	for ( tiki = tiki_list ; tiki ; tiki = tiki->next ) {
		int		size;

		if ( !tiki->valid ) {
			continue;
		}
		size = TIKI_Size( tiki );
		Com_Printf( "T %6d : %s\n", size, tiki->name );
		totalTikis += size;
		numTikis++;
	}
	Com_Printf( "Total Tikis: %i\n\n", totalTikis );
	Com_Printf( "Total Resident: %i\n", totalAnims + totalTikis );
	Com_Printf( "Num Anims: %i\n", numAnims );
	Com_Printf( "Num Tikis: %i\n", numTikis );
}

/*
===============
TIKI_CacheAll_f

cachealltikis (0x452490): loads every definition under models/. Retail
rewrites its binary cache of each as it goes (0x44d4d0); this module keeps
no such cache, so here it loads them, which reports any that fail.
===============
*/
void TIKI_CacheAll_f( void ) {
	char	**list;
	char	name[MAX_QPATH];
	int		count, i;

	list = FS_ListFiles( "models", ".tik", &count );
	if ( !list || !count ) {
		Com_Printf( "WARNING: no tiki files found\n" );
		FS_FreeFileList( list );
		return;
	}
	Com_Printf( "%i tiki files need to be cached\n", count );
	for ( i = 0 ; i < count ; i++ ) {
		Com_sprintf( name, sizeof( name ), "models/%s", list[i] );
		Com_Printf( "...cacheing %s %4i of %4i\n", name, i, count );
		TIKI_Register( name );
	}
	FS_FreeFileList( list );
}
