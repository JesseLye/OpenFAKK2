/*
===========================================================================
FAKK2's inventory bar.

The retail client keeps the weapon selection on its side: global/
inventory.scr defines the inventory's slots ("typedef swords { ... }") and
the items in each, with the hand each item goes in and the pictures and
model the bar draws. default.cfg binds the number keys to "warpinv <slot>",
the mouse wheel to "nextinv"/"previnv" and "i" to "inventory"; the bar
shows the slot's items the player has (the player state's
inventory_name_index names them), and while it is up the attack keys equip
the highlighted item by sending the game "use "<item>" lefthand|righthand|
dualhand" (0x409e50/0x40a090), or a bare "use "<item>"" for an item that is
used rather than wielded. The bar goes away five seconds after its last
change (0x408ac0). "inventory" opens the whole inventory as a screen.

Read from the retail client's command handler (0x408dc0) and the helpers
it calls. Each item's frame shows its model turning ("move spin") or
rocking ("move bob") as the script asks, framed by the model's bounds;
the script's "offset" is not used. The HUD's "invmodelrender" widgets
(weaphud.urc and dualweaphud.urc) show the wielded weapons the same way,
with the script's "hud" settings.
===========================================================================
*/

#include "client.h"
#include "../tiki/tiki.h"

#define INV_FILE		"global/inventory.scr"
#define INV_MAX_TYPES	8
#define INV_MAX_ITEMS	16
#define INV_SHOW_TIME	5000
#define INV_FONT		"verdana-12"
#define INV_MODEL_FOV	30

// the game's item and ammo name config strings (bg_public.h)
#define INV_CS_ITEMS	( 32 + MAX_MODELS + MAX_SOUNDS + MAX_IMAGES + MAX_LIGHTSTYLES + MAX_CLIENTS )
#define INV_CS_AMMO		( INV_CS_ITEMS + MAX_ITEMS )

#define INV_EQUIP_LEFT	1
#define INV_EQUIP_RIGHT	2
#define INV_EQUIP_DUAL	4
#define INV_EQUIP_USE	8

typedef enum {
	INV_MOVE_NONE,
	INV_MOVE_SPIN,
	INV_MOVE_BOB
} invMove_t;

typedef struct {
	char		model[64];
	char		anim[32];
	invMove_t	move;
	vec3_t		angles;
	vec3_t		angleDeltas;
	vec4_t		window;			// x y w h, shares of the frame
} invView_t;

typedef struct {
	char		name[64];
	char		ammo[32];
	int			equip;
	char		command[128];		// sent instead of "use" when set
	qhandle_t	bgShader;
	qhandle_t	selShader;
	qboolean	selOnTop;
	qhandle_t	barShader;
	int			barOffsetY, barWidth, barHeight;
	qboolean	checkAmmo;
	invView_t	view;				// in the bar
	invView_t	hud;				// in the HUD's windows
} invItem_t;

typedef struct {
	char		name[32];
	qhandle_t	buttonShader;
	qhandle_t	hoverShader;
	qhandle_t	selShader;
	int			width, height;
	int			numItems;
	invItem_t	items[INV_MAX_ITEMS];
} invType_t;

static struct {
	int			vertOffset, horizOffset;
	int			width, height;
	qboolean	alignRight;
	qboolean	cascadeLeft;
	char		selectSound[64];
	char		rejectSound[64];
	char		changeSound[64];
} inv;

static invType_t	inv_types[INV_MAX_TYPES];
static int			inv_numTypes;
static qboolean		inv_loaded;

static int			inv_curType = -1;
static int			inv_curItem = -1;
static int			inv_showTime;
static qboolean		inv_fullScreen;

/*
=================
INV_Load
=================
*/
static void INV_ParseVector( char **p, vec3_t v ) {
	// the script quotes its vectors: "0 30 0"
	sscanf( COM_ParseExt( p, qfalse ), "%f %f %f", &v[0], &v[1], &v[2] );
}

static void INV_InitView( invView_t *v ) {
	Com_Memset( v, 0, sizeof( *v ) );
	v->window[2] = v->window[3] = 1;
}

