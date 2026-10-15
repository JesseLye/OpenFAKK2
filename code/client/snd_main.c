/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.
Copyright (C) 2005 Stuart Dalton (badcdev@gmail.com)

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

#include "client.h"
#include "snd_codec.h"
#include "snd_local.h"
#include "snd_public.h"

cvar_t *s_volume;
cvar_t *s_muted;
cvar_t *s_musicVolume;
cvar_t *s_reverb;		// FAKK2
cvar_t *s_doppler;
cvar_t *s_backend;
cvar_t *s_muteWhenMinimized;
cvar_t *s_muteWhenUnfocused;

static soundInterface_t si;

/*
=================
S_ValidateInterface
=================
*/
static qboolean S_ValidSoundInterface( soundInterface_t *pSi )
{
	if( !pSi->Shutdown ) return qfalse;
	if( !pSi->StartSound ) return qfalse;
	if( !pSi->StartLocalSound ) return qfalse;
	if( !pSi->StopSound ) return qfalse;
	if( !pSi->StartBackgroundTrack ) return qfalse;
	if( !pSi->StopBackgroundTrack ) return qfalse;
	if( !pSi->RawSamples ) return qfalse;
	if( !pSi->StopAllSounds ) return qfalse;
	if( !pSi->ClearLoopingSounds ) return qfalse;
	if( !pSi->AddLoopingSound ) return qfalse;
	if( !pSi->AddRealLoopingSound ) return qfalse;
	if( !pSi->StopLoopingSound ) return qfalse;
	if( !pSi->Respatialize ) return qfalse;
	if( !pSi->UpdateEntityPosition ) return qfalse;
	if( !pSi->Update ) return qfalse;
	if( !pSi->DisableSounds ) return qfalse;
	if( !pSi->BeginRegistration ) return qfalse;
	if( !pSi->RegisterSound ) return qfalse;
	if( !pSi->ClearSoundBuffer ) return qfalse;
	if( !pSi->SoundInfo ) return qfalse;
	if( !pSi->SoundList ) return qfalse;

#ifdef USE_VOIP
	if( !pSi->StartCapture ) return qfalse;
	if( !pSi->AvailableCaptureSamples ) return qfalse;
	if( !pSi->Capture ) return qfalse;
	if( !pSi->StopCapture ) return qfalse;
	if( !pSi->MasterGain ) return qfalse;
#endif

	return qtrue;
}

/*
=================
S_StartSound
=================
*/
void S_StartSound( vec3_t origin, int entnum, int entchannel, sfxHandle_t sfx, float volume, float minDist )
{
	if( si.StartSound ) {
		si.StartSound( origin, entnum, entchannel, sfx, volume, minDist );
	}
}

/*
=================
S_StopSound
=================
*/
void S_StopSound( int entnum, int entchannel )
{
	if( si.StopSound ) {
		si.StopSound( entnum, entchannel );
	}
}

/*
=================
S_StopLocalSound
=================
*/
void S_StopLocalSound( int channel )
{
	if( si.StopLocalSound ) {
		si.StopLocalSound( channel );
	}
}

/*
=================
S_SetReverb
=================
*/
void S_SetReverb( int type, float level )
{
	if( si.SetReverb ) {
		si.SetReverb( type, level );
	}
}

/*
=================
S_StartLocalSound
=================
*/
void S_StartLocalSound( sfxHandle_t sfx, int channelNum )
{
	if( si.StartLocalSound ) {
		si.StartLocalSound( sfx, channelNum );
	}
}

/*
=================
S_StartBackgroundTrack
=================
*/
void S_StartBackgroundTrack( const char *intro, const char *loop )
{
	if( si.StartBackgroundTrack ) {
		si.StartBackgroundTrack( intro, loop );
	}
}

/*
=================
S_StopBackgroundTrack
=================
*/
void S_StopBackgroundTrack( void )
{
	if( si.StopBackgroundTrack ) {
		si.StopBackgroundTrack( );
	}
}

/*
=================
S_RawSamples
=================
*/
void S_RawSamples (int stream, int samples, int rate, int width, int channels,
		   const byte *data, float volume, int entityNum)
{
	if(si.RawSamples)
		si.RawSamples(stream, samples, rate, width, channels, data, volume, entityNum);
}

/*
=================
S_StopAllSounds
=================
*/
void S_StopAllSounds( void )
{
	if( si.StopAllSounds ) {
		si.StopAllSounds( );
	}
}

