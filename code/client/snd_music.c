/*
===========================================================================
FAKK2's music: a soundtrack of songs chosen by mood.

The game names a soundtrack in a configstring and sends a current mood, a
fallback mood, a volume and a fade time in the player state; the client
game module hands them to MUSIC_NewSoundtrack, MUSIC_UpdateMood and
MUSIC_UpdateVolume. Read from fakk2.exe:

- MUSIC_LoadSoundtrackFile (0x440be0) reads "<name>.mus": "path <dir>"
  prefixes the files that follow; a line "<mood> <file>" or "<file>" (the
  mood is then the file name without its extension) adds a song, 16 at
  most; "!<mood> loop|restart|interrupt|volume <v>|fadetime <t>" sets a
  song's flags, interrupt implying restart and a fade time of 0. Volume
  defaults to 1 and fade time to 1. When no mood has been requested yet,
  loading requests normal/normal.
- MUSIC_UpdateMood (0x441570) records the pair. Coming back to the pair
  that preceded an action mood skips the current mood's song for the
  fallback's when that song is marked interrupt.
- MUSIC_UpdateVolume (0x441690) starts a fade of the game's music volume
  from its current value to the new one over the fade time.
- MUSIC_Update (0x4412a0) runs each frame: a one-shot song is over once it
  is within the fallback song's fade time of its end (0x45e7e0, "Song
  ended"), and the fallback becomes the requested mood; a requested mood
  whose song is not the one playing starts that song (0x45e380) on one of
  two handles: the song playing fades out over the new song's fade time,
  unless the new song is marked interrupt, which stops it at once
  (0x45e2e0); a looping song starts at full volume, a one-shot fades in
  over its own fade time; a song without restart resumes where it was
  stopped. Each handle's volume (0x45e5e0) is the song's volume times
  s_musicvolume times the game's music volume times its fade, on top of
  s_volume. The retail default for s_musicvolume is 0.60.

Here the two handles are two raw streams fed from the codec streams a
little ahead of real time, on either sound backend. Not followed: the
retail engine truncates fade times to whole seconds, and its fade-down of
the game's volume starts from 1 rather than the current value.
===========================================================================
*/

#include "client.h"
#include "snd_local.h"
#include "snd_codec.h"

#define MUSIC_MAX_SONGS		16
#define MUSIC_HANDLES		2
#define MUSIC_FIRST_STREAM	( MAX_RAW_STREAMS - MUSIC_HANDLES )	// raw streams the handles use
#define MUSIC_LEAD_MS		250		// how far ahead of real time a handle is fed
#define MUSIC_RESYNC_MS		500		// further behind than this, a handle skips ahead
#define MUSIC_FEED_BYTES	16384

#define SONG_LOOP			1
#define SONG_RESTART		2
#define SONG_INTERRUPT		4

// the game's numbering (q_shared.h, music_mood_t)
typedef enum {
	mood_none,
	mood_normal,
	mood_action,
	mood_suspense,
	mood_mystery,
	mood_success,
	mood_failure,
	mood_surprise,
	mood_special,
	mood_aux1,
	mood_aux2,
	mood_aux3,
	mood_aux4,
	mood_aux5,
	mood_aux6,
	mood_aux7,
	mood_totalnumber
} musicMood_t;

static const char *musicMoodNames[mood_totalnumber] = {
	"none", "normal", "action", "suspense", "mystery", "success", "failure", "surprise",
	"special", "aux1", "aux2", "aux3", "aux4", "aux5", "aux6", "aux7"
};

typedef struct {
	char		name[32];			// the mood it plays for
	char		path[MAX_QPATH];
	int			mood;				// musicMood_t, or -1
	int			flags;				// SONG_*
	float		volume;
	float		fadeTime;			// seconds
	int			resumeFrame;		// where it stopped last, for a song without restart
} musicSong_t;

typedef enum {
	HANDLE_STEADY,
	HANDLE_FADE_IN,
	HANDLE_FADE_OUT
} musicHandleState_t;

typedef struct {
	snd_stream_t		*stream;
	int					song;
	musicHandleState_t	state;
	int					fadeStart;		// cls.realtime
	float				fadeTime;
	qboolean			watchEnd;		// still counted for "song ended"
	qboolean			looping;
	qboolean			ended;			// the file ran out
	int					startTime;		// cls.realtime when feeding began
	int					playedFrames;	// fed since startTime
	int					posFrame;		// position in the song
} musicHandle_t;

