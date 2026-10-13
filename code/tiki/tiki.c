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

#define	TIKI_MAX_SURFACES		32
#define	TIKI_MAX_SKINS			4
#define	TIKI_MAX_NAME			128
#define	TIKI_MAX_LINE_TOKENS	64
#define	TIKI_MAX_MACROS			128
#define	TIKI_MAX_INCLUDE_DEPTH	8
#define	TIKI_MAX_LINE			2048

// .tan vertex animation
#define	TAN_IDENT				((' '<<24)+('N'<<16)+('A'<<8)+'T')	// "TAN " on disk
#define	TAN_VERSION				2
#define	TAN_MAX_TAGS			16
#define	TAN_HEADER_SIZE			176
#define	TAN_FRAME_SIZE			68
#define	TAN_TAG_NAME_SIZE		64
#define	TAN_TAG_DATA_SIZE		48
#define	TAN_SURFACE_HEADER_SIZE	104

// .skb skeleton and .ska skeletal animation
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

#define	SKEL_QUAT_SCALE			( 1.0f / 32767.0f )
#define	SKEL_OFFSET_SCALE		( 1.0f / 63.0f )

// a "last" frame selector, resolved to a frame number once the animation is loaded
#define	TIKI_FRAME_CMD_LAST		-4

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
	char				fullname[TIKI_MAX_NAME];
	int					flags;
	int					crossblend;

	int					numFrames;
	float				totalTime;
	vec3_t				totalDelta;
	tikiAnimFrame_t		*frames;

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
} tikiSurface_t;

struct tiki_s {
	struct tiki_s		*next;
	char				name[TIKI_MAX_NAME];
	qboolean			valid;

	float				loadScale;
	float				lodScale;
	float				lodBias;
	vec3_t				loadOrigin;
	vec3_t				mins, maxs;
	float				radius;
	qboolean			radiusSet;

	char				path[MAX_QPATH];
	char				skelName[TIKI_MAX_NAME];

	int					numSurfaces;
	tikiSurface_t		surfaces[TIKI_MAX_SURFACES];

	// tag names for vertex-animated models, bone names for skeletal ones
	int					numTags;
	char				( *tagNames )[MAX_QPATH];
	int					*boneParents;		// NULL unless skeletal

	int					numAnims;
	int					animCapacity;
	tikiAnim_t			*anims;

	tikiCommandList_t	serverInit;
	tikiCommandList_t	clientInit;
};

typedef struct {
	char	name[MAX_QPATH];
	char	value[MAX_QPATH * 2];
} tikiMacro_t;