/*
FAKK2's looping sounds. The client game module names no entity for a looping
sound: the retail engine files each one under its sound (0x45c2d0, a slot of
that sound not yet taken this frame, else a free one), and its sound update
(0x45c470) plays every stationary loop of one sound as a single Miles sample.
That sample's volume is the sum of what each loop would give at its distance
(the first loop's volume and minimum distance for all of them, full within the
minimum distance, the inverse of the distance beyond, nothing past 64 times
it), no more than the sound information's "maxfactor" times the loudest of
them when that is 1 or more, and no more than 1; it sits half the minimum
distance from the listener, in the direction of the loops weighted by their
volumes. A new loop of a sound already playing joins that sample where it is.
The module passes no velocity, so every loop is stationary (retail plays a
moving one on its own).

Sounds that burn on several entities at once rely on it: global/sound0.txt
gives the fire on a burning creature (flame2.wav, three on a large one, put
out and lit again as the fire sword keeps hitting it) "maxfactor 1", so any
number of them sounds like the nearest one. Played one per entity, each new
fire started the sound again from its beginning over the others.

So the frame's loops are gathered here and handed to the backend when the
module respatializes (or at the update, if it does not), one per sound, under
a key kept from frame to frame for that sound.
*/
#define	S_MAX_LOOPS			MAX_GENTITIES
#define	S_MAX_LOOP_KEYS		64
#define	S_LOOP_KEY_BASE		( ENTITYNUM_WORLD - 1 )	// keys count down from here, clear of the low entity numbers
#define	S_MAX_LOOP_SFX		4096					// MAX_SFX in both backends

typedef struct {
	vec3_t		origin;
	sfxHandle_t	sfx;
	float		volume;
	float		minDist;
	float		gain;
	qboolean	taken;
} sLoop_t;

static sLoop_t				s_loops[S_MAX_LOOPS];
static int					s_numLoops;
static qboolean				s_loopsChanged;
static int					s_loopKeySfx[S_MAX_LOOP_KEYS];	// each key's sound plus one, 0 for none
static const soundInfo_t	*s_loopSfxInfo[S_MAX_LOOP_SFX];
static vec3_t				s_listenerOrigin;

/*
=================
S_ResetLoops
=================
*/
static void S_ResetLoops( void )
{
	s_numLoops = 0;
	s_loopsChanged = qfalse;
	Com_Memset( s_loopKeySfx, 0, sizeof( s_loopKeySfx ) );
	Com_Memset( (void *)s_loopSfxInfo, 0, sizeof( s_loopSfxInfo ) );
}

