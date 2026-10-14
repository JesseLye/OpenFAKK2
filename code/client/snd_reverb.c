/*
===========================================================================
FAKK2's reverb, for the software mixer.

The game sends a reverb type and level in the player state (its
TriggerReverb entities, placed in the level's .snd file, set them as the
player walks through them); the client game module hands them to
S_SetReverb. The retail engine (0x45d3d0) passes the type straight to the
Miles sound system as an EAX environment (AIL_set_3D_room_type) and the
level as the EAX effect volume, so only a card with EAX ever produced it,
and its s_reverb cvar defaulted to 0.

Here the 26 EAX environments are the EFX reverb presets OpenAL Soft
publishes for them (efx-presets.h), rendered by a Freeverb-style late
reverb (Jezar's public-domain design: eight parallel comb filters and four
allpasses per channel): each comb's feedback is set from the preset's
decay time, its damping from the high-frequency decay ratio, the
pre-delay from the reflections and late reverb delays, and the wet gain
from the preset's gain times its late reverb gain times the level. Only
world sounds feed it; local sounds, music and other raw streams stay dry.
===========================================================================
*/

#include "snd_local.h"
#include "../thirdparty/openal-soft-1.24.3/include/AL/efx-presets.h"

#define REVERB_TYPES		26
#define NUM_COMBS			8
#define NUM_ALLPASSES		4
#define REVERB_SPREAD		23			// extra samples on the right channel's delays
#define REVERB_TUNING_RATE	44100		// the delays below are for this rate
#define REVERB_FIXED_GAIN	0.015f		// Freeverb's input scale
#define REVERB_ALLPASS_FEEDBACK	0.5f
#define REVERB_WET_SCALE	3.0f		// Freeverb's output scale (its "scalewet")
#define REVERB_MAX_WET		2.0f
#define REVERB_MAX_PREDELAY_MS	400		// the longest preset (mountains) is 0.4 s

static const int combTuning[NUM_COMBS] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
static const int allpassTuning[NUM_ALLPASSES] = { 556, 441, 341, 225 };

// in the order of the game's eax_* numbering (q_shared.h)
static const EFXEAXREVERBPROPERTIES reverbPresets[REVERB_TYPES] = {
	EFX_REVERB_PRESET_GENERIC, EFX_REVERB_PRESET_PADDEDCELL, EFX_REVERB_PRESET_ROOM,
	EFX_REVERB_PRESET_BATHROOM, EFX_REVERB_PRESET_LIVINGROOM, EFX_REVERB_PRESET_STONEROOM,
	EFX_REVERB_PRESET_AUDITORIUM, EFX_REVERB_PRESET_CONCERTHALL, EFX_REVERB_PRESET_CAVE,
	EFX_REVERB_PRESET_ARENA, EFX_REVERB_PRESET_HANGAR, EFX_REVERB_PRESET_CARPETEDHALLWAY,
	EFX_REVERB_PRESET_HALLWAY, EFX_REVERB_PRESET_STONECORRIDOR, EFX_REVERB_PRESET_ALLEY,
	EFX_REVERB_PRESET_FOREST, EFX_REVERB_PRESET_CITY, EFX_REVERB_PRESET_MOUNTAINS,
	EFX_REVERB_PRESET_QUARRY, EFX_REVERB_PRESET_PLAIN, EFX_REVERB_PRESET_PARKINGLOT,
	EFX_REVERB_PRESET_SEWERPIPE, EFX_REVERB_PRESET_UNDERWATER, EFX_REVERB_PRESET_DRUGGED,
	EFX_REVERB_PRESET_DIZZY, EFX_REVERB_PRESET_PSYCHOTIC
};

static const char *reverbNames[REVERB_TYPES] = {
	"generic", "paddedcell", "room", "bathroom", "livingroom", "stoneroom", "auditorium",
	"concerthall", "cave", "arena", "hangar", "carpetedhallway", "hallway", "stonecorridor",
	"alley", "forest", "city", "mountains", "quarry", "plain", "parkinglot", "sewerpipe",
	"underwater", "drugged", "dizzy", "psychotic"
};

typedef struct {
	float	*buf;
	int		size, pos;
	float	feedback;
	float	damp1, damp2;
	float	filterStore;
} comb_t;

typedef struct {
	float	*buf;
	int		size, pos;
} allpass_t;

typedef struct {
	comb_t		combs[NUM_COMBS];
	allpass_t	allpasses[NUM_ALLPASSES];
} reverbChannel_t;