typedef struct {
	tiki_t				*tiki;
	int					numMacros;
	tikiMacro_t			macros[TIKI_MAX_MACROS];
	tikiCommandList_t	surfaceCmds;	// applied once the surface names are known
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

static void TIKI_AppendArgs( tikiCommand_t *cmd, char **tokens, int numTokens ) {
	char	**grown = TIKI_Alloc( ( cmd->numArgs + numTokens ) * sizeof( char * ) );
	int		i;

	memcpy( grown, cmd->args, cmd->numArgs * sizeof( char * ) );
	for ( i = 0 ; i < numTokens ; i++ ) {
		grown[cmd->numArgs + i] = TIKI_CopyString( tokens[i] );
	}
	free( cmd->args );
	cmd->args = grown;
	cmd->numArgs += numTokens;
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
	TIKI_FreeCommands( &tiki->serverInit );
	TIKI_FreeCommands( &tiki->clientInit );
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
		for ( i = 0 ; i < numBones ; i++ ) {
			const byte	*b = data + ofsBones + i * SKB_BONE_SIZE;
			int			parent = TIKI_ReadInt( b );

			// a bone may only hang off an earlier bone, which keeps evaluation a single pass
			tiki->boneParents[i] = ( parent >= 0 && parent < i ) ? parent : -1;
			Q_strncpyz( tiki->tagNames[i], (const char *)b + 8, MAX_QPATH );
		}
		tiki->numTags = numBones;

		for ( i = 0 ; i < numSurfaces ; i++ ) {
			int		ofsEnd;

			if ( !TIKI_InFile( length, ofs, SKB_SURFACE_HEADER_SIZE ) ) {
				break;
			}
			Q_strncpyz( tiki->surfaces[i].name, (const char *)data + ofs + 4, MAX_QPATH );
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

// returns qfalse when the selector is not one; the line then continues the previous command
static qboolean TIKI_ParseFrameSelector( const char *token, int *frame ) {
	const char	*p;

	if ( !Q_stricmp( token, "first" ) ) {
		*frame = 0;
	} else if ( !Q_stricmp( token, "last" ) ) {
		*frame = TIKI_FRAME_CMD_LAST;
	} else if ( !Q_stricmp( token, "every" ) ) {
		*frame = TIKI_FRAME_CMD_EVERY_FRAME;
	} else if ( !Q_stricmp( token, "exit" ) ) {
		*frame = TIKI_FRAME_CMD_EXIT;
	} else if ( !Q_stricmp( token, "entry" ) || !Q_stricmp( token, "enter" ) ) {
		*frame = TIKI_FRAME_CMD_ENTRY;
	} else {
		for ( p = token ; *p ; p++ ) {
			if ( !isdigit( (unsigned char)*p ) ) {
				return qfalse;
			}
		}
		if ( p == token ) {
			return qfalse;
		}
		*frame = atoi( token );
	}
	return qtrue;
}

static void TIKI_ParseSetup( tikiParse_t *parse, char **tokens, int numTokens ) {
	tiki_t		*tiki = parse->tiki;
	const char	*key = tokens[0];

	if ( !Q_stricmp( key, "surface" ) ) {
		TIKI_AddCommand( &parse->surfaceCmds, 0, tokens + 1, numTokens - 1 );
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
	} else {
		Com_DPrintf( "TIKI: unknown setup keyword '%s' in %s\n", key, tiki->name );
	}
}

static qboolean TIKI_ParseFile( tikiParse_t *parse, const char *filename, int depth );

/*
===============
TIKI_ParseText

Sections, braces and commands are all line oriented: a brace stands alone on
its line, and a command is the tokens of one line. Each file tracks its own
section state, so an included file is a complete definition in its own right
that contributes to the same model.
===============
*/
static void TIKI_ParseText( tikiParse_t *parse, char *text, int depth ) {
	enum { SECTION_NONE, SECTION_SETUP, SECTION_INIT, SECTION_ANIMATIONS, SECTION_OTHER } section = SECTION_NONE;
	enum { SUB_NONE, SUB_SERVER, SUB_CLIENT } sub = SUB_NONE;
	tiki_t		*tiki = parse->tiki;
	int			level = 0;
	int			animIndex = -1;
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
			level++;
			continue;
		}
		if ( !strcmp( head, "}" ) ) {
			if ( level > 0 ) {
				level--;
			}
			if ( level == 0 ) {
				section = SECTION_NONE;
			}
			if ( level == 1 ) {
				sub = SUB_NONE;
			}
			continue;
		}

		if ( level == 0 ) {
			if ( !Q_stricmp( head, "setup" ) ) {
				section = SECTION_SETUP;
			} else if ( !Q_stricmp( head, "init" ) ) {
				section = SECTION_INIT;
			} else if ( !Q_stricmp( head, "animations" ) ) {
				section = SECTION_ANIMATIONS;
			} else {
				section = SECTION_OTHER;	// includes the leading TIKI identifier
			}
			sub = SUB_NONE;
			continue;
		}

		switch ( section ) {
		case SECTION_SETUP:
			TIKI_ParseSetup( parse, tokens, numTokens );
			break;

		case SECTION_INIT:
			if ( level == 1 ) {
				sub = !Q_stricmp( head, "server" ) ? SUB_SERVER : SUB_CLIENT;
			} else if ( level == 2 ) {
				TIKI_AddCommand( sub == SUB_SERVER ? &tiki->serverInit : &tiki->clientInit, 0, tokens, numTokens );
			}
			break;

		case SECTION_ANIMATIONS:
			if ( level == 1 ) {
				tikiAnim_t	*anim;
				int			i;

				if ( numTokens < 2 ) {
					break;
				}
				anim = TIKI_NewAnim( tiki );
				animIndex = tiki->numAnims - 1;
				Q_strncpyz( anim->alias, tokens[0], sizeof( anim->alias ) );
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
			} else if ( level == 2 ) {
				sub = !Q_stricmp( head, "server" ) ? SUB_SERVER : SUB_CLIENT;
			} else if ( level == 3 && animIndex >= 0 ) {
				tikiAnim_t			*anim = &tiki->anims[animIndex];
				tikiCommandList_t	*list = sub == SUB_SERVER ? &anim->serverCmds : &anim->clientCmds;
				int					frame;

				if ( TIKI_ParseFrameSelector( head, &frame ) ) {
					TIKI_AddCommand( list, frame, tokens + 1, numTokens - 1 );
				} else if ( list->count ) {
					// client effect commands spread their parameters over following lines
					TIKI_AppendArgs( &list->cmds[list->count - 1], tokens, numTokens );
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
				surface->numSkins = 0;
				for ( k = 2 ; k < cmd->numArgs && surface->numSkins < TIKI_MAX_SKINS ; k++ ) {
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
		char	name[TIKI_MAX_NAME];

		if ( tiki->path[0] ) {
			Com_sprintf( name, sizeof( name ), "%s/%s", tiki->path, tiki->skelName );
		} else {
			Q_strncpyz( name, tiki->skelName, sizeof( name ) );
		}
		TIKI_LoadSkb( tiki, name );
	}

	ClearBounds( tiki->mins, tiki->maxs );

	for ( i = 0 ; i < tiki->numAnims ; i++ ) {
		tikiAnim_t	*anim = &tiki->anims[i];
		void		*buffer;
		int			length;
		qboolean	ok;

		length = (int)FS_ReadFile( anim->fullname, &buffer );
		if ( !buffer ) {
			Com_Printf( "WARNING: %s: couldn't load animation %s\n", tiki->name, anim->fullname );
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

		if ( anim->totalDelta[0] || anim->totalDelta[1] || anim->totalDelta[2] ) {
			anim->flags |= TIKI_ANIM_DELTA_DRIVEN;
		}

		for ( j = 0 ; j < anim->numFrames ; j++ ) {
			AddPointToBounds( anim->frames[j].mins, tiki->mins, tiki->maxs );
			AddPointToBounds( anim->frames[j].maxs, tiki->mins, tiki->maxs );
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
	tiki->next = tiki_list;
	tiki_list = tiki;

	parse = TIKI_Alloc( sizeof( *parse ) );
	parse->tiki = tiki;

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

int TIKI_NumAnims( const tiki_t *tiki ) {
	return tiki ? tiki->numAnims : 0;
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

Variants of an animation are written as the name followed by a number
(idle1, idle2, ...). Picks among the exact name and its numbered variants.
===============
*/
int TIKI_Anim_Random( const tiki_t *tiki, const char *name ) {
	int		matches[64];
	int		numMatches = 0;
	int		len, i;

	if ( !tiki || !name || !name[0] ) {
		return -1;
	}
	len = strlen( name );
	for ( i = 0 ; i < tiki->numAnims && numMatches < (int)ARRAY_LEN( matches ) ; i++ ) {
		const char	*alias = tiki->anims[i].alias;
		const char	*rest;

		if ( Q_stricmpn( alias, name, len ) ) {
			continue;
		}
		for ( rest = alias + len ; *rest && isdigit( (unsigned char)*rest ) ; rest++ ) {
		}
		if ( !*rest ) {
			matches[numMatches++] = i;
		}
	}
	if ( !numMatches ) {
		return -1;
	}
	return matches[rand() % numMatches];
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

/*
===============
TIKI_BoneOrientation

Walks from the bone up through its parents. How the retail engine composes
bone transforms has not been confirmed, and bone controllers and animation
blending are not applied, so skeletal tag placement is provisional.
===============
*/
static void TIKI_BoneOrientation( const tiki_t *tiki, const tikiAnim_t *anim, int framenum, int bone, orientation_t *out ) {
	int		chain[SKEL_MAX_BONES];
	int		depth = 0;
	int		i;

	VectorClear( out->origin );
	AxisClear( out->axis );

	while ( bone >= 0 && bone < anim->numBones && depth < SKEL_MAX_BONES ) {
		chain[depth++] = bone;
		bone = tiki->boneParents ? tiki->boneParents[bone] : -1;
	}

	for ( i = depth - 1 ; i >= 0 ; i-- ) {
		const short	*data = &anim->boneData[( framenum * anim->numBones + chain[i] ) * 8];
		float		quat[4];
		vec3_t		offset, local[3], combined[3];
		int			j;

		for ( j = 0 ; j < 4 ; j++ ) {
			quat[j] = data[j] * SKEL_QUAT_SCALE;
		}
		for ( j = 0 ; j < 3 ; j++ ) {
			offset[j] = data[4 + j] * SKEL_OFFSET_SCALE;
		}

		for ( j = 0 ; j < 3 ; j++ ) {
			VectorMA( out->origin, offset[j], out->axis[j], out->origin );
		}
		TIKI_QuatToAxis( quat, local );
		MatrixMultiply( local, out->axis, combined );
		AxisCopy( combined, out->axis );
	}
}

orientation_t TIKI_Tag_Orientation( const tiki_t *tiki, int animnum, int framenum, int tagnum, float scale ) {
	const tikiAnim_t	*anim = TIKI_GetAnim( tiki, animnum );
	orientation_t		orient;

	VectorClear( orient.origin );
	AxisClear( orient.axis );

	if ( !anim || framenum < 0 || framenum >= anim->numFrames || tagnum < 0 ) {
		return orient;
	}

	if ( anim->boneData ) {
		if ( tagnum >= anim->numBones ) {
			return orient;
		}
		TIKI_BoneOrientation( tiki, anim, framenum, tagnum, &orient );
	} else if ( anim->tagData ) {
		const float	*data;

		if ( tagnum >= anim->numTags ) {
			return orient;
		}
		data = &anim->tagData[( tagnum * anim->numFrames + framenum ) * 12];
		VectorCopy( data, orient.origin );
		VectorCopy( data + 3, orient.axis[0] );
		VectorCopy( data + 6, orient.axis[1] );
		VectorCopy( data + 9, orient.axis[2] );
	} else {
		return orient;
	}

	VectorAdd( orient.origin, tiki->loadOrigin, orient.origin );
	VectorScale( orient.origin, tiki->loadScale * scale, orient.origin );
	return orient;
}