/*
=================
S_SendLoops

Merges the frame's loops by sound and hands them to the backend, once for
each S_ClearLoopingSounds.
=================
*/
static void S_SendLoops( void )
{
	sfxHandle_t	sfx[S_MAX_LOOP_KEYS];
	vec3_t		origin[S_MAX_LOOP_KEYS];
	float		volume[S_MAX_LOOP_KEYS], minDist[S_MAX_LOOP_KEYS];
	int			key[S_MAX_LOOP_KEYS];
	qboolean	keyUsed[S_MAX_LOOP_KEYS];
	int			numSounds = 0;
	int			i, j, k;

	if( !s_loopsChanged ) {
		return;
	}
	s_loopsChanged = qfalse;

	for( i = 0; i < s_numLoops; i++ ) {
		s_loops[i].taken = qfalse;
	}

	for( i = 0; i < s_numLoops && numSounds < S_MAX_LOOP_KEYS; i++ ) {
		sLoop_t				*lead = &s_loops[i];
		const soundInfo_t	*info;
		float				vol, dist, total = 0, loudest = 0;
		vec3_t				dir, v;

		if( lead->taken ) {
			continue;
		}

		info = ( lead->sfx >= 0 && lead->sfx < S_MAX_LOOP_SFX ) ? s_loopSfxInfo[lead->sfx] : NULL;
		if( !info ) {
			info = S_SoundInfoForName( NULL );
		}
		vol = lead->volume < 0 ? info->volume : lead->volume;
		minDist[numSounds] = lead->minDist < 0 ? info->minDist : lead->minDist;

		for( j = i; j < s_numLoops; j++ ) {
			sLoop_t	*loop = &s_loops[j];

			if( loop->sfx != lead->sfx ) {
				continue;
			}
			loop->taken = qtrue;
			dist = Distance( loop->origin, s_listenerOrigin );
			if( dist <= minDist[numSounds] ) {
				loop->gain = vol;
			} else if( dist < minDist[numSounds] * SOUND_MAX_DIST_FACTOR ) {
				loop->gain = vol * minDist[numSounds] / dist;
			} else {
				loop->gain = 0;
			}
			total += loop->gain;
			if( loop->gain > loudest ) {
				loudest = loop->gain;
			}
		}

		if( info->maxFactor >= 1 && total > info->maxFactor * loudest ) {
			total = info->maxFactor * loudest;
		}
		if( total <= 0 ) {
			continue;
		}

		VectorClear( dir );
		for( j = i; j < s_numLoops; j++ ) {
			sLoop_t	*loop = &s_loops[j];

			if( loop->sfx != lead->sfx ) {
				continue;
			}
			VectorSubtract( loop->origin, s_listenerOrigin, v );
			VectorNormalize( v );
			VectorMA( dir, loop->gain / total, v, dir );
		}
		VectorNormalize( dir );
		VectorMA( s_listenerOrigin, minDist[numSounds] * 0.5f, dir, origin[numSounds] );

		sfx[numSounds] = lead->sfx;
		volume[numSounds] = total > 1 ? 1 : total;
		numSounds++;
	}

	// a sound keeps the key it had, so the backend carries on playing it
	Com_Memset( keyUsed, 0, sizeof( keyUsed ) );
	for( i = 0; i < numSounds; i++ ) {
		key[i] = -1;
		for( k = 0; k < S_MAX_LOOP_KEYS; k++ ) {
			if( s_loopKeySfx[k] == sfx[i] + 1 ) {
				key[i] = k;
				keyUsed[k] = qtrue;
				break;
			}
		}
	}
	for( i = 0; i < numSounds; i++ ) {
		if( key[i] >= 0 ) {
			continue;
		}
		for( k = 0; k < S_MAX_LOOP_KEYS; k++ ) {
			if( !keyUsed[k] ) {
				key[i] = k;
				keyUsed[k] = qtrue;
				break;
			}
		}
	}
	for( k = 0; k < S_MAX_LOOP_KEYS; k++ ) {
		if( !keyUsed[k] ) {
			s_loopKeySfx[k] = 0;
		}
	}

	for( i = 0; i < numSounds; i++ ) {
		s_loopKeySfx[key[i]] = sfx[i] + 1;
		if( si.AddLoopingSound ) {
			si.AddLoopingSound( S_LOOP_KEY_BASE - key[i], origin[i], vec3_origin,
				sfx[i], volume[i], minDist[i] );
		}
	}
}

/*
=================
S_ClearLoopingSounds
=================
*/
void S_ClearLoopingSounds( qboolean killall )
{
	s_numLoops = 0;
	s_loopsChanged = qtrue;

	if( si.ClearLoopingSounds ) {
		si.ClearLoopingSounds( killall );
	}
}

/*
=================
S_AddLoopingSound
=================
*/
void S_AddLoopingSound( const vec3_t origin, sfxHandle_t sfx, float volume, float minDist )
{
	sLoop_t	*loop;

	if( s_numLoops >= S_MAX_LOOPS ) {
		return;
	}
	loop = &s_loops[s_numLoops++];
	VectorCopy( origin, loop->origin );
	loop->sfx = sfx;
	loop->volume = volume;
	loop->minDist = minDist;
	s_loopsChanged = qtrue;
}

/*
=================
S_AddRealLoopingSound
=================
*/
void S_AddRealLoopingSound( int entityNum, const vec3_t origin,
		const vec3_t velocity, sfxHandle_t sfx, float volume, float minDist )
{
	if( si.AddRealLoopingSound ) {
		si.AddRealLoopingSound( entityNum, origin, velocity, sfx, volume, minDist );
	}
}

/*
=================
S_StopLoopingSound
=================
*/
void S_StopLoopingSound( int entityNum )
{
	if( si.StopLoopingSound ) {
		si.StopLoopingSound( entityNum );
	}
}

/*
=================
S_Respatialize
=================
*/
void S_Respatialize( int entityNum, const vec3_t origin,
		vec3_t axis[3], int inwater )
{
	// the module respatializes after adding the frame's loops, and the
	// software mixer mixes its loops here
	VectorCopy( origin, s_listenerOrigin );
	S_SendLoops( );

	if( si.Respatialize ) {
		si.Respatialize( entityNum, origin, axis, inwater );
	}
}

/*
=================
S_UpdateEntityPosition
=================
*/
void S_UpdateEntityPosition( int entityNum, const vec3_t origin, qboolean useListener )
{
	if( si.UpdateEntityPosition ) {
		si.UpdateEntityPosition( entityNum, origin, useListener );
	}
}