static char				music_soundtrack[MAX_QPATH];
static qboolean			music_enabled;
static musicSong_t		music_songs[MUSIC_MAX_SONGS];
static int				music_numSongs;
static int				music_currentSong = -1;

static int				music_requestedMood, music_fallbackMood;
static int				music_currentMood, music_currentFallback;
static int				music_prevMood, music_prevFallback;

// the game's music volume and its fade
static float			music_volume = 1, music_targetVolume = 1, music_startVolume = 1;
static float			music_fadeTime;
static int				music_fadeStart;
static qboolean			music_fadeDown;

static musicHandle_t	music_handles[MUSIC_HANDLES];
static qboolean			music_initialised;		// the sound system started (and so the codecs)

static const char *MUSIC_MoodName( int mood ) {
	if ( mood < 0 || mood >= mood_totalnumber ) {
		return "";
	}
	return musicMoodNames[mood];
}

static int MUSIC_MoodNum( const char *name ) {
	int		i;

	for ( i = 0 ; i < mood_totalnumber ; i++ ) {
		if ( !Q_stricmp( name, musicMoodNames[i] ) ) {
			return i;
		}
	}
	return -1;
}

static int MUSIC_FindSong( const char *name ) {
	int		i;

	for ( i = 0 ; i < music_numSongs ; i++ ) {
		if ( !Q_stricmp( music_songs[i].name, name ) ) {
			return i;
		}
	}
	return -1;
}

/*
=================
MUSIC_StopHandle

Remembers where the song was, unless it restarts.
=================
*/
static void MUSIC_StopHandle( int h ) {
	musicHandle_t	*handle = &music_handles[h];
	musicSong_t		*song;

	if ( !handle->stream ) {
		return;
	}
	song = &music_songs[handle->song];
	song->resumeFrame = ( song->flags & SONG_RESTART ) ? 0 : handle->posFrame;
	S_CodecCloseStream( handle->stream );
	handle->stream = NULL;
	handle->watchEnd = qfalse;
	Com_DPrintf( "MUSIC: stopped %s on handle %i\n", song->name, h );
}

// the handle playing the current song, or -1
static int MUSIC_PlayingHandle( void ) {
	int		h;

	for ( h = 0 ; h < MUSIC_HANDLES ; h++ ) {
		if ( music_handles[h].stream && music_handles[h].song == music_currentSong ) {
			return h;
		}
	}
	return -1;
}

// retail 0x441720
void MUSIC_StopSongs( void ) {
	int		h;

	for ( h = 0 ; h < MUSIC_HANDLES ; h++ ) {
		MUSIC_StopHandle( h );
	}
	music_currentSong = -1;
}