static reverbChannel_t	rv_channels[2];
static float			*rv_predelay;
static int				rv_predelaySize, rv_predelayPos, rv_predelayLen;
static int				rv_rate;
static int				rv_type = -1;
static float			rv_level;
static float			rv_wet;

static void S_ReverbClear( void ) {
	int		c, i;

	for ( c = 0 ; c < 2 ; c++ ) {
		for ( i = 0 ; i < NUM_COMBS ; i++ ) {
			Com_Memset( rv_channels[c].combs[i].buf, 0, rv_channels[c].combs[i].size * sizeof( float ) );
			rv_channels[c].combs[i].filterStore = 0;
		}
		for ( i = 0 ; i < NUM_ALLPASSES ; i++ ) {
			Com_Memset( rv_channels[c].allpasses[i].buf, 0, rv_channels[c].allpasses[i].size * sizeof( float ) );
		}
	}
	Com_Memset( rv_predelay, 0, rv_predelaySize * sizeof( float ) );
}

/*
=================
S_ReverbInit

Sizes the delay lines for the output rate. May be called again.
=================
*/
void S_ReverbInit( int rate ) {
	int		c, i;

	if ( rate == rv_rate ) {
		return;
	}
	S_ReverbShutdown();
	rv_rate = rate;

	for ( c = 0 ; c < 2 ; c++ ) {
		int		spread = c ? REVERB_SPREAD : 0;

		for ( i = 0 ; i < NUM_COMBS ; i++ ) {
			comb_t	*comb = &rv_channels[c].combs[i];

			comb->size = ( combTuning[i] + spread ) * rate / REVERB_TUNING_RATE;
			comb->buf = Z_Malloc( comb->size * sizeof( float ) );
		}
		for ( i = 0 ; i < NUM_ALLPASSES ; i++ ) {
			allpass_t	*ap = &rv_channels[c].allpasses[i];

			ap->size = ( allpassTuning[i] + spread ) * rate / REVERB_TUNING_RATE;
			ap->buf = Z_Malloc( ap->size * sizeof( float ) );
		}
	}
	rv_predelaySize = REVERB_MAX_PREDELAY_MS * rate / 1000 + 1;
	rv_predelay = Z_Malloc( rv_predelaySize * sizeof( float ) );
	rv_predelayPos = 0;

	// the settings carry over a restart
	if ( rv_type >= 0 ) {
		int		type = rv_type;

		rv_type = -1;
		S_ReverbSet( type, rv_level );
	}
}

void S_ReverbShutdown( void ) {
	int		c, i;

	if ( !rv_rate ) {
		return;
	}
	for ( c = 0 ; c < 2 ; c++ ) {
		for ( i = 0 ; i < NUM_COMBS ; i++ ) {
			Z_Free( rv_channels[c].combs[i].buf );
			rv_channels[c].combs[i].buf = NULL;
		}
		for ( i = 0 ; i < NUM_ALLPASSES ; i++ ) {
			Z_Free( rv_channels[c].allpasses[i].buf );
			rv_channels[c].allpasses[i].buf = NULL;
		}
	}
	Z_Free( rv_predelay );
	rv_predelay = NULL;
	rv_rate = 0;
}

/*
=================
S_ReverbSet

The environment and how much of it, 0 to 1. A new environment starts clean.
=================
*/
void S_ReverbSet( int type, float level ) {
	const EFXEAXREVERBPROPERTIES	*p;
	float							damp;
	int								c, i;

	if ( type < 0 || type >= REVERB_TYPES ) {
		type = 0;
	}
	if ( level < 0 ) {
		level = 0;
	} else if ( level > 1 ) {
		level = 1;
	}
	p = &reverbPresets[type];
	rv_level = level;
	rv_wet = p->flGain * p->flLateReverbGain * level * REVERB_WET_SCALE;
	if ( rv_wet > REVERB_MAX_WET ) {
		rv_wet = REVERB_MAX_WET;	// underwater and psychotic would clip loud sounds
	}

	if ( type == rv_type || !rv_rate ) {
		rv_type = type;
		return;
	}
	rv_type = type;
	Com_DPrintf( "reverb: %s, level %.2f\n", reverbNames[type], level );

	// a comb's feedback is what leaves its decay time as the preset's (60 dB)
	damp = 1.0f - p->flDecayHFRatio;
	if ( damp < 0.05f ) {
		damp = 0.05f;
	} else if ( damp > 0.9f ) {
		damp = 0.9f;
	}
	for ( c = 0 ; c < 2 ; c++ ) {
		for ( i = 0 ; i < NUM_COMBS ; i++ ) {
			comb_t	*comb = &rv_channels[c].combs[i];

			comb->feedback = (float)pow( 10.0, -3.0 * comb->size / ( p->flDecayTime * rv_rate ) );
			comb->damp1 = damp;
			comb->damp2 = 1.0f - damp;
		}
	}
	rv_predelayLen = (int)( ( p->flReflectionsDelay + p->flLateReverbDelay ) * rv_rate );
	if ( rv_predelayLen >= rv_predelaySize ) {
		rv_predelayLen = rv_predelaySize - 1;
	}
	S_ReverbClear();
}

