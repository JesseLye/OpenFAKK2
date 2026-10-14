/*
===========================================================================
FAKK2's inventory bar.

The retail client keeps the weapon selection on its side: global/
inventory.scr defines the inventory's slots ("typedef swords { ... }") and
the items in each, with the hand each item goes in and the pictures the
bar draws. default.cfg binds the number keys to "warpinv <slot>", the
mouse wheel to "nextinv"/"previnv"; the bar shows the slot's items the
player has (the player state's inventory_name_index names them), and
while it is up the attack keys equip the highlighted item by sending the
game "use "<item>" lefthand|righthand|dualhand" (0x409e50/0x40a090), or a
bare "use "<item>"" for an item that is used rather than wielded. The bar
goes away five seconds after its last change (0x408ac0).

Read from the retail client's command handler (0x408dc0) and the helpers
it calls; the bar's look is a plain rendering of the script's shaders
without the item models the retail bar turns in its windows.
===========================================================================
*/

#include "client.h"

#define INV_FILE		"global/inventory.scr"
#define INV_MAX_TYPES	8
#define INV_MAX_ITEMS	16
#define INV_SHOW_TIME	5000
#define INV_FONT		"verdana-12"

// the game's item name config strings start after the players' (bg_public.h)
#define INV_CS_ITEMS	( 32 + MAX_MODELS + MAX_SOUNDS + MAX_IMAGES + MAX_LIGHTSTYLES + MAX_CLIENTS )

#define INV_EQUIP_LEFT	1
#define INV_EQUIP_RIGHT	2
#define INV_EQUIP_DUAL	4
#define INV_EQUIP_USE	8

typedef struct {
	char		name[64];
	char		ammo[32];
	int			equip;
	char		command[128];		// sent instead of "use" when set
	qhandle_t	bgShader;
	qhandle_t	selShader;
	qboolean	selOnTop;
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

/*
=================
INV_Load
=================
*/
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
		} else if ( !item ) {
			SkipRestOfLine( &p );
		} else if ( !Q_stricmp( tok, "ammo" ) ) {
			Q_strncpyz( item->ammo, COM_ParseExt( &p, qfalse ), sizeof( item->ammo ) );
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
		} else {
			// model, move, angles, offsets, windows, ammo bars: the retail bar's
			// turning models, not drawn here
			SkipRestOfLine( &p );
		}
	}
	FS_FreeFile( buf );
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
		if ( idx <= 0 || INV_CS_ITEMS + idx >= MAX_CONFIGSTRINGS ) {
			continue;
		}
		if ( !Q_stricmp( cl.gameState.stringData + cl.gameState.stringOffsets[INV_CS_ITEMS + idx], item->name ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

static qboolean INV_Open( void ) {
	return inv_curType >= 0 && cls.realtime - inv_showTime < INV_SHOW_TIME && clc.state == CA_ACTIVE;
}

static void INV_Close( void ) {
	inv_showTime = 0;
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
		if ( i < 0 ) {
			// the slot is empty: start from the next slot that has something
			inv_curType = t;
			inv_curItem = -1;
		} else {
			inv_curType = t;
			inv_curItem = i;
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

	if ( !INV_Open() || inv_curItem < 0 ) {
		return;
	}
	item = &inv_types[inv_curType].items[inv_curItem];
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
the bar is up.
=================
*/
qboolean INV_KeyEvent( int key ) {
	const char	*bind;

	if ( !INV_Open() || key < 0 || key >= MAX_KEYS ) {
		return qfalse;
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
	if ( INV_Open() ) {
		INV_Close();
	} else {
		INV_Step( 1 );
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
		if ( i == inv_curItem && item->selShader && !item->selOnTop ) {
			URC_DrawPic( ix, y, type->width, type->height, item->selShader, white );
		}
		URC_DrawPic( ix, y, type->width, type->height, item->bgShader, white );
		if ( i == inv_curItem && item->selShader && item->selOnTop ) {
			URC_DrawPic( ix, y, type->width, type->height, item->selShader, white );
		}
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