static void INV_Load( void ) {
	char		*buf, *p, *tok;
	invType_t	*type = NULL;
	invItem_t	*item = NULL;

	Com_Memset( &inv, 0, sizeof( inv ) );
	Com_Memset( inv_types, 0, sizeof( inv_types ) );
	inv_numTypes = 0;
	inv_loaded = qtrue;
	inv.width = inv.height = 64;
	inv.alignRight = qtrue;
	inv.cascadeLeft = qtrue;

	if ( FS_ReadFile( INV_FILE, (void **)&buf ) <= 0 ) {
		Com_Printf( "WARNING: could not load %s\n", INV_FILE );
		return;
	}
	p = buf;
	while ( 1 ) {
		tok = COM_Parse( &p );
		if ( !tok[0] ) {
			break;
		}
		if ( !Q_stricmp( tok, "typedef" ) ) {
			if ( inv_numTypes >= INV_MAX_TYPES ) {
				Com_Printf( "WARNING: %s: too many slots\n", INV_FILE );
				break;
			}
			type = &inv_types[inv_numTypes++];
			item = NULL;
			Q_strncpyz( type->name, COM_Parse( &p ), sizeof( type->name ) );
			type->width = inv.width;
			type->height = inv.height;
		} else if ( !Q_stricmp( tok, "}" ) ) {
			type = NULL;
			item = NULL;
		} else if ( !Q_stricmp( tok, "{" ) ) {
		} else if ( !Q_stricmp( tok, "inv_vert_offset" ) ) {
			inv.vertOffset = atoi( COM_ParseExt( &p, qfalse ) );
		} else if ( !Q_stricmp( tok, "inv_horiz_offset" ) ) {
			inv.horizOffset = atoi( COM_ParseExt( &p, qfalse ) );
		} else if ( !Q_stricmp( tok, "inv_height" ) ) {
			inv.height = atoi( COM_ParseExt( &p, qfalse ) );
		} else if ( !Q_stricmp( tok, "inv_width" ) ) {
			inv.width = atoi( COM_ParseExt( &p, qfalse ) );
		} else if ( !Q_stricmp( tok, "inv_align" ) ) {
			inv.alignRight = Q_stricmp( COM_ParseExt( &p, qfalse ), "left" ) != 0;
		} else if ( !Q_stricmp( tok, "inv_cascade" ) ) {
			inv.cascadeLeft = Q_stricmp( COM_ParseExt( &p, qfalse ), "right" ) != 0;
		} else if ( !Q_stricmp( tok, "inv_selectsound" ) ) {
			Q_strncpyz( inv.selectSound, COM_ParseExt( &p, qfalse ), sizeof( inv.selectSound ) );
		} else if ( !Q_stricmp( tok, "inv_rejectsound" ) ) {
			Q_strncpyz( inv.rejectSound, COM_ParseExt( &p, qfalse ), sizeof( inv.rejectSound ) );
		} else if ( !Q_stricmp( tok, "inv_changesound" ) ) {
			Q_strncpyz( inv.changeSound, COM_ParseExt( &p, qfalse ), sizeof( inv.changeSound ) );
		} else if ( !type ) {
			SkipRestOfLine( &p );
		} else if ( !Q_stricmp( tok, "button_shader" ) ) {
			type->buttonShader = re.RegisterShaderNoMip( COM_ParseExt( &p, qfalse ) );
		} else if ( !Q_stricmp( tok, "hover_shader" ) ) {
			type->hoverShader = re.RegisterShaderNoMip( COM_ParseExt( &p, qfalse ) );
		} else if ( !Q_stricmp( tok, "sel_shader" ) ) {
			type->selShader = re.RegisterShaderNoMip( COM_ParseExt( &p, qfalse ) );
		} else if ( !Q_stricmp( tok, "width" ) ) {
			type->width = atoi( COM_ParseExt( &p, qfalse ) );
		} else if ( !Q_stricmp( tok, "height" ) ) {
			type->height = atoi( COM_ParseExt( &p, qfalse ) );
		} else if ( !Q_stricmp( tok, "item" ) ) {
			if ( type->numItems >= INV_MAX_ITEMS ) {
				Com_Printf( "WARNING: %s: too many items in %s\n", INV_FILE, type->name );
				SkipRestOfLine( &p );
				item = NULL;
				continue;
			}
			item = &type->items[type->numItems++];
			Q_strncpyz( item->name, COM_ParseExt( &p, qfalse ), sizeof( item->name ) );
			INV_InitView( &item->view );
			INV_InitView( &item->hud );
			item->barWidth = item->barHeight = 27;
		} else if ( !item ) {
			SkipRestOfLine( &p );
		} else if ( !Q_stricmp( tok, "ammo" ) ) {
			Q_strncpyz( item->ammo, COM_ParseExt( &p, qfalse ), sizeof( item->ammo ) );
		} else if ( !Q_stricmp( tok, "checkammo" ) ) {
			item->checkAmmo = atoi( COM_ParseExt( &p, qfalse ) ) != 0;
		} else if ( !Q_stricmp( tok, "equip" ) ) {
			while ( 1 ) {
				tok = COM_ParseExt( &p, qfalse );
				if ( !tok[0] ) break;
				if ( !Q_stricmp( tok, "left" ) ) item->equip |= INV_EQUIP_LEFT;
				else if ( !Q_stricmp( tok, "right" ) ) item->equip |= INV_EQUIP_RIGHT;
				else if ( !Q_stricmp( tok, "dual" ) ) item->equip |= INV_EQUIP_DUAL;
				else if ( !Q_stricmp( tok, "use" ) ) item->equip |= INV_EQUIP_USE;
			}
		} else if ( !Q_stricmp( tok, "command" ) ) {
			Q_strncpyz( item->command, COM_ParseExt( &p, qfalse ), sizeof( item->command ) );
		} else if ( !Q_stricmp( tok, "bgshader" ) ) {
			item->bgShader = re.RegisterShaderNoMip( COM_ParseExt( &p, qfalse ) );
		} else if ( !Q_stricmp( tok, "selitem_shader" ) ) {
			item->selShader = re.RegisterShaderNoMip( COM_ParseExt( &p, qfalse ) );
		} else if ( !Q_stricmp( tok, "selitem_shaderontop" ) ) {
			item->selOnTop = qtrue;
		} else if ( !Q_stricmp( tok, "barshader" ) ) {
			item->barShader = re.RegisterShaderNoMip( COM_ParseExt( &p, qfalse ) );
		} else if ( !Q_stricmp( tok, "baroffsety" ) ) {
			item->barOffsetY = atoi( COM_ParseExt( &p, qfalse ) );
		} else if ( !Q_stricmp( tok, "barwidth" ) ) {
			item->barWidth = atoi( COM_ParseExt( &p, qfalse ) );
		} else if ( !Q_stricmp( tok, "barheight" ) ) {
			item->barHeight = atoi( COM_ParseExt( &p, qfalse ) );
		} else if ( !Q_stricmp( tok, "model" ) ) {
			Q_strncpyz( item->view.model, COM_ParseExt( &p, qfalse ), sizeof( item->view.model ) );
			if ( !item->hud.model[0] ) {
				Q_strncpyz( item->hud.model, item->view.model, sizeof( item->hud.model ) );
			}
		} else if ( !Q_stricmp( tok, "hudmodel" ) ) {
			Q_strncpyz( item->hud.model, COM_ParseExt( &p, qfalse ), sizeof( item->hud.model ) );
		} else if ( !Q_stricmp( tok, "anim" ) ) {
			Q_strncpyz( item->view.anim, COM_ParseExt( &p, qfalse ), sizeof( item->view.anim ) );
			Q_strncpyz( item->hud.anim, item->view.anim, sizeof( item->hud.anim ) );
		} else if ( !Q_stricmp( tok, "move" ) || !Q_stricmp( tok, "hudmove" ) ) {
			invView_t	*v = tok[0] == 'h' || tok[0] == 'H' ? &item->hud : &item->view;

			tok = COM_ParseExt( &p, qfalse );
			v->move = !Q_stricmp( tok, "spin" ) ? INV_MOVE_SPIN : !Q_stricmp( tok, "bob" ) ? INV_MOVE_BOB : INV_MOVE_NONE;
		} else if ( !Q_stricmp( tok, "angles" ) ) {
			INV_ParseVector( &p, item->view.angles );
		} else if ( !Q_stricmp( tok, "hudangles" ) ) {
			INV_ParseVector( &p, item->hud.angles );
		} else if ( !Q_stricmp( tok, "angledeltas" ) ) {
			INV_ParseVector( &p, item->view.angleDeltas );
		} else if ( !Q_stricmp( tok, "hudangledeltas" ) ) {
			INV_ParseVector( &p, item->hud.angleDeltas );
		} else if ( !Q_stricmp( tok, "modelwindow" ) ) {
			int		i;

			for ( i = 0 ; i < 4 ; i++ ) {
				item->view.window[i] = atof( COM_ParseExt( &p, qfalse ) );
			}
		} else {
			// offset, rotateoffset and their hud forms: the retail bar's own
			// placing, not used; the model is framed by its bounds here
			SkipRestOfLine( &p );
		}
	}
	FS_FreeFile( buf );
}

