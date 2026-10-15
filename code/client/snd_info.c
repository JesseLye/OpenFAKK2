/*
===========================================================================
FAKK2's sound information table.

The retail engine reads global/sound0.txt to global/sound9.txt when the
sound system starts (fakk2.exe 0x43f890). Each "sound <name>" line opens an
entry and the lines after it set the entry's minimum distance (full volume
within it, "levelwide" is 10000), volume (0 to 1), loop block, how many may
play at once, and whether it is a local sound, which is not placed in the
world. "defaultmindist" and "defaultvolume" change the defaults for the
sounds after them (160 and 1.0 to begin with); "maxdist" and
"defaultmaxdist" are read and ignored. The files use "$define NAME value"
and "$NAME$" in later tokens. At most 1000 sounds are kept.

When the client game module starts a sound with a negative volume or
minimum distance, the entry for that name supplies the value; a name with no
entry gets the defaults in force at the end of the files. The attenuation is
the Miles sound system's: the retail engine gives each sample a minimum
distance and a maximum distance of 64 times that (0x45bf00), between which
the volume falls with the inverse of the distance and beyond which it stays
at its minimum. S_SoundGain is that curve.
===========================================================================
*/

#include "snd_local.h"

#define SOUND_INFO_FILES		10
#define SOUND_INFO_DEFINES		128
#define SOUND_INFO_HASH			256

#define SOUND_INFO_DEFAULT_MINDIST	160.0f
#define SOUND_INFO_DEFAULT_VOLUME	1.0f

typedef struct {
	char	name[64];
	char	value[MAX_QPATH];
} soundDefine_t;

static soundInfo_t	s_soundInfos[MAX_SOUND_INFOS];
static int			s_numSoundInfos;
static soundInfo_t	*s_soundInfoHash[SOUND_INFO_HASH];

// the defaults in force at the end of the files, for names with no entry
static soundInfo_t	s_soundInfoDefault;

static int S_SoundInfoHash( const char *name ) {
	int		hash = 0, i;

	for ( i = 0 ; name[i] ; i++ ) {
		char	c = tolower( name[i] );

		if ( c == '\\' ) {
			c = '/';
		}
		hash = hash * 31 + c;
	}
	return hash & ( SOUND_INFO_HASH - 1 );
}

static qboolean S_SoundInfoNameMatches( const char *a, const char *b ) {
	for ( ; *a && *b ; a++, b++ ) {
		char	ca = tolower( *a ), cb = tolower( *b );

		if ( ca == '\\' ) {
			ca = '/';
		}
		if ( cb == '\\' ) {
			cb = '/';
		}
		if ( ca != cb ) {
			return qfalse;
		}
	}
	return *a == *b;
}

static void S_SoundInfoSetDefaults( soundInfo_t *info, float minDist, float volume ) {
	info->minDist = minDist;
	info->volume = volume;
	info->loopStart = -1;
	info->loopEnd = -1;
	info->maxNumber = 5;
	info->maxFactor = -1;
	info->localSound = qfalse;
}

