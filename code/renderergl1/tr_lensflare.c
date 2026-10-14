/*
===========================================================================
FAKK2's lens flares, after the retail renderer (fakk2.exe 0x48bb20 draws
them, 0x48b690 reads scripts/lensflare2.txt, 0x48bf90 sets the three sets
up).

The file defines three sets, "sun", "entity" and "dlight", each with
dot_min (how squarely the view must face the light before its flares
show), fullscale (how bright the set's full-screen blend gets), an
optional fullscreen shader with its fullfade time, and flare lines
<size> <where> <shader> [alphascale].

The retail draw works in a -1..1 screen space: the light's screen point P
comes from the view's matrices; flare i sits at P * (1 - 2 * where), so
where 0 is the light, 0.5 the screen's centre and 1 the point opposite the
light; its quad is +-size in that space (so wider than tall on a wide
screen); its colour is the light's with alpha = alphascale * t, where
t = (dot - dot_min) / (1 - dot_min). A flare shows only when the light is
inside the view, dot > dot_min and a world trace from the eye to the light
is clear (to a far point in the light's direction that must hit the sky
when the light is in the portal sky). The set's full-screen blend is
white at fullscale * t^4, fading over fullfade ms once the light is gone.

The sun is the worldspawn's "sunflare" point, in the sky camera's world
when "sunflare_inportalsky" is set. An entity with RF_LENSFLARE gets the
entity set in its colour, plus a "corona" quad (textures/sprites/corona,
r_lightcoronasize half-size) at the light whenever it is in front of and
visible to the eye; a dynamic light with the lensflare or viewlensflare
bit gets the same; an entity with RF_VIEWLENSFLARE gets the dlight set.
===========================================================================
*/

#include "tr_local.h"

#define LF_FILE			"scripts/lensflare2.txt"
#define LF_MAX_FLARES	20			// retail's set holds 20
#define LF_FAR			16384.0f

// the game's q_shared.h renderfx bits
#define LF_RF_VIEWLENSFLARE	( 1 << 3 )
#define LF_RF_LENSFLARE		( 1 << 8 )
#define LF_RF_SKYENTITY		( 1 << 14 )

// the cgame's light flags (tr_types.h)
#define LF_LIGHT_LENSFLARE		1
#define LF_LIGHT_VIEWLENSFLARE	2

typedef struct {
	float		size, where, alphaScale;
	shader_t	*shader;
} lfFlare_t;

typedef struct {
	const char	*name;
	qboolean	defined;
	float		dotMin;
	float		fullScale;
	int			fullFade;
	shader_t	*fullscreen;
	int			numFlares;
	lfFlare_t	flares[LF_MAX_FLARES];

	// the light being drawn
	vec3_t		origin;			// in the view's world
	vec3_t		farPoint;		// where the trace towards it ends
	qboolean	inSky;			// seen through the portal sky
	vec4_t		color;
	float		screenX, screenY;	// -1..1

	// the full-screen blend
	qboolean	fullActive;
	int			fullTime;
	float		fullAlpha;

	int			lastPrint;		// r_lensflare 2's trace
} lfSet_t;

static lfSet_t	lf_sun = { "sun" }, lf_entity = { "entity" }, lf_dlight = { "dlight" };
static shader_t	*lf_corona;
static qboolean	lf_loaded;

void R_LensFlareReset( void ) {
	lf_loaded = qfalse;
}