/*
=================
MUSIC_LoadSoundtrackFile

Retail 0x440be0.
=================
*/
static qboolean MUSIC_LoadSoundtrackFile( const char *name ) {
	char		filename[MAX_QPATH], prefix[MAX_QPATH] = "";
	char		key[MAX_QPATH], arg1[MAX_QPATH], arg2[MAX_QPATH];
	char		*text;
	const char	*p, *token;
	int			numArgs, idx;

	if ( strrchr( name, '.' ) ) {
		Q_strncpyz( filename, name, sizeof( filename ) );
	} else {
		Com_sprintf( filename, sizeof( filename ), "%s.mus", name );
	}
	if ( FS_ReadFile( filename, (void **)&text ) <= 0 || !text ) {
		Com_Printf( "Couldn't load %s\n", filename );
		return qfalse;
	}
	Com_DPrintf( "SOUNDTRACK: Loading %s\n", filename );

	MUSIC_StopSongs();
	music_numSongs = 0;
	Com_Memset( music_songs, 0, sizeof( music_songs ) );

	p = text;
	for ( ;; ) {
		token = COM_ParseExt( &p, qtrue );
		if ( !token[0] ) {
			break;
		}
		Q_strncpyz( key, token, sizeof( key ) );

		// the rest of the line
		numArgs = 0;
		arg1[0] = arg2[0] = 0;
		while ( ( token = COM_ParseExt( &p, qfalse ) )[0] ) {
			if ( numArgs == 0 ) {
				Q_strncpyz( arg1, token, sizeof( arg1 ) );
			} else if ( numArgs == 1 ) {
				Q_strncpyz( arg2, token, sizeof( arg2 ) );
			}
			numArgs++;
		}

		if ( !Q_stricmp( key, "path" ) ) {
			size_t	len;

			Q_strncpyz( prefix, arg1, sizeof( prefix ) - 1 );
			len = strlen( prefix );
			if ( len && prefix[len - 1] != '/' && prefix[len - 1] != '\\' ) {
				Q_strcat( prefix, sizeof( prefix ), "/" );
			}
			continue;
		}

		if ( key[0] == '!' ) {
			musicSong_t	*song;

			idx = MUSIC_FindSong( key + 1 );
			if ( idx < 0 ) {
				Com_DPrintf( "MUSIC_LoadSoundtrackFile: song %s not found, command skipped in %s.\n", key + 1, filename );
				continue;
			}
			song = &music_songs[idx];
			if ( !Q_stricmp( arg1, "volume" ) ) {
				song->volume = atof( arg2 );
			} else if ( !Q_stricmp( arg1, "fadetime" ) ) {
				song->fadeTime = atof( arg2 );
			} else if ( !Q_stricmp( arg1, "loop" ) ) {
				song->flags |= SONG_LOOP;
			} else if ( !Q_stricmp( arg1, "restart" ) ) {
				song->flags |= SONG_RESTART;
			} else if ( !Q_stricmp( arg1, "interrupt" ) ) {
				song->fadeTime = 0;
				song->flags |= SONG_INTERRUPT | SONG_RESTART;
			} else {
				Com_DPrintf( "MUSIC_LoadSoundtrackFile: unknown command %s for song %s in %s.\n", arg1, key + 1, filename );
			}
			continue;
		}

		// a song: "<mood> <file>", or a file alone named after its mood
		if ( music_numSongs == MUSIC_MAX_SONGS ) {
			Com_DPrintf( "MUSIC_LoadSoundtrackFile: Too many songs in %s, skipping %s.\n", filename, key );
			continue;
		}
		{
			musicSong_t	*song = &music_songs[music_numSongs++];

			if ( numArgs == 0 ) {
				COM_StripExtension( key, song->name, sizeof( song->name ) );
				Com_sprintf( song->path, sizeof( song->path ), "%s%s", prefix, key );
			} else {
				Q_strncpyz( song->name, key, sizeof( song->name ) );
				Com_sprintf( song->path, sizeof( song->path ), "%s%s", prefix, arg1 );
			}
			song->mood = MUSIC_MoodNum( song->name );
			song->volume = 1.0f;
			song->fadeTime = 1.0f;
		}
	}
	FS_FreeFile( text );

	if ( !music_numSongs ) {
		Com_DPrintf( "MUSIC_LoadSoundtrackFile: could not load %s, no songs defined.\n", filename );
		return qfalse;
	}

	music_currentSong = -1;
	if ( music_requestedMood == mood_none ) {
		MUSIC_UpdateMood( mood_normal, mood_normal );
	}
	return qtrue;
}

/*
=================
MUSIC_NewSoundtrack

Retail 0x4414c0. An empty name turns the music off.
=================
*/
void MUSIC_NewSoundtrack( const char *name ) {
	if ( !name ) {
		name = "";
	}
	if ( !Q_stricmp( name, music_soundtrack ) ) {
		return;
	}
	Q_strncpyz( music_soundtrack, name, sizeof( music_soundtrack ) );

	if ( music_soundtrack[0] ) {
		music_enabled = qtrue;
		MUSIC_LoadSoundtrackFile( music_soundtrack );
	} else {
		music_enabled = qfalse;
		if ( MUSIC_PlayingHandle() != -1 ) {
			MUSIC_StopSongs();
		}
	}
}

