/*
===========================================================================
TIKI model definitions for Heavy Metal: F.A.K.K. 2.

A .tik text file names a set of animation files (.tan vertex animations or
.ska skeletal animations over a .skb skeleton), assigns shaders to surfaces
and attaches server and client commands to model initialisation and to
animation frames. This module parses those definitions and answers the
queries the game makes about them. It does not render anything.
===========================================================================
*/

#ifndef __TIKI_H__
#define __TIKI_H__

#include "../qcommon/q_shared.h"

typedef struct tiki_s tiki_t;

// frame selectors a command can be attached to, besides a frame number
#define	TIKI_FRAME_CMD_EVERY_FRAME	-1
#define	TIKI_FRAME_CMD_EXIT			-2
#define	TIKI_FRAME_CMD_ENTRY		-3

// animation flags reported to the game
#define	TIKI_ANIM_DELTA_DRIVEN		( 1 << 0 )
#define	TIKI_ANIM_DEFAULT_ANGLES	( 1 << 3 )
#define	TIKI_ANIM_NO_TIMECHECK		( 1 << 4 )

void		TIKI_Shutdown( void );

// Returns the cached definition, loading it on first use, or NULL when the
// file is missing or malformed. A failure is remembered and reported once.
tiki_t		*TIKI_Register( const char *name );

const char	*TIKI_Name( const tiki_t *tiki );

// each model owns one alias list, created on demand by its user
struct AliasList_s	**TIKI_AliasList( tiki_t *tiki );

// what a renderer needs to draw the model
qboolean	TIKI_IsSkeletal( const tiki_t *tiki );
const char	*TIKI_SkelFile( const tiki_t *tiki );	// the .skb file, "" when not skeletal
const char	*TIKI_Path( const tiki_t *tiki );
float		TIKI_Scale( const tiki_t *tiki );
void		TIKI_Origin( const tiki_t *tiki, vec3_t origin );
void		TIKI_LightOffset( const tiki_t *tiki, vec3_t offset );	// the setup's "lightoffset"
const char	*TIKI_Anim_File( const tiki_t *tiki, int animnum );
const char	*TIKI_Surface_Shader( const tiki_t *tiki, int num, int skin );

int			TIKI_NumAnims( const tiki_t *tiki );
int			TIKI_NumSkins( const tiki_t *tiki );
int			TIKI_NumSurfaces( const tiki_t *tiki );
int			TIKI_NumTags( const tiki_t *tiki );
qboolean	TIKI_InitCommands( const tiki_t *tiki, qboolean server, tiki_cmd_t *out );
void		TIKI_CalculateBounds( const tiki_t *tiki, float scale, vec3_t mins, vec3_t maxs );
float		TIKI_Radius( const tiki_t *tiki );

const char	*TIKI_Anim_NameForNum( const tiki_t *tiki, int animnum );
int			TIKI_Anim_NumForName( const tiki_t *tiki, const char *name );
int			TIKI_Anim_Random( const tiki_t *tiki, const char *name );
int			TIKI_Anim_NumFrames( const tiki_t *tiki, int animnum );
float		TIKI_Anim_Time( const tiki_t *tiki, int animnum );
void		TIKI_Anim_Delta( const tiki_t *tiki, int animnum, vec3_t delta );
int			TIKI_Anim_Flags( const tiki_t *tiki, int animnum );
int			TIKI_Anim_CrossblendTime( const tiki_t *tiki, int animnum );
qboolean	TIKI_Anim_HasCommands( const tiki_t *tiki, int animnum, qboolean server );

qboolean	TIKI_Frame_Commands( const tiki_t *tiki, int animnum, int framenum, qboolean server, tiki_cmd_t *out );
void		TIKI_Frame_Delta( const tiki_t *tiki, int animnum, int framenum, vec3_t delta );
float		TIKI_Frame_Time( const tiki_t *tiki, int animnum, int framenum );
void		TIKI_Frame_Bounds( const tiki_t *tiki, int animnum, int framenum, float scale, vec3_t mins, vec3_t maxs );

int			TIKI_Surface_NameToNum( const tiki_t *tiki, const char *name );
const char	*TIKI_Surface_NumToName( const tiki_t *tiki, int num );
int			TIKI_Surface_Flags( const tiki_t *tiki, int num );
int			TIKI_Surface_NumSkins( const tiki_t *tiki, int num );

int			TIKI_Tag_NumForName( const tiki_t *tiki, const char *name );
const char	*TIKI_Tag_NameForNum( const tiki_t *tiki, int num );

// what an entity is playing, for placing its tags: the game modules' refEntity_t
// animation fields, with the frames that are not interpolated towards set to the
// current ones and cross-blend lerps below zero meaning no cross-blend
typedef struct {
	int				anim, frame, oldanim, oldframe;
	float			backlerp;				// 0 = current frame, 1 = old frame
	int				crossblend_anim, crossblend_frame;
	float			crossblend_lerp;		// share of the current animation
	qboolean		uselegs, usetorso;
	int				torso_anim, torso_frame, torso_oldanim, torso_oldframe;
	float			torso_backlerp;
	int				torso_crossblend_anim, torso_crossblend_frame;
	float			torso_crossblend_lerp;
	float			scale;
	const int		*bone_tag;				// NUM_BONE_CONTROLLERS each, or NULL
	const vec4_t	*bone_quat;
} tikiAnimState_t;

void			TIKI_AnimStateFrame( tikiAnimState_t *st, int anim, int frame, float scale, const int *bone_tag, const vec4_t *bone_quat );
orientation_t	TIKI_Tag_Orientation( const tiki_t *tiki, int animnum, int framenum, int tagnum, float scale, const int *bone_tag, const vec4_t *bone_quat );
orientation_t	TIKI_Tag_OrientationEx( const tiki_t *tiki, const tikiAnimState_t *st, int tagnum );

#endif // __TIKI_H__