/*
=================
S_Update
=================
*/
void S_Update( void )
{
	if(s_muted->integer)
	{
		if(!(s_muteWhenMinimized->integer && com_minimized->integer) &&
		   !(s_muteWhenUnfocused->integer && com_unfocused->integer))
		{
			s_muted->integer = qfalse;
			s_muted->modified = qtrue;
		}
	}
	else
	{
		if((s_muteWhenMinimized->integer && com_minimized->integer) ||
		   (s_muteWhenUnfocused->integer && com_unfocused->integer))
		{
			s_muted->integer = qtrue;
			s_muted->modified = qtrue;
		}
	}
	
	MUSIC_Update( );	// FAKK2: feeds the songs through the raw streams

	S_SendLoops( );		// a frame that cleared its loops without respatializing

	if( si.Update ) {
		si.Update( );
	}
}

/*
=================
S_DisableSounds
=================
*/
void S_DisableSounds( void )
{
	if( si.DisableSounds ) {
		si.DisableSounds( );
	}
}

/*
=================
S_BeginRegistration
=================
*/
void S_BeginRegistration( void )
{
	if( si.BeginRegistration ) {
		si.BeginRegistration( );
	}
}

/*
=================
S_RegisterSound
=================
*/
sfxHandle_t	S_RegisterSound( const char *sample, qboolean compressed )
{
	sfxHandle_t	sfx;

	if( !si.RegisterSound ) {
		return 0;
	}
	sfx = si.RegisterSound( sample, compressed );
	if( sfx >= 0 && sfx < S_MAX_LOOP_SFX ) {
		s_loopSfxInfo[sfx] = S_SoundInfoForName( sample );	// for the loops' maxfactor
	}
	return sfx;
}

/*
=================
S_ClearSoundBuffer
=================
*/
void S_ClearSoundBuffer( void )
{
	if( si.ClearSoundBuffer ) {
		si.ClearSoundBuffer( );
	}
}

/*
=================
S_SoundInfo
=================
*/
void S_SoundInfo( void )
{
	if( si.SoundInfo ) {
		si.SoundInfo( );
	}
}

/*
=================
S_SoundList
=================
*/
void S_SoundList( void )
{
	if( si.SoundList ) {
		si.SoundList( );
	}
}


#ifdef USE_VOIP
/*
=================
S_StartCapture
=================
*/
void S_StartCapture( void )
{
	if( si.StartCapture ) {
		si.StartCapture( );
	}
}

/*
=================
S_AvailableCaptureSamples
=================
*/
int S_AvailableCaptureSamples( void )
{
	if( si.AvailableCaptureSamples ) {
		return si.AvailableCaptureSamples( );
	}
	return 0;
}

/*
=================
S_Capture
=================
*/
void S_Capture( int samples, byte *data )
{
	if( si.Capture ) {
		si.Capture( samples, data );
	}
}

/*
=================
S_StopCapture
=================
*/
void S_StopCapture( void )
{
	if( si.StopCapture ) {
		si.StopCapture( );
	}
}

/*
=================
S_MasterGain
=================
*/
void S_MasterGain( float gain )
{
	if( si.MasterGain ) {
		si.MasterGain( gain );
	}
}
#endif

//=============================================================================

/*
=================
S_Play_f
=================
*/
void S_Play_f( void ) {
	int 		i;
	int			c;
	sfxHandle_t	h;

	if( !si.RegisterSound || !si.StartLocalSound ) {
		return;
	}

	c = Cmd_Argc();

	if( c < 2 ) {
		Com_Printf ("Usage: play <sound filename> [sound filename] [sound filename] ...\n");
		return;
	}

	for( i = 1; i < c; i++ ) {
		h = si.RegisterSound( Cmd_Argv(i), qfalse );

		if( h ) {
			si.StartLocalSound( h, CHAN_LOCAL_SOUND );
		}
	}
}

/*
=================
S_Music_f
=================
*/
void S_Music_f( void ) {
	int		c;

	if( !si.StartBackgroundTrack ) {
		return;
	}

	c = Cmd_Argc();

	if ( c == 2 ) {
		si.StartBackgroundTrack( Cmd_Argv(1), NULL );
	} else if ( c == 3 ) {
		si.StartBackgroundTrack( Cmd_Argv(1), Cmd_Argv(2) );
	} else {
		Com_Printf ("Usage: music <musicfile> [loopfile]\n");
		return;
	}

}

/*
=================
S_Music_f
=================
*/
void S_StopMusic_f( void )
{
	if(!si.StopBackgroundTrack)
		return;

	si.StopBackgroundTrack();
}