/*
=================
MUSIC_UpdateMood

Retail 0x441570.
=================
*/
void MUSIC_UpdateMood( int current, int fallback ) {
	qboolean	restoring;

	if ( current == music_currentMood && fallback == music_currentFallback ) {
		return;
	}
	restoring = current == music_prevMood && fallback == music_prevFallback && music_currentMood == mood_action;

	music_prevMood = music_currentMood;
	music_prevFallback = music_currentFallback;
	music_currentMood = current;
	music_currentFallback = fallback;
	music_requestedMood = current;
	music_fallbackMood = fallback;

	Com_DPrintf( "MUSIC: changing from [ %s | %s ] to [ %s | %s ]\n", MUSIC_MoodName( music_prevMood ),
		MUSIC_MoodName( music_prevFallback ), MUSIC_MoodName( current ), MUSIC_MoodName( fallback ) );

	if ( restoring ) {
		int		idx = MUSIC_FindSong( MUSIC_MoodName( current ) );

		if ( idx >= 0 && ( music_songs[idx].flags & SONG_INTERRUPT ) ) {
			Com_DPrintf( "MUSIC: restoring music from action state, skipping [ %s ] for [ %s ]\n",
				MUSIC_MoodName( current ), MUSIC_MoodName( fallback ) );
			music_requestedMood = fallback;
		}
	}
}

/*
=================
MUSIC_UpdateVolume

Retail 0x441690.
=================
*/
void MUSIC_UpdateVolume( float volume, float fadeTime ) {
	if ( volume == music_targetVolume && fadeTime == music_fadeTime ) {
		return;
	}
	music_startVolume = music_volume;
	music_targetVolume = volume;
	music_fadeStart = cls.realtime;
	music_fadeTime = fadeTime;
	if ( volume > music_volume ) {
		music_fadeDown = qfalse;
	} else if ( volume < music_volume ) {
		music_fadeDown = qtrue;
	}
}

/*
=================
MUSIC_PlaySong

Retail 0x45e380: starts the song for a mood on a free handle.
=================
*/
static void MUSIC_PlaySong( const char *moodName ) {
	int				idx, old, h;
	qboolean		useOther = qfalse;
	musicSong_t		*song;
	musicHandle_t	*handle;

	idx = MUSIC_FindSong( moodName );
	if ( idx < 0 ) {
		return;
	}
	old = MUSIC_PlayingHandle();
	if ( old != -1 && idx == music_currentSong ) {
		return;
	}
	music_currentSong = idx;
	song = &music_songs[idx];

	if ( old != -1 ) {
		if ( !( song->flags & SONG_INTERRUPT ) ) {
			// the song playing fades out over the new song's fade time
			music_handles[old].state = HANDLE_FADE_OUT;
			music_handles[old].fadeStart = cls.realtime;
			music_handles[old].fadeTime = song->fadeTime;
			useOther = qtrue;
		} else {
			MUSIC_StopHandle( old );
		}
	}
	h = useOther ? ( old == 0 ) : 0;
	handle = &music_handles[h];
	if ( handle->stream ) {
		MUSIC_StopHandle( h );
	}

	handle->stream = S_CodecOpenStream( song->path );
	if ( !handle->stream ) {
		Com_Printf( "Could not setup mp3 file: %s\n", song->path );
		return;
	}
	handle->song = idx;
	handle->watchEnd = qtrue;
	handle->ended = qfalse;
	handle->startTime = cls.realtime;
	handle->playedFrames = 0;
	handle->posFrame = 0;
	if ( song->resumeFrame > 0 && S_CodecSeekStream( handle->stream, song->resumeFrame ) ) {
		handle->posFrame = song->resumeFrame;
	}
	if ( song->flags & SONG_LOOP ) {
		handle->looping = qtrue;
		handle->state = HANDLE_STEADY;
	} else {
		handle->looping = qfalse;
		handle->state = HANDLE_FADE_IN;
		handle->fadeStart = cls.realtime;
		handle->fadeTime = song->fadeTime;
	}
	Com_DPrintf( "MUSIC: playing %s (%s) on handle %i%s%s\n", song->path, song->name, h,
		handle->looping ? ", looping" : ", fading in", handle->posFrame ? ", resumed" : "" );
}

/*
=================
MUSIC_CheckSongEnds

Retail 0x45e7e0: a one-shot is over once it is within the fallback song's
fade time of its end; the fallback becomes the requested mood.
=================
*/
static void MUSIC_CheckSongEnds( void ) {
	int		h, fb;
	float	fade;

	for ( h = 0 ; h < MUSIC_HANDLES ; h++ ) {
		musicHandle_t	*handle = &music_handles[h];

		if ( !handle->stream || !handle->watchEnd || handle->looping ) {
			continue;
		}
		fb = MUSIC_FindSong( MUSIC_MoodName( music_fallbackMood ) );
		fade = fb >= 0 ? music_songs[fb].fadeTime : 0;
		if ( !handle->ended && handle->posFrame + fade * handle->stream->info.rate < handle->stream->info.samples ) {
			continue;
		}
		handle->watchEnd = qfalse;
		Com_DPrintf( "MUSIC: Song ended, changing from [ %s ] to [ %s ]\n",
			MUSIC_MoodName( music_requestedMood ), MUSIC_MoodName( music_fallbackMood ) );
		music_requestedMood = music_fallbackMood;
	}
}