/*
=================
S_SoundInfoExpand

Replaces each $NAME$ in a token by its define.
=================
*/
static void S_SoundInfoExpand( const char *in, char *out, int outSize, const soundDefine_t *defines, int numDefines ) {
	int		len = 0;

	while ( *in && len < outSize - 1 ) {
		if ( *in == '$' ) {
			const char	*end = strchr( in + 1, '$' );

			if ( end ) {
				int		nameLen = (int)( end - in - 1 ), i;

				for ( i = numDefines - 1 ; i >= 0 ; i-- ) {
					if ( (int)strlen( defines[i].name ) == nameLen && !Q_stricmpn( defines[i].name, in + 1, nameLen ) ) {
						break;
					}
				}
				if ( i >= 0 ) {
					const char	*v = defines[i].value;

					while ( *v && len < outSize - 1 ) {
						out[len++] = *v++;
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

/*
=================
S_SoundInfoParseFile
=================
*/
static void S_SoundInfoParseFile( const char *filename, char *text, float *defaultMinDist, float *defaultVolume ) {
	static soundDefine_t	defines[SOUND_INFO_DEFINES];
	int						numDefines = 0;
	const char				*p = text;
	char					key[MAX_QPATH], token[MAX_QPATH];
	soundInfo_t				*info = &s_soundInfoDefault;
	float					f;

	// the next token on the line, with its defines expanded
#define VALUE()	( S_SoundInfoExpand( COM_ParseExt( &p, qfalse ), token, sizeof( token ), defines, numDefines ), token )

	for ( ;; ) {
		S_SoundInfoExpand( COM_ParseExt( &p, qtrue ), key, sizeof( key ), defines, numDefines );
		if ( !key[0] ) {
			break;
		}

		if ( !Q_stricmp( key, "$define" ) ) {
			Q_strncpyz( token, COM_ParseExt( &p, qfalse ), sizeof( token ) );
			if ( numDefines < SOUND_INFO_DEFINES ) {
				Q_strncpyz( defines[numDefines].name, token, sizeof( defines[numDefines].name ) );
				S_SoundInfoExpand( COM_ParseExt( &p, qfalse ), defines[numDefines].value, sizeof( defines[numDefines].value ), defines, numDefines );
				numDefines++;
			} else {
				COM_ParseExt( &p, qfalse );
			}
		} else if ( !Q_stricmp( key, "sound" ) ) {
			VALUE();
			if ( !token[0] ) {
				continue;
			}
			if ( s_numSoundInfos == MAX_SOUND_INFOS ) {
				Com_Printf( "Too many sound infos specified\n" );
				break;
			}
			info = &s_soundInfos[s_numSoundInfos++];
			Com_Memset( info, 0, sizeof( *info ) );
			Q_strncpyz( info->name, token, sizeof( info->name ) );
			S_SoundInfoSetDefaults( info, *defaultMinDist, *defaultVolume );
		} else if ( !Q_stricmp( key, "mindist" ) ) {
			f = atof( VALUE() );
			if ( f < 0 ) {
				Com_Printf( "Minimum distance (%f) out of range for sound %s\n", f, info->name );
				f = *defaultMinDist;
			}
			info->minDist = f;
		} else if ( !Q_stricmp( key, "maxdist" ) || !Q_stricmp( key, "defaultmaxdist" ) ) {
			COM_ParseExt( &p, qfalse );
		} else if ( !Q_stricmp( key, "volume" ) ) {
			f = atof( VALUE() );
			if ( f < 0 || f > 1 ) {
				Com_Printf( "Volume (%f) out of range for sound %s\n", f, info->name );
				f = *defaultVolume;
			}
			info->volume = f;
		} else if ( !Q_stricmp( key, "loopstart" ) ) {
			info->loopStart = atoi( VALUE() );
		} else if ( !Q_stricmp( key, "loopend" ) ) {
			info->loopEnd = atoi( VALUE() );
		} else if ( !Q_stricmp( key, "maxnumber" ) ) {
			info->maxNumber = atoi( VALUE() );
		} else if ( !Q_stricmp( key, "maxfactor" ) ) {
			info->maxFactor = atof( VALUE() );
		} else if ( !Q_stricmp( key, "localsound" ) ) {
			info->localSound = qtrue;
		} else if ( !Q_stricmp( key, "defaultmindist" ) ) {
			f = atof( VALUE() );
			if ( f < 0 ) {
				Com_Printf( "Default minimum distance (%f) out of range\n", f );
				f = SOUND_INFO_DEFAULT_MINDIST;
			}
			*defaultMinDist = f;
		} else if ( !Q_stricmp( key, "defaultvolume" ) ) {
			f = atof( VALUE() );
			if ( f < 0 || f > 1 ) {
				Com_Printf( "Default volume (%f) out of range\n", f );
				f = SOUND_INFO_DEFAULT_VOLUME;
			}
			*defaultVolume = f;
		}
		// anything else is ignored, as the retail parser ignores it
	}
#undef VALUE
}

/*
=================
S_LoadSoundInfo

Reads the sound information files. Called when the sound system starts;
may be called again.
=================
*/
void S_LoadSoundInfo( void ) {
	float	defaultMinDist = SOUND_INFO_DEFAULT_MINDIST;
	float	defaultVolume = SOUND_INFO_DEFAULT_VOLUME;
	int		i;

	s_numSoundInfos = 0;
	Com_Memset( s_soundInfoHash, 0, sizeof( s_soundInfoHash ) );
	Com_Memset( &s_soundInfoDefault, 0, sizeof( s_soundInfoDefault ) );
	S_SoundInfoSetDefaults( &s_soundInfoDefault, defaultMinDist, defaultVolume );

	for ( i = 0 ; i < SOUND_INFO_FILES ; i++ ) {
		char	filename[MAX_QPATH];
		char	*text;

		Com_sprintf( filename, sizeof( filename ), "global/sound%d.txt", i );
		if ( FS_ReadFile( filename, (void **)&text ) <= 0 || !text ) {
			continue;
		}
		Com_DPrintf( "Loading %s\n", filename );
		S_SoundInfoParseFile( filename, text, &defaultMinDist, &defaultVolume );
		FS_FreeFile( text );
	}

	// the defaults left at the end apply to every sound without an entry
	s_soundInfoDefault.minDist = defaultMinDist;
	s_soundInfoDefault.volume = defaultVolume;

	for ( i = 0 ; i < s_numSoundInfos ; i++ ) {
		int		hash = S_SoundInfoHash( s_soundInfos[i].name );

		s_soundInfos[i].hashNext = s_soundInfoHash[hash];
		s_soundInfoHash[hash] = &s_soundInfos[i];
	}
	Com_DPrintf( "%i sound infos\n", s_numSoundInfos );
}

/*
=================
S_SoundInfoForName

Never NULL: a name without an entry gets the defaults.
=================
*/
const soundInfo_t *S_SoundInfoForName( const char *name ) {
	const soundInfo_t	*info;

	if ( !name ) {
		return &s_soundInfoDefault;
	}
	for ( info = s_soundInfoHash[S_SoundInfoHash( name )] ; info ; info = info->hashNext ) {
		if ( S_SoundInfoNameMatches( info->name, name ) ) {
			return info;
		}
	}
	return &s_soundInfoDefault;
}

/*
=================
S_SoundInfo_f

s_soundinfo [name]: the table's size, or the entry a name would get.
=================
*/
void S_SoundInfo_f( void ) {
	const soundInfo_t	*info;

	if ( Cmd_Argc() < 2 ) {
		Com_Printf( "%i sound infos; defaults: mindist %g, volume %g\n", s_numSoundInfos,
			s_soundInfoDefault.minDist, s_soundInfoDefault.volume );
		return;
	}
	info = S_SoundInfoForName( Cmd_Argv( 1 ) );
	Com_Printf( "%s: %s mindist %g volume %g loop %i..%i maxnumber %i maxfactor %g%s\n",
		Cmd_Argv( 1 ), info->name[0] ? info->name : "(defaults)", info->minDist, info->volume,
		info->loopStart, info->loopEnd, info->maxNumber, info->maxFactor,
		info->localSound ? " localsound" : "" );
}

/*
=================
S_SoundGain

The share of a sound's volume heard at a distance: all of it within the
minimum distance, falling with the inverse of the distance beyond it, and
no less than at SOUND_MAX_DIST_FACTOR times the minimum distance.
=================
*/
float S_SoundGain( float minDist, float dist ) {
	if ( minDist <= 0 ) {
		return dist <= 0 ? 1.0f : SOUND_MIN_GAIN;
	}
	if ( dist <= minDist ) {
		return 1.0f;
	}
	if ( dist >= minDist * SOUND_MAX_DIST_FACTOR ) {
		return SOUND_MIN_GAIN;
	}
	return minDist / dist;
}

/*
=================
S_SoundInterp

Linear interpolation in a table of n points with rising x.
=================
*/
float S_SoundInterp( const float *x, const float *y, int n, float v ) {
	int		i;

	if ( v <= x[0] ) {
		return y[0];
	}
	for ( i = 1 ; i < n ; i++ ) {
		if ( v <= x[i] ) {
			return y[i - 1] + ( y[i] - y[i - 1] ) * ( v - x[i - 1] ) / ( x[i] - x[i - 1] );
		}
	}
	return y[n - 1];
}

/*
=================
S_SoundPan

How the retail engine's sounds are spread over the two ears. With no EAX card
Miles opens its "Fast 2D Positional Audio" provider, and recordings of it
(a tone 48 units from the listener, tools/retail-compare/probe-sound-pan*.scr)
show a balance control rather than a constant-power pan: the ear towards the
sound keeps all of it, the other loses more the further the sound is to the
side, 17 dB at most, and a sound behind the listener is 3 dB down in both.
Front and back mirror each other. The share is a function of the bearing
off the line ahead, level with the listener (the tone a little above or below
the ear panned as far as one level with it): right and forward are the
direction's parts in listener space. The gains are 0 to 1.
=================
*/
void S_SoundPan( float right, float forward, float *leftGain, float *rightGain ) {
	static const float	sideAngle[] = { 0, 15, 30, 45, 60, 75, 80, 85, 90 };
	static const float	farLoss[] = { 0, -1.5f, -3.5f, -5.8f, -8.9f, -13.3f, -15.3f, -16.1f, -17.1f };	// dB
	float	angle, near, far;

	angle = ( right || forward ) ? RAD2DEG( atan2( fabs( right ), fabs( forward ) ) ) : 0;
	near = forward < 0 ? 0.70710678f : 1.0f;
	far = near * powf( 10, S_SoundInterp( sideAngle, farLoss, ARRAY_LEN( sideAngle ), angle ) / 20 );
	*leftGain = right > 0 ? far : near;
	*rightGain = right > 0 ? near : far;
}

/*
=================
S_FollowOrigin

FAKK2: where a sound playing on an entity is when the client game module has
updated the entity, as the retail engine's sound update places it (fakk2.exe
0x45cdd1): at the listener for an entity sent with use_listener (the weapons
and other things attached to the player, sent with an origin of 0 0 0), and
otherwise at the entity's origin, except that the player's own origin, 90
units straight below the listener, counts as the listener.
=================
*/
void S_FollowOrigin( int entityNum, const vec3_t origin, qboolean useListener,
		int listenerNumber, const vec3_t listenerOrigin, vec3_t out ) {
	if ( useListener || ( entityNum == listenerNumber && origin[0] == listenerOrigin[0] &&
			origin[1] == listenerOrigin[1] && fabs( listenerOrigin[2] - origin[2] - 90 ) < 0.1f ) ) {
		VectorCopy( listenerOrigin, out );
	} else {
		VectorCopy( origin, out );
	}
}
