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


void S_Init( void );
void S_Shutdown( void );

// if origin is NULL, the sound will be dynamically sourced from the entity.
// FAKK2: a negative volume or minimum distance takes the sound's own from
// global/sound*.txt (see snd_info.c)
void S_StartSound( vec3_t origin, int entnum, int entchannel, sfxHandle_t sfx, float volume, float minDist );
void S_StartLocalSound( sfxHandle_t sfx, int channelNum );
void S_StopSound( int entnum, int entchannel );
void S_StopLocalSound( int channel );		// FAKK2: what S_StartLocalSound started on the channel
// FAKK2: the EAX environment (eax_* in the SDK, 0 generic .. 25 psychotic) and its level, 0 to 1
void S_SetReverb( int type, float level );

void S_StartBackgroundTrack( const char *intro, const char *loop );
void S_StopBackgroundTrack( void );

// cinematics and voice-over-network will send raw samples
// 1.0 volume will be direct output of source samples
void S_RawSamples(int stream, int samples, int rate, int width, int channels,
				   const byte *data, float volume, int entityNum);

// stop all sounds and the background track
void S_StopAllSounds( void );

// all continuous looping sounds must be added before calling S_Update
void S_ClearLoopingSounds( qboolean killall );
void S_AddLoopingSound( int entityNum, const vec3_t origin, const vec3_t velocity, sfxHandle_t sfx, float volume, float minDist );
void S_AddRealLoopingSound( int entityNum, const vec3_t origin, const vec3_t velocity, sfxHandle_t sfx, float volume, float minDist );
void S_StopLoopingSound(int entityNum );

// recompute the relative volumes for all running sounds
// relative to the given entityNum / orientation
void S_Respatialize( int entityNum, const vec3_t origin, vec3_t axis[3], int inwater );

// let the sound system know where an entity currently is
void S_UpdateEntityPosition( int entityNum, const vec3_t origin );

void S_Update( void );

void S_DisableSounds( void );

void S_BeginRegistration( void );

// RegisterSound will allways return a valid sample, even if it
// has to create a placeholder.  This prevents continuous filesystem
// checks for missing files
sfxHandle_t	S_RegisterSound( const char *sample, qboolean compressed );

void S_DisplayFreeMemory(void);

void S_ClearSoundBuffer( void );

void SNDDMA_Activate( void );

void S_UpdateBackgroundTrack( void );

// FAKK2's music (snd_music.c)
void MUSIC_Init( void );
void MUSIC_NewSoundtrack( const char *name );
void MUSIC_UpdateMood( int current, int fallback );
void MUSIC_UpdateVolume( float volume, float fadeTime );
void MUSIC_Update( void );
void MUSIC_StopSongs( void );
void MUSIC_Shutdown( void );


#ifdef USE_VOIP
void S_StartCapture( void );
int S_AvailableCaptureSamples( void );
void S_Capture( int samples, byte *data );
void S_StopCapture( void );
void S_MasterGain( float gain );
#endif