/*
=================
MUSIC_FeedHandle

Keeps the handle's raw stream MUSIC_LEAD_MS ahead of real time.
=================
*/
static void MUSIC_FeedHandle( int h, float gain ) {
	musicHandle_t	*handle = &music_handles[h];
	snd_info_t		*info = &handle->stream->info;
	byte			buf[MUSIC_FEED_BYTES];
	int				frameBytes = info->width * info->channels;
	int				target, want, r, frames;

	if ( handle->ended || frameBytes <= 0 ) {
		return;
	}
	target = (int)( ( cls.realtime - handle->startTime ) * (double)info->rate / 1000.0 ) + info->rate * MUSIC_LEAD_MS / 1000;
	if ( target - handle->playedFrames > info->rate * MUSIC_RESYNC_MS / 1000 ) {
		// too far behind (a load, a stall): skip ahead rather than flood the stream
		handle->startTime = cls.realtime - (int)( handle->playedFrames * 1000.0 / info->rate );
		target = handle->playedFrames + info->rate * MUSIC_LEAD_MS / 1000;
	}

	while ( handle->playedFrames < target ) {
		want = target - handle->playedFrames;
		if ( want > (int)( sizeof( buf ) / frameBytes ) ) {
			want = sizeof( buf ) / frameBytes;
		}
		r = S_CodecReadStream( handle->stream, want * frameBytes, buf );
		if ( r <= 0 ) {
			if ( handle->looping && handle->posFrame > 0 && S_CodecSeekStream( handle->stream, 0 ) ) {
				handle->posFrame = 0;
				continue;
			}
			handle->ended = qtrue;
			break;
		}
		frames = r / frameBytes;
		S_RawSamples( MUSIC_FIRST_STREAM + h, frames, info->rate, info->width, info->channels, buf, gain, -1 );
		handle->playedFrames += frames;
		handle->posFrame += frames;
	}
}

/*
=================
MUSIC_UpdateHandles

Retail 0x45e5e0: the fades, then the feeding.
=================
*/
static void MUSIC_UpdateHandles( void ) {
	int		h;

	for ( h = 0 ; h < MUSIC_HANDLES ; h++ ) {
		musicHandle_t	*handle = &music_handles[h];
		musicSong_t		*song;
		float			full, f, gain;

		if ( !handle->stream ) {
			continue;
		}
		song = &music_songs[handle->song];
		full = song->volume * s_musicVolume->value * music_volume;

		f = 1;
		if ( handle->state != HANDLE_STEADY && handle->fadeTime > 0 ) {
			f = ( cls.realtime - handle->fadeStart ) / ( handle->fadeTime * 1000.0f );
		}
		switch ( handle->state ) {
		case HANDLE_FADE_IN:
			if ( f >= 1 ) {
				handle->state = HANDLE_STEADY;
				gain = full;
			} else {
				gain = full * f;
			}
			break;
		case HANDLE_FADE_OUT:
			if ( f >= 1 ) {
				MUSIC_StopHandle( h );
				continue;
			}
			gain = full * ( 1 - f );
			break;
		default:
			gain = full;
			break;
		}
		MUSIC_FeedHandle( h, gain );
	}
}