/*
=================
R_LoadLensFlareSet

Retail 0x48b690: the section's defaults are dot_min .8 and fullscale .7.
=================
*/
static void R_LoadLensFlareSet( lfSet_t *set, const char *buf ) {
	const char	*p = buf;
	char		*tok;

	set->defined = qfalse;
	set->dotMin = 0.8f;
	set->fullScale = 0.7f;
	set->fullFade = 0;
	set->fullscreen = NULL;
	set->numFlares = 0;
	set->fullActive = qfalse;

	// find "begin <name>"
	while ( 1 ) {
		tok = COM_ParseExt( (char **)&p, qtrue );
		if ( !tok[0] ) {
			ri.Printf( PRINT_WARNING, "WARNING: could not find begin for section '%s' in lensflares\n", set->name );
			return;
		}
		if ( !Q_stricmp( tok, "begin" ) ) {
			tok = COM_ParseExt( (char **)&p, qfalse );
			if ( !Q_stricmp( tok, set->name ) ) {
				break;
			}
		}
	}
	set->defined = qtrue;

	while ( 1 ) {
		tok = COM_ParseExt( (char **)&p, qtrue );
		if ( !tok[0] ) {
			ri.Printf( PRINT_WARNING, "WARNING: could not find end for section '%s' in lensflares\n", set->name );
			break;
		}
		if ( !Q_stricmp( tok, "end" ) ) {
			break;
		}
		if ( !Q_stricmp( tok, "dot_min" ) ) {
			tok = COM_ParseExt( (char **)&p, qfalse );
			if ( !tok[0] ) {
				ri.Printf( PRINT_WARNING, "WARNING: invalid dot_min in lensflares\n" );
			} else {
				set->dotMin = atof( tok );
			}
		} else if ( !Q_stricmp( tok, "flare" ) ) {
			lfFlare_t	f;

			f.alphaScale = 1.0f;
			tok = COM_ParseExt( (char **)&p, qfalse );
			if ( !tok[0] ) {
				ri.Printf( PRINT_WARNING, "WARNING: no flare arg 1 for flare '%d' in lensflares\n", set->numFlares );
				continue;
			}
			f.size = atof( tok );
			tok = COM_ParseExt( (char **)&p, qfalse );
			if ( !tok[0] ) {
				ri.Printf( PRINT_WARNING, "WARNING: no flare arg 2 for flare '%d' in lensflares\n", set->numFlares );
				continue;
			}
			f.where = atof( tok );
			tok = COM_ParseExt( (char **)&p, qfalse );
			if ( !tok[0] ) {
				ri.Printf( PRINT_WARNING, "WARNING: no flare arg 3 for flare '%d' in lensflares\n", set->numFlares );
				continue;
			}
			f.shader = R_FindShader( tok, LIGHTMAP_NONE, qfalse );
			tok = COM_ParseExt( (char **)&p, qfalse );
			if ( tok[0] ) {
				f.alphaScale = atof( tok );
			}
			if ( set->numFlares < LF_MAX_FLARES ) {
				set->flares[set->numFlares++] = f;
			}
		} else if ( !Q_stricmp( tok, "fullscale" ) ) {
			tok = COM_ParseExt( (char **)&p, qfalse );
			if ( !tok[0] ) {
				ri.Printf( PRINT_WARNING, "WARNING: no arg for fullscale, assuming default (in lensflares)\n" );
			} else {
				set->fullScale = atof( tok );
			}
		} else if ( !Q_stricmp( tok, "fullscreen" ) ) {
			tok = COM_ParseExt( (char **)&p, qfalse );
			if ( !tok[0] ) {
				ri.Printf( PRINT_WARNING, "WARNING: no arg for fullscreen in lensflares\n" );
			} else {
				set->fullscreen = R_FindShader( tok, LIGHTMAP_NONE, qfalse );
			}
		} else if ( !Q_stricmp( tok, "fullfade" ) ) {
			tok = COM_ParseExt( (char **)&p, qfalse );
			if ( !tok[0] ) {
				ri.Printf( PRINT_WARNING, "WARNING: no arg for fullfade in lensflares\n" );
			} else {
				set->fullFade = atoi( tok );
			}
		} else {
			SkipRestOfLine( (char **)&p );
		}
	}
	if ( !set->numFlares ) {
		ri.Printf( PRINT_WARNING, "WARNING: no lensflares defined!\n" );
	}
	if ( !set->fullscreen && !Q_stricmp( set->name, "sun" ) ) {
		ri.Printf( PRINT_WARNING, "WARNING: no full screen blend shader defined for the sun's lensflares\n" );
	}
}

static void R_LoadLensFlares( void ) {
	char	*buf;

	lf_loaded = qtrue;
	lf_sun.defined = lf_entity.defined = lf_dlight.defined = qfalse;
	if ( ri.FS_ReadFile( LF_FILE, (void **)&buf ) <= 0 ) {
		ri.Printf( PRINT_WARNING, "WARNING: Could not open lens flare file %s\n", LF_FILE );
		return;
	}
	R_LoadLensFlareSet( &lf_sun, buf );
	R_LoadLensFlareSet( &lf_entity, buf );
	R_LoadLensFlareSet( &lf_dlight, buf );
	ri.FS_FreeFile( buf );
	lf_corona = R_FindShader( "textures/sprites/corona", LIGHTMAP_NONE, qfalse );
}