qboolean S_ReverbActive( void ) {
	return rv_rate && rv_type >= 0 && rv_wet > 0;
}

/*
=================
S_ReverbProcess

Adds the reverb of the send to the paint buffer.
=================
*/
void S_ReverbProcess( portable_samplepair_t *paint, const portable_samplepair_t *send, int count ) {
	int		i, c, k;

	if ( !S_ReverbActive() ) {
		return;
	}
	for ( i = 0 ; i < count ; i++ ) {
		float	input = ( send[i].left + send[i].right ) * ( 0.5f * REVERB_FIXED_GAIN );
		float	delayed;
		float	out[2];

		// pre-delay
		rv_predelay[rv_predelayPos] = input;
		k = rv_predelayPos - rv_predelayLen;
		if ( k < 0 ) {
			k += rv_predelaySize;
		}
		delayed = rv_predelay[k];
		if ( ++rv_predelayPos == rv_predelaySize ) {
			rv_predelayPos = 0;
		}

		for ( c = 0 ; c < 2 ; c++ ) {
			reverbChannel_t	*ch = &rv_channels[c];
			float			acc = 0;

			for ( k = 0 ; k < NUM_COMBS ; k++ ) {
				comb_t	*comb = &ch->combs[k];
				float	output = comb->buf[comb->pos];

				comb->filterStore = output * comb->damp2 + comb->filterStore * comb->damp1;
				comb->buf[comb->pos] = delayed + comb->filterStore * comb->feedback;
				if ( ++comb->pos >= comb->size ) {
					comb->pos = 0;
				}
				acc += output;
			}
			for ( k = 0 ; k < NUM_ALLPASSES ; k++ ) {
				allpass_t	*ap = &ch->allpasses[k];
				float		bufout = ap->buf[ap->pos];
				float		in = acc;

				acc = bufout - in;
				ap->buf[ap->pos] = in + bufout * REVERB_ALLPASS_FEEDBACK;
				if ( ++ap->pos >= ap->size ) {
					ap->pos = 0;
				}
			}
			out[c] = acc * rv_wet;
		}
		paint[i].left += (int)out[0];
		paint[i].right += (int)out[1];
	}
}

const char *S_ReverbName( int type ) {
	return ( type >= 0 && type < REVERB_TYPES ) ? reverbNames[type] : "";
}

// for the OpenAL backend, which hands the preset to EFX
const EFXEAXREVERBPROPERTIES *S_ReverbPreset( int type ) {
	return &reverbPresets[( type >= 0 && type < REVERB_TYPES ) ? type : 0];
}

/*
=================
S_SetReverb_f

s_setreverb [<type>|<name> [level]]: sets the environment by hand, until the
game's next trigger changes it; alone, prints the current one and the names.
=================
*/
void S_SetReverb_f( void ) {
	int		type, i;
	float	level;

	if ( Cmd_Argc() < 2 ) {
		Com_Printf( "reverb: %s, level %.2f%s\n", rv_type >= 0 ? reverbNames[rv_type] : "none", rv_level,
			s_reverb->integer ? "" : " (s_reverb is 0)" );
		Com_Printf( "usage: s_setreverb <type|name> [level 0..1]\ntypes:" );
		for ( i = 0 ; i < REVERB_TYPES ; i++ ) {
			Com_Printf( " %d:%s", i, reverbNames[i] );
		}
		Com_Printf( "\n" );
		return;
	}
	type = -1;
	for ( i = 0 ; i < REVERB_TYPES ; i++ ) {
		if ( !Q_stricmp( Cmd_Argv( 1 ), reverbNames[i] ) ) {
			type = i;
		}
	}
	if ( type < 0 ) {
		type = atoi( Cmd_Argv( 1 ) );
		if ( type < 0 || type >= REVERB_TYPES ) {
			Com_Printf( "no such reverb type\n" );
			return;
		}
	}
	level = Cmd_Argc() > 2 ? atof( Cmd_Argv( 2 ) ) : 0.5f;
	S_SetReverb( type, level );
	Com_Printf( "reverb: %s, level %.2f\n", reverbNames[type], level );
}