/*
=================
MUSIC_Update

Retail 0x4412a0, once a frame.
=================
*/
void MUSIC_Update( void ) {
	int		curMood;

	if ( !music_enabled || !music_initialised ) {
		return;
	}

	MUSIC_CheckSongEnds();

	if ( !music_numSongs && music_soundtrack[0] ) {
		MUSIC_LoadSoundtrackFile( music_soundtrack );
	}

	curMood = music_currentSong >= 0 ? music_songs[music_currentSong].mood : -1;
	if ( music_requestedMood == mood_none ) {
		if ( MUSIC_PlayingHandle() != -1 ) {
			MUSIC_StopSongs();
		}
	} else if ( music_requestedMood != curMood ) {
		const char	*name = MUSIC_MoodName( music_requestedMood );

		if ( MUSIC_FindSong( name ) >= 0 && music_numSongs && music_soundtrack[0] ) {
			Com_DPrintf( "Playing %s\n", name );
			MUSIC_PlaySong( name );
		}
	}

	// the game's volume fade
	if ( music_volume != music_targetVolume ) {
		if ( music_fadeTime <= 0 ) {
			music_volume = music_targetVolume;
		} else {
			float	f = ( cls.realtime - music_fadeStart ) / ( music_fadeTime * 1000.0f );

			music_volume = music_startVolume + ( music_targetVolume - music_startVolume ) * f;
			if ( ( !music_fadeDown && music_volume > music_targetVolume ) || ( music_fadeDown && music_volume < music_targetVolume ) ) {
				music_volume = music_targetVolume;
			}
		}
	}

	MUSIC_UpdateHandles();
}

/*
=================
MUSIC_Shutdown

Forgets the soundtrack as well as stopping the songs.
=================
*/
void MUSIC_Shutdown( void ) {
	MUSIC_StopSongs();
	music_soundtrack[0] = 0;
	music_enabled = qfalse;
	music_numSongs = 0;
	music_requestedMood = music_fallbackMood = mood_none;
	music_currentMood = music_currentFallback = music_prevMood = music_prevFallback = mood_none;
	music_volume = music_targetVolume = music_startVolume = 1;
	music_fadeTime = 0;
}

// console commands, as the retail engine has them

static void MUSIC_LoadSoundtrack_f( void ) {
	if ( Cmd_Argc() != 2 ) {
		Com_Printf( "usage: loadsoundtrack <name>\n" );
		return;
	}
	MUSIC_NewSoundtrack( Cmd_Argv( 1 ) );
}

static void MUSIC_PlaySong_f( void ) {
	int		mood;

	if ( Cmd_Argc() < 2 ) {
		Com_Printf( "usage: playsong <mood> [fallback mood]\n" );
		return;
	}
	mood = MUSIC_MoodNum( Cmd_Argv( 1 ) );
	if ( mood < 0 ) {
		Com_Printf( "No such mood: %s\n", Cmd_Argv( 1 ) );
		return;
	}
	if ( Cmd_Argc() > 2 ) {
		int		fallback = MUSIC_MoodNum( Cmd_Argv( 2 ) );

		MUSIC_UpdateMood( mood, fallback < 0 ? music_currentFallback : fallback );
	} else {
		MUSIC_UpdateMood( mood, music_currentFallback );
	}
}

static void MUSIC_Info_f( void ) {
	int		i, h;

	Com_Printf( "soundtrack \"%s\"%s, %i songs; mood %s / %s, requested %s, volume %g\n", music_soundtrack,
		music_enabled ? "" : " (off)", music_numSongs, MUSIC_MoodName( music_currentMood ),
		MUSIC_MoodName( music_currentFallback ), MUSIC_MoodName( music_requestedMood ), music_volume );
	for ( i = 0 ; i < music_numSongs ; i++ ) {
		const musicSong_t	*song = &music_songs[i];

		Com_Printf( "%2i %-10s %s%s%s%s volume %g fadetime %g\n", i, song->name, song->path,
			( song->flags & SONG_LOOP ) ? " loop" : "", ( song->flags & SONG_RESTART ) ? " restart" : "",
			( song->flags & SONG_INTERRUPT ) ? " interrupt" : "", song->volume, song->fadeTime );
	}
	for ( h = 0 ; h < MUSIC_HANDLES ; h++ ) {
		const musicHandle_t	*handle = &music_handles[h];

		if ( handle->stream ) {
			Com_Printf( "handle %i: %s at %i of %i, %s\n", h, music_songs[handle->song].name, handle->posFrame,
				handle->stream->info.samples, handle->state == HANDLE_FADE_IN ? "fading in" :
				handle->state == HANDLE_FADE_OUT ? "fading out" : "steady" );
		}
	}
}

void MUSIC_Init( void ) {
	music_initialised = qtrue;
	Cmd_AddCommand( "loadsoundtrack", MUSIC_LoadSoundtrack_f );
	Cmd_AddCommand( "playsong", MUSIC_PlaySong_f );
	Cmd_AddCommand( "music_info", MUSIC_Info_f );
}