//=============================================================================

/*
=================
S_Init
=================
*/
void S_Init( void )
{
	cvar_t		*cv;
	qboolean	started = qfalse;

	Com_Printf( "------ Initializing Sound ------\n" );

	// FAKK2: retail's defaults (fakk2.exe 0x43e67b)
	s_volume = Cvar_Get( "s_volume", "1.0", CVAR_ARCHIVE );
	s_musicVolume = Cvar_Get( "s_musicvolume", "0.60", CVAR_ARCHIVE );
	// s_separation sets how far apart retail's own mixer puts the two ears
	// (0x43f2cc) and the pan it gives Miles (0x45e8d0); with Miles' 3D provider,
	// which places the sounds itself, it changes nothing: recordings of a tone at
	// fourteen bearings with 0.5 and 0 matched (probe-sound-pan.scr). It is kept
	// for configs that set it, and changes nothing here either
	Cvar_Get( "s_separation", "0.5", CVAR_ARCHIVE );
	Cvar_Get( "s_loadas8bit", "0", CVAR_ARCHIVE | CVAR_LATCH | CVAR_SOUND_LATCH );	// snd_codec.c
	s_reverb = Cvar_Get( "s_reverb", "1", CVAR_ARCHIVE );	// FAKK2: retail defaulted to 0, needing an EAX card
	s_muted = Cvar_Get("s_muted", "0", CVAR_ROM);
	s_doppler = Cvar_Get( "s_doppler", "1", CVAR_ARCHIVE );
	s_backend = Cvar_Get( "s_backend", "", CVAR_ROM );
	s_muteWhenMinimized = Cvar_Get( "s_muteWhenMinimized", "0", CVAR_ARCHIVE );
	s_muteWhenUnfocused = Cvar_Get( "s_muteWhenUnfocused", "0", CVAR_ARCHIVE );

	cv = Cvar_Get( "s_initsound", "1", 0 );
	if( !cv->integer ) {
		Com_Printf( "Sound disabled.\n" );
	} else {

		S_CodecInit( );
		S_LoadSoundInfo( );		// FAKK2: global/sound*.txt

		Cmd_AddCommand( "play", S_Play_f );
		Cmd_AddCommand( "music", S_Music_f );
		Cmd_AddCommand( "stopmusic", S_StopMusic_f );
		Cmd_AddCommand( "s_list", S_SoundList );
		Cmd_AddCommand( "s_stop", S_StopAllSounds );
		Cmd_AddCommand( "stopsound", S_StopAllSounds );	// FAKK2: the game sends it when a level ends
		Cmd_AddCommand( "s_info", S_SoundInfo );
		Cmd_AddCommand( "s_soundinfo", S_SoundInfo_f );	// FAKK2
		Cmd_AddCommand( "s_setreverb", S_SetReverb_f );	// FAKK2
		MUSIC_Init( );

		cv = Cvar_Get( "s_useOpenAL", "1", CVAR_ARCHIVE | CVAR_LATCH | CVAR_SOUND_LATCH );
		if( cv->integer ) {
			//OpenAL
			started = S_AL_Init( &si );
			Cvar_Set( "s_backend", "OpenAL" );
		}

		if( !started ) {
			started = S_Base_Init( &si );
			Cvar_Set( "s_backend", "base" );
		}

		if( started ) {
			if( !S_ValidSoundInterface( &si ) ) {
				Com_Error( ERR_FATAL, "Sound interface invalid" );
			}

			S_SoundInfo( );
			Com_Printf( "Sound initialization successful.\n" );
		} else {
			Com_Printf( "Sound initialization failed.\n" );
		}
	}

	Com_Printf( "--------------------------------\n");
}

/*
=================
S_Shutdown
=================
*/
void S_Shutdown( void )
{
	MUSIC_Shutdown( );
	if( si.Shutdown ) {
		si.Shutdown( );
	}

	Com_Memset( &si, 0, sizeof( soundInterface_t ) );
	S_ResetLoops( );

	Cmd_RemoveCommand( "play" );
	Cmd_RemoveCommand( "music");
	Cmd_RemoveCommand( "stopmusic");
	Cmd_RemoveCommand( "s_list" );
	Cmd_RemoveCommand( "s_stop" );
	Cmd_RemoveCommand( "stopsound" );
	Cmd_RemoveCommand( "s_info" );
	Cmd_RemoveCommand( "s_soundinfo" );
	Cmd_RemoveCommand( "s_setreverb" );

	S_CodecShutdown( );
}