/*
=================
LF_SetLight

Retail 0x48af10: a light in the portal sky is a direction from the sky
camera, turned by the sky's axis into the view's world.
=================
*/
static void LF_SetLight( lfSet_t *set, const viewParms_t *view, const vec3_t pos, qboolean inSky, const vec4_t color ) {
	Vector4Copy( color, set->color );
	set->inSky = inSky && tr.refdef.sky_portal;
	if ( set->inSky ) {
		vec3_t	d, r;

		VectorSubtract( pos, tr.refdef.sky_origin, d );
		r[0] = DotProduct( d, tr.refdef.sky_axis[0] );
		r[1] = DotProduct( d, tr.refdef.sky_axis[1] );
		r[2] = DotProduct( d, tr.refdef.sky_axis[2] );
		VectorAdd( view->or.origin, r, set->origin );
		VectorNormalize( r );
		VectorMA( view->or.origin, LF_FAR, r, set->farPoint );
	} else {
		VectorCopy( pos, set->origin );
		VectorCopy( pos, set->farPoint );
	}
}

/*
=================
LF_Project

The light's -1..1 screen point; false when it is behind the eye.
=================
*/
static qboolean LF_Project( lfSet_t *set, const viewParms_t *view, float *dot ) {
	vec3_t	dir;
	float	f;

	VectorSubtract( set->origin, view->or.origin, dir );
	f = DotProduct( dir, view->or.axis[0] );
	if ( f <= 0 ) {
		return qfalse;
	}
	set->screenX = -( DotProduct( dir, view->or.axis[1] ) / f ) / tan( DEG2RAD( view->fovX * 0.5f ) );
	set->screenY = ( DotProduct( dir, view->or.axis[2] ) / f ) / tan( DEG2RAD( view->fovY * 0.5f ) );
	*dot = f / VectorLength( dir );
	return qtrue;
}

/*
=================
LF_Visible

The world between the eye and the light: nothing in the way, or the sky
in its direction when it is in the portal sky.
=================
*/
static qboolean LF_Visible( const lfSet_t *set, const viewParms_t *view ) {
	trace_t	trace;

	if ( !ri.CM_BoxTrace ) {
		return qtrue;
	}
	ri.CM_BoxTrace( &trace, view->or.origin, set->farPoint, NULL, NULL, 0, CONTENTS_SOLID, qfalse );
	if ( set->inSky ) {
		return ( trace.surfaceFlags & SURF_SKY ) != 0;
	}
	return trace.fraction == 1.0f;
}

/*
=================
LF_DrawQuad

A quad in the -1..1 screen space: x, y its centre, sx, sy its half-sizes.
=================
*/
static void LF_DrawQuad( const viewParms_t *view, float x, float y, float sx, float sy, const vec4_t color, shader_t *shader ) {
	float	halfW = view->viewportWidth * 0.5f;
	float	halfH = view->viewportHeight * 0.5f;
	float	cx = view->viewportX + halfW;
	float	cy = glConfig.vidHeight - ( view->viewportY + halfH );

	if ( !shader || shader == tr.defaultShader ) {
		return;
	}
	RE_SetColor( color );
	RE_StretchPic( cx + ( x - sx ) * halfW, cy - ( y + sy ) * halfH, 2 * sx * halfW, 2 * sy * halfH, 0, 0, 1, 1, shader->index );
}

/*
=================
LF_DrawSet

Retail 0x48b160.
=================
*/
static void LF_DrawSet( lfSet_t *set, const viewParms_t *view ) {
	float	dot, t;
	vec4_t	color;
	int		i;

	if ( !set->defined ) {
		return;
	}
	if ( !LF_Project( set, view, &dot ) ) {
		return;
	}
	if ( r_lensflare->integer > 1 && tr.refdef.time / 250 != set->lastPrint ) {
		vec3_t	dir, angles;

		set->lastPrint = tr.refdef.time / 250;
		VectorSubtract( set->origin, view->or.origin, dir );
		vectoangles( dir, angles );
		ri.Printf( PRINT_ALL, "lensflare: %s dot %.3f (min %.3f) colour %.2f %.2f %.2f %.2f at yaw %.0f pitch %.0f, screen %.2f %.2f, time %d\n",
			set->name, dot, set->dotMin, set->color[0], set->color[1], set->color[2], set->color[3],
			angles[YAW], angles[PITCH], set->screenX, set->screenY, tr.refdef.time );
	}
	if ( fabs( set->screenX ) >= 1 || fabs( set->screenY ) >= 1 ) {
		return;
	}
	if ( dot <= set->dotMin ) {
		return;
	}
	if ( !LF_Visible( set, view ) ) {
		return;
	}
	t = ( dot - set->dotMin ) / ( 1.0f - set->dotMin );
	for ( i = 0 ; i < set->numFlares ; i++ ) {
		const lfFlare_t	*f = &set->flares[i];
		float			k = 1.0f - 2.0f * f->where;

		VectorCopy( set->color, color );
		color[3] = f->alphaScale * set->color[3] * t;
		LF_DrawQuad( view, set->screenX * k, set->screenY * k, f->size, f->size, color, f->shader );
	}
	set->fullActive = qtrue;
	set->fullTime = tr.refdef.time;
	set->fullAlpha = t * t * t * t * set->fullScale;
}