/*
=================
INV_ConfigString
=================
*/
static const char *INV_ConfigString( int index ) {
	if ( index < 0 || index >= MAX_CONFIGSTRINGS ) {
		return "";
	}
	return cl.gameState.stringData + cl.gameState.stringOffsets[index];
}

/*
=================
INV_Owned

Whether the player carries the item: the player state names the inventory
by item config strings.
=================
*/
static qboolean INV_Owned( const invItem_t *item ) {
	int		i, idx;

	if ( clc.state != CA_ACTIVE || !cl.snap.valid ) {
		return qfalse;
	}
	for ( i = 0 ; i < MAX_INVENTORY ; i++ ) {
		idx = cl.snap.ps.inventory_name_index[i];
		if ( idx <= 0 ) {
			continue;
		}
		if ( !Q_stricmp( INV_ConfigString( INV_CS_ITEMS + idx ), item->name ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

// the item of a name in the script, or NULL
static const invItem_t *INV_FindItem( const char *name, int *typeOut ) {
	int		t, i;

	for ( t = 0 ; t < inv_numTypes ; t++ ) {
		for ( i = 0 ; i < inv_types[t].numItems ; i++ ) {
			if ( !Q_stricmp( inv_types[t].items[i].name, name ) ) {
				if ( typeOut ) {
					*typeOut = t;
				}
				return &inv_types[t].items[i];
			}
		}
	}
	return NULL;
}

/*
=================
INV_Ammo

The player's count and limit of the item's ammo; false without any.
=================
*/
static qboolean INV_Ammo( const invItem_t *item, int *amount, int *max ) {
	int		i, idx;

	if ( !item->ammo[0] || clc.state != CA_ACTIVE || !cl.snap.valid ) {
		return qfalse;
	}
	for ( i = 0 ; i < MAX_AMMO ; i++ ) {
		idx = cl.snap.ps.ammo_name_index[i];
		if ( idx <= 0 ) {
			continue;
		}
		if ( !Q_stricmp( INV_ConfigString( INV_CS_AMMO + idx ), item->ammo ) ) {
			*amount = cl.snap.ps.ammo_amount[i];
			*max = cl.snap.ps.max_ammo_amount[i];
			return qtrue;
		}
	}
	return qfalse;
}

static qboolean INV_Open( void ) {
	return inv_fullScreen || ( inv_curType >= 0 && cls.realtime - inv_showTime < INV_SHOW_TIME && clc.state == CA_ACTIVE );
}

static void INV_Close( void ) {
	inv_showTime = 0;
	inv_fullScreen = qfalse;
}

// the first (dir 1) or last (dir -1) owned item of a slot, or -1
static int INV_FirstOwned( const invType_t *type, int dir ) {
	int		i;

	for ( i = dir > 0 ? 0 : type->numItems - 1 ; i >= 0 && i < type->numItems ; i += dir ) {
		if ( INV_Owned( &type->items[i] ) ) {
			return i;
		}
	}
	return -1;
}

/*
=================
INV_SelectType

Retail 0x408cf0: show the slot with its first item.
=================
*/
static void INV_SelectType( int t ) {
	inv_curType = t;
	inv_curItem = INV_FirstOwned( &inv_types[t], 1 );
	inv_showTime = cls.realtime;
	URC_PlayAliasSound( inv.changeSound );
}

/*
=================
INV_Step

Retail 0x408670: the next or previous owned item, running on into the
next or previous slot.
=================
*/
static void INV_Step( int dir ) {
	int		t, i, tries;

	if ( !inv_numTypes ) {
		return;
	}
	if ( !INV_Open() ) {
		t = inv_curType >= 0 ? inv_curType : 0;
		i = INV_FirstOwned( &inv_types[t], 1 );
		inv_curType = t;
		inv_curItem = i;
		if ( i >= 0 ) {
			inv_showTime = cls.realtime;
			URC_PlayAliasSound( inv.changeSound );
			return;
		}
	}
	t = inv_curType;
	for ( i = inv_curItem + dir ; i >= 0 && i < inv_types[t].numItems ; i += dir ) {
		if ( INV_Owned( &inv_types[t].items[i] ) ) {
			inv_curItem = i;
			inv_showTime = cls.realtime;
			URC_PlayAliasSound( inv.changeSound );
			return;
		}
	}
	for ( tries = 0 ; tries < inv_numTypes ; tries++ ) {
		t = ( t + dir + inv_numTypes ) % inv_numTypes;
		i = INV_FirstOwned( &inv_types[t], dir );
		if ( i >= 0 ) {
			inv_curType = t;
			inv_curItem = i;
			inv_showTime = cls.realtime;
			URC_PlayAliasSound( inv.changeSound );
			return;
		}
	}
	inv_showTime = cls.realtime;
	URC_PlayAliasSound( inv.rejectSound );
}

/*
=================
INV_Equip

Retail 0x409e50 (left) and 0x40a090 (right): the highlighted item to the
hand, when it goes there.
=================
*/
static void INV_Equip( int hand ) {
	const invItem_t	*item;
	int				amount, max;

	if ( !INV_Open() || inv_curType < 0 || inv_curItem < 0 ) {
		return;
	}
	item = &inv_types[inv_curType].items[inv_curItem];
	if ( item->checkAmmo && INV_Ammo( item, &amount, &max ) && amount <= 0 ) {
		URC_PlayAliasSound( inv.rejectSound );
		inv_showTime = cls.realtime;
		return;
	}
	if ( item->command[0] ) {
		CL_AddReliableCommand( item->command, qfalse );
	} else if ( item->equip & INV_EQUIP_DUAL ) {
		CL_AddReliableCommand( va( "use \"%s\" dualhand", item->name ), qfalse );
	} else if ( item->equip & hand ) {
		CL_AddReliableCommand( va( "use \"%s\" %s", item->name, hand == INV_EQUIP_LEFT ? "lefthand" : "righthand" ), qfalse );
	} else if ( item->equip & INV_EQUIP_USE ) {
		CL_AddReliableCommand( va( "use \"%s\"", item->name ), qfalse );
	} else {
		URC_PlayAliasSound( inv.rejectSound );
		inv_showTime = cls.realtime;
		return;
	}
	URC_PlayAliasSound( inv.selectSound );
	INV_Close();
}

/*
=================
INV_KeyEvent

Retail 0x408dc0: the keys bound to the attack commands take the item while
the bar is up; the arrows and the wheel's keys move through it. Escape
closes the inventory screen.
=================
*/
qboolean INV_KeyEvent( int key ) {
	const char	*bind;

	if ( !INV_Open() || key < 0 || key >= MAX_KEYS ) {
		return qfalse;
	}
	if ( key == K_ESCAPE ) {
		if ( inv_fullScreen ) {
			INV_Close();
			return qtrue;
		}
		return qfalse;
	}
	if ( inv_fullScreen ) {
		if ( key == K_RIGHTARROW || key == K_DOWNARROW ) {
			INV_Step( 1 );
			return qtrue;
		}
		if ( key == K_LEFTARROW || key == K_UPARROW ) {
			INV_Step( -1 );
			return qtrue;
		}
	}
	bind = Key_GetBinding( key );
	if ( !bind || !bind[0] ) {
		return qfalse;
	}
	if ( !Q_stricmp( bind, "+attackleft" ) ) {
		INV_Equip( INV_EQUIP_LEFT );
		return qtrue;
	}
	if ( !Q_stricmp( bind, "+attackright" ) ) {
		INV_Equip( INV_EQUIP_RIGHT );
		return qtrue;
	}
	return qfalse;
}

/*
=================
Commands
=================
*/
static void INV_WarpInv_f( void ) {
	int		t;

	if ( Cmd_Argc() < 2 ) {
		Com_Printf( "Usage: warpinv <slot_name>\n" );
		return;
	}
	if ( clc.state != CA_ACTIVE ) {
		return;
	}
	for ( t = 0 ; t < inv_numTypes ; t++ ) {
		if ( !Q_stricmp( inv_types[t].name, Cmd_Argv( 1 ) ) ) {
			INV_SelectType( t );
			return;
		}
	}
	Com_Printf( "warpinv: no inventory slot '%s'\n", Cmd_Argv( 1 ) );
}

static void INV_NextInv_f( void ) {
	if ( clc.state == CA_ACTIVE ) {
		INV_Step( 1 );
	}
}

static void INV_PrevInv_f( void ) {
	if ( clc.state == CA_ACTIVE ) {
		INV_Step( -1 );
	}
}

static void INV_Inventory_f( void ) {
	if ( clc.state != CA_ACTIVE ) {
		return;
	}
	if ( inv_fullScreen ) {
		INV_Close();
		return;
	}
	inv_fullScreen = qtrue;
	if ( inv_curType < 0 || inv_curItem < 0 ) {
		int		t;

		inv_curType = -1;
		inv_curItem = -1;
		for ( t = 0 ; t < inv_numTypes ; t++ ) {
			int		i = INV_FirstOwned( &inv_types[t], 1 );

			if ( i >= 0 ) {
				inv_curType = t;
				inv_curItem = i;
				break;
			}
		}
		if ( inv_curType < 0 ) {
			inv_curType = 0;
		}
	}
	inv_showTime = cls.realtime;
}

/*
=================
INV_DrawModel

The item's model in a window of the frame, framed by its bounds, turned
by the script's angles and moved as it asks.
=================
*/
static void INV_DrawModel( const invView_t *v, float x, float y, float w, float h, float alpha ) {
	refdef_t	rd;
	refEntity_t	ent;
	tiki_t		*tiki = NULL;
	qhandle_t	handle;
	vec3_t		mins, maxs, center, angles, target;
	float		radius, dist, fx, fy, fw, fh, t;
	int			i, anim = 0;

	if ( !v->model[0] ) {
		return;
	}
	handle = CL_RegisterTikiModel( v->model, &tiki );
	if ( !handle ) {
		return;
	}
	if ( tiki ) {
		anim = TIKI_Anim_NumForName( tiki, v->anim[0] ? v->anim : "idle" );
		if ( anim < 0 ) {
			anim = 0;
		}
		TIKI_CalculateBounds( tiki, 1.0f, mins, maxs );
	} else {
		re.ModelBounds( handle, mins, maxs );
	}
	VectorAdd( mins, maxs, center );
	VectorScale( center, 0.5f, center );
	VectorSubtract( maxs, mins, target );
	// the longest extent fills the window
	radius = target[0];
	if ( target[1] > radius ) radius = target[1];
	if ( target[2] > radius ) radius = target[2];
	radius *= 0.5f;
	if ( radius < 1 ) {
		radius = 1;
	}

	fx = x + v->window[0] * w;
	fy = y + v->window[1] * h;
	fw = v->window[2] * w;
	fh = v->window[3] * h;
	SCR_AdjustFrom640( &fx, &fy, &fw, &fh );
	if ( fw < 1 || fh < 1 ) {
		return;
	}

	Com_Memset( &rd, 0, sizeof( rd ) );
	rd.x = fx;
	rd.y = fy;
	rd.width = fw;
	rd.height = fh;
	rd.fov_x = INV_MODEL_FOV;
	rd.fov_y = atan2( fh, fw / tan( rd.fov_x / 360 * M_PI ) ) * 360 / M_PI;
	rd.time = cls.realtime;
	rd.rdflags = RDF_NOWORLDMODEL;
	// the script places its models at negative x, so the retail window
	// looks down -x; the same here, so its angles read the same way
	VectorSet( angles, 0, 180, 0 );
	AnglesToAxis( angles, rd.viewaxis );

	// the model fills the narrower of the window's angles
	dist = radius / tan( DEG2RAD( ( rd.fov_y < rd.fov_x ? rd.fov_y : rd.fov_x ) * 0.5f ) ) * 1.15f;

	// the model's thinnest side faces the window (a sword's flat, a
	// shield's face); the script's angles are for the retail camera, whose
	// placing is not known, so only its motion is kept: a spin turns about
	// the vertical, a bob rocks by the deltas
	t = cls.realtime * 0.001f;
	// the longest extent lies across the window (a sword's blade, a gun's
	// barrel), seen from a little above and aside so no side is edge-on
	if ( target[0] >= target[1] && target[0] >= target[2] ) {
		VectorSet( angles, 20, 90 + 30, 0 );	// x across
	} else if ( target[1] >= target[2] ) {
		VectorSet( angles, 20, 30, 0 );			// y across
	} else {
		VectorSet( angles, 20, 30, 90 );		// z across
	}
	if ( v->move == INV_MOVE_SPIN ) {
		angles[YAW] += t * ( v->angleDeltas[YAW] ? v->angleDeltas[YAW] : 30 );
	} else if ( v->move == INV_MOVE_BOB ) {
		VectorMA( angles, sin( t * 2 ), v->angleDeltas, angles );
	}

	Com_Memset( &ent, 0, sizeof( ent ) );
	ent.reType = RT_MODEL;
	ent.hModel = handle;
	ent.anim = ent.oldanim = anim;
	AnglesToAxis( angles, ent.axis );
	VectorSet( target, -dist, 0, 0 );
	VectorCopy( target, ent.origin );
	for ( i = 0 ; i < 3 ; i++ ) {
		VectorMA( ent.origin, -center[i], ent.axis[i], ent.origin );
	}
	VectorCopy( ent.origin, ent.oldorigin );
	VectorCopy( ent.origin, ent.lightingOrigin );
	ent.renderfx = RF_NOSHADOW;
	ent.shaderRGBA[0] = ent.shaderRGBA[1] = ent.shaderRGBA[2] = 255;
	ent.shaderRGBA[3] = alpha * 255;
	ent.scale = 1;

	re.ClearScene();
	re.AddRefEntityToScene( &ent );
	re.RenderScene( &rd );
}

/*
=================
INV_DrawAmmoBar

The item's ammo as the script's bar at the frame's foot: its fill rises
with the count, the count beside it.
=================
*/
static void INV_DrawAmmoBar( const invItem_t *item, float x, float y, float w, float h ) {
	int		amount, max;
	float	frac, bx, by, bh;
	vec4_t	white = { 1, 1, 1, 1 };

	if ( !item->barShader || !INV_Ammo( item, &amount, &max ) ) {
		return;
	}
	frac = max > 0 ? (float)amount / max : 0;
	if ( frac > 1 ) frac = 1;
	if ( frac < 0 ) frac = 0;
	bx = x + w - item->barWidth - 2;
	by = y + h - item->barHeight + item->barOffsetY;
	bh = item->barHeight * frac;
	if ( bh > 0 ) {
		// the gradient's top at the fill's top
		URC_DrawPic( bx, by + item->barHeight - bh, item->barWidth, bh, item->barShader, white );
	}
	URC_DrawText( INV_FONT, x + 2, y + h - 14, va( "%d", amount ), white );
}

/*
=================
INV_DrawFrame

One item's frame: its background, its model, its ammo, the highlight.
=================
*/
static void INV_DrawFrame( const invItem_t *item, float x, float y, float w, float h, qboolean selected ) {
	vec4_t	white = { 1, 1, 1, 1 };

	if ( selected && item->selShader && !item->selOnTop ) {
		URC_DrawPic( x, y, w, h, item->selShader, white );
	}
	URC_DrawPic( x, y, w, h, item->bgShader, white );
	INV_DrawModel( &item->view, x, y, w, h, 1 );
	INV_DrawAmmoBar( item, x, y, w, h );
	if ( selected && item->selShader && item->selOnTop ) {
		URC_DrawPic( x, y, w, h, item->selShader, white );
	}
}

/*
=================
INV_DrawHudModel

A HUD widget's "invmodelrender <n>": the item the player state's
activeItems[n] names (2 the left hand's weapon, 3 the right's, 4 the
two-handed one), with the script's hud settings.
=================
*/
void INV_DrawHudModel( int slot, float x, float y, float w, float h, float alpha ) {
	const invItem_t	*item;
	int				idx;

	if ( slot < 0 || slot >= MAX_ACTIVE_ITEMS || clc.state != CA_ACTIVE || !cl.snap.valid ) {
		return;
	}
	idx = cl.snap.ps.activeItems[slot];
	if ( idx <= 0 ) {
		return;
	}
	item = INV_FindItem( INV_ConfigString( INV_CS_ITEMS + idx ), NULL );
	if ( !item ) {
		return;
	}
	INV_DrawModel( &item->hud, x, y, w, h, alpha );
}

/*
=================
INV_DrawScreen

The whole inventory: a row for each slot, its button at the left and the
items the player has beside it.
=================
*/
static void INV_DrawScreen( void ) {
	vec4_t	back = { 0, 0, 0, 0.6f };
	vec4_t	white = { 1, 1, 1, 1 };
	vec4_t	dim = { 1, 1, 1, 0.5f };
	float	rowH, y, x;
	int		t, i;

	SCR_FillRect( 0, 0, 640, 480, back );
	rowH = ( 480 - 40 ) / (float)( inv_numTypes > 0 ? inv_numTypes : 1 );
	if ( rowH > 72 ) {
		rowH = 72;
	}
	for ( t = 0 ; t < inv_numTypes ; t++ ) {
		const invType_t	*type = &inv_types[t];
		float			fw = type->width, fh = type->height, s = 1;

		if ( fh > rowH - 8 ) {
			s = ( rowH - 8 ) / fh;
			fw *= s;
			fh *= s;
		}
		y = 20 + t * rowH;
		x = 24;
		URC_DrawPic( x, y, fw, fh, t == inv_curType ? type->selShader : type->buttonShader, t == inv_curType ? white : dim );
		x += fw + 16;
		for ( i = 0 ; i < type->numItems ; i++ ) {
			const invItem_t	*item = &type->items[i];

			if ( !INV_Owned( item ) ) {
				continue;
			}
			INV_DrawFrame( item, x, y, fw, fh, t == inv_curType && i == inv_curItem );
			if ( t == inv_curType && i == inv_curItem ) {
				URC_DrawText( INV_FONT, x, y + fh + 1, item->name, white );
			}
			x += fw + 8;
		}
	}
}

/*
=================
INV_Draw

The slot's buttons down the right edge, the slot's owned items cascading
left from the current one, the highlighted item's name beneath.
=================
*/
void INV_Draw( void ) {
	const invType_t	*type;
	float			x, y, bx, w, h;
	int				t, i, n;
	vec4_t			white = { 1, 1, 1, 1 };
	vec4_t			dim = { 1, 1, 1, 0.6f };

	if ( !INV_Open() || !inv_numTypes ) {
		return;
	}
	if ( inv_fullScreen ) {
		INV_DrawScreen();
		return;
	}
	w = inv.width;
	h = inv.height;
	bx = inv.alignRight ? 640 - w - inv.horizOffset : inv.horizOffset;
	y = inv.vertOffset;

	// the slots
	for ( t = 0 ; t < inv_numTypes ; t++ ) {
		type = &inv_types[t];
		URC_DrawPic( bx, y + t * h, w, h, t == inv_curType ? type->selShader : type->buttonShader, t == inv_curType ? white : dim );
	}

	// the current slot's items
	type = &inv_types[inv_curType];
	x = bx;
	y = inv.vertOffset + inv_curType * h;
	for ( i = 0, n = 0 ; i < type->numItems ; i++ ) {
		const invItem_t	*item = &type->items[i];
		float			ix;

		if ( !INV_Owned( item ) ) {
			continue;
		}
		n++;
		ix = inv.cascadeLeft ? x - n * type->width : x + n * type->width;
		INV_DrawFrame( item, ix, y, type->width, type->height, i == inv_curItem );
		if ( i == inv_curItem ) {
			float	tw = URC_TextWidth( INV_FONT, item->name );

			URC_DrawText( INV_FONT, ix + ( type->width - tw ) * 0.5f, y + type->height + 2, item->name, white );
		}
	}
}

/*
=================
INV_Init / INV_Shutdown

With the UI, on every map.
=================
*/
void INV_Init( void ) {
	if ( !inv_loaded ) {
		INV_Load();
	}
	inv_curType = -1;
	inv_curItem = -1;
	inv_showTime = 0;
	inv_fullScreen = qfalse;
	Cmd_AddCommand( "warpinv", INV_WarpInv_f );
	Cmd_AddCommand( "nextinv", INV_NextInv_f );
	Cmd_AddCommand( "previnv", INV_PrevInv_f );
	Cmd_AddCommand( "inventory", INV_Inventory_f );
}

void INV_Shutdown( void ) {
	Cmd_RemoveCommand( "warpinv" );
	Cmd_RemoveCommand( "nextinv" );
	Cmd_RemoveCommand( "previnv" );
	Cmd_RemoveCommand( "inventory" );
	inv_loaded = qfalse;		// its shaders are the map's
}