/*
=================
LF_DrawLight

Retail 0x48b3c0: the set's flares, then the corona at the light.
=================
*/
static void LF_DrawLight( lfSet_t *set, const viewParms_t *view ) {
	float	dot, s;

	LF_DrawSet( set, view );
	if ( !LF_Project( set, view, &dot ) ) {
		return;
	}
	if ( !LF_Visible( set, view ) ) {
		return;
	}
	s = r_lightcoronasize->value;
	LF_DrawQuad( view, set->screenX, set->screenY, s, s, set->color, lf_corona );
}

/*
=================
LF_DrawFullscreen

Retail 0x48b5a0: the blend of the set's last showing, fading over fullfade.
=================
*/
static void LF_DrawFullscreen( lfSet_t *set, const viewParms_t *view ) {
	int		elapsed;
	float	alpha;
	vec4_t	white = { 1, 1, 1, 1 };

	if ( !set->fullscreen || !set->fullActive ) {
		return;
	}
	elapsed = tr.refdef.time - set->fullTime;
	if ( elapsed > set->fullFade ) {
		set->fullActive = qfalse;
		return;
	}
	alpha = set->fullAlpha;
	if ( set->fullFade > 0 ) {
		alpha *= ( set->fullFade - elapsed ) / (float)set->fullFade;
	}
	if ( alpha <= 0.0001f ) {
		set->fullActive = qfalse;
		return;
	}
	white[3] = alpha;
	LF_DrawQuad( view, 0, 0, 1, 1, white, set->fullscreen );
}

/*
=================
R_DrawLensFlares

After the main view (retail 0x48bb20).
=================
*/
void R_DrawLensFlares( const viewParms_t *view ) {
	vec4_t	white = { 1, 1, 1, 1 }, color;
	int		i;

	if ( !lf_loaded ) {
		R_LoadLensFlares();
	}

	if ( tr.world && r_sunflare->string[0] && Q_stricmp( r_sunflare->string, "0 0 0" ) ) {
		// retail's developer placing of the sun (0x48bb20 reads r_sunflare)
		vec3_t	pos;

		if ( sscanf( r_sunflare->string, "%f %f %f", &pos[0], &pos[1], &pos[2] ) == 3 ) {
			LF_SetLight( &lf_sun, view, pos, r_sunflare_inportalsky->integer != 0, white );
			LF_DrawSet( &lf_sun, view );
		}
	} else if ( tr.hasSunFlare && tr.world ) {
		LF_SetLight( &lf_sun, view, tr.sunFlareOrigin, tr.sunFlareInPortalSky, white );
		LF_DrawSet( &lf_sun, view );
	}

	for ( i = 0 ; i < tr.refdef.num_entities ; i++ ) {
		const refEntity_t	*e = &tr.refdef.entities[i].e;

		if ( !( e->renderfx & ( LF_RF_LENSFLARE | LF_RF_VIEWLENSFLARE ) ) ) {
			continue;
		}
		color[0] = e->shaderRGBA[0] / 255.0f;
		color[1] = e->shaderRGBA[1] / 255.0f;
		color[2] = e->shaderRGBA[2] / 255.0f;
		color[3] = e->shaderRGBA[3] / 255.0f;
		if ( e->renderfx & LF_RF_LENSFLARE ) {
			LF_SetLight( &lf_entity, view, e->origin, ( e->renderfx & LF_RF_SKYENTITY ) != 0, color );
			LF_DrawLight( &lf_entity, view );
		}
		if ( e->renderfx & LF_RF_VIEWLENSFLARE ) {
			LF_SetLight( &lf_dlight, view, e->origin, ( e->renderfx & LF_RF_SKYENTITY ) != 0, color );
			LF_DrawSet( &lf_dlight, view );
		}
	}

	for ( i = 0 ; i < tr.refdef.num_dlights ; i++ ) {
		const dlight_t	*dl = &tr.refdef.dlights[i];

		if ( !( dl->flags & ( LF_LIGHT_LENSFLARE | LF_LIGHT_VIEWLENSFLARE ) ) ) {
			continue;
		}
		VectorCopy( dl->color, color );
		color[3] = 1;
		LF_SetLight( &lf_entity, view, dl->origin, qfalse, color );
		LF_DrawLight( &lf_entity, view );
	}

	LF_DrawFullscreen( &lf_sun, view );
	LF_DrawFullscreen( &lf_entity, view );
	LF_DrawFullscreen( &lf_dlight, view );
	RE_SetColor( NULL );
}
