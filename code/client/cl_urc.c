/*
===========================================================================
FAKK2's user interface: menus and HUDs described by ui/*.urc files.

The retail engine draws its menus, HUD and on-screen prints itself, from
"resource" files the SDK never shipped code for. Its UI_Init (0x40e9d0)
loads every ui/*.urc, registers the console commands below, and looks up
the four HUD menus by name (weaphud, dualweaphud, healthhud, bosshud);
its per-frame update (0x40af50) hides them all when ui_hud is off, no map
is active, a menu is up or the cinematic flag is set, shows weaphud when
the player's dual-weapon item slot is empty (activeItems[4] < 0) and
dualweaphud otherwise, and bosshud when the boss-health stat is at least
1. Centre and location prints arrive as their own network messages
(svc_centerprint: string; svc_locprint: short x, short y, string), are
drawn with the facfont-20 font for 4 seconds (0x418cc0), and a text that
starts with '@' names a shader to draw instead. While a map loads (from
CL_MapLoading, 0x4207f0, to the first active snapshot, 0x41d520) the update
fills the screen black and draws textures/menu/loading in the middle.

This is a fresh implementation of that: the file grammar is what the 31
shipped files use, the widget classes are drawn as pictures and text, and
the fonts are the game's fonts/*.ritualfont (a text file: height, aspect,
256 character indirections, and a glyph rectangle list in texture pixels
over a 256-wide image).
===========================================================================
*/

#include "client.h"

// the SDK's bg_public.h values the HUD menus read (code/fakk/fgame)
#define URC_STAT_BOSSHEALTH		13
#define URC_STAT_CINEMATIC		14
#define URC_STAT_ADDFADE		15			// ps.blend is added rather than blended
#define URC_STAT_LETTERBOX		16			// letterbox fraction * URC_MAX_LETTERBOX_SIZE
#define URC_MAX_LETTERBOX_SIZE	0x7fff
#define URC_ITEM_WEAPON_DUAL	4
#define URC_ADDFADE_SHADER		"textures/jack/whitefade"	// scripts/sunflare.shader: GL_SRC_ALPHA GL_ONE, fromClient

#define URC_MAX_MENUS		64
#define URC_MAX_WIDGETS		1024
#define URC_MAX_FONTS		16
#define URC_MAX_STACK		16
#define URC_FONT_CHARS		256
#define URC_FONT_GLYPHS		256
#define URC_FONT_TEXWIDTH	256.0f
#define URC_DEFAULT_FONT	"verdana-14"
#define URC_PRINT_FONT		"facfont-20"
#define URC_PRINT_TIME		4000		// 0x418cc0 sets 4000.0; retail shows the intro's quote at 1 s and not at 4.5 s
#define URC_PRINT_FADE		1000		// the fade inside that time; not read from retail
#define URC_PRINT_HOLD		( URC_PRINT_TIME - URC_PRINT_FADE )
#define URC_PRINT_FADEIN	500			// the credits fade in; the time is not read from retail

#define Vector4Set( v, a, b, c, d )	( (v)[0] = (a), (v)[1] = (b), (v)[2] = (c), (v)[3] = (d) )

typedef struct {
	char		name[64];
	qhandle_t	shader;
	float		height;
	float		aspect;				// texture width over height
	int			indirection[URC_FONT_CHARS];
	int			numGlyphs;
	float		glyph[URC_FONT_GLYPHS][4];	// x y w h in texture pixels
} urcFont_t;

typedef enum {
	WC_LABEL, WC_BUTTON, WC_CHECKBOX, WC_FIELD, WC_SLIDER, WC_LIST, WC_PULLDOWN, WC_OTHER
} urcClass_t;

typedef enum {
	DIR_NONE, DIR_FROM_TOP, DIR_FROM_BOTTOM, DIR_FROM_LEFT, DIR_FROM_RIGHT
} urcDir_t;

typedef struct {
	urcClass_t	cls;
	char		name[64];
	char		title[256];
	float		rect[4];
	vec4_t		fgcolor;
	vec4_t		bgcolor;
	char		font[64];
	char		shaderName[MAX_QPATH];
	char		tileShaderName[MAX_QPATH];
	char		hoverShaderName[MAX_QPATH];
	char		statShaderName[MAX_QPATH];
	char		statTileName[MAX_QPATH];
	char		statFlashName[MAX_QPATH];
	char		checkedName[MAX_QPATH];
	char		uncheckedName[MAX_QPATH];
	qhandle_t	shader, tileShader, hoverShader, statShader, statTile, statFlash, checkedShader, uncheckedShader;
	qboolean	shadersLoaded;
	char		clickSound[MAX_QPATH];
	char		hoverSound[MAX_QPATH];
	char		linkcvar[64];
	char		stuffcommand[256];
	char		checkedCommand[256];
	char		uncheckedCommand[256];
	// "linkstring <value> <text>": what a label shows for a cvar value
	int			numLinkStrings;
	struct {
		char	value[32];
		char	text[64];
	}			linkStrings[16];
	// sliders: "setrange <min> <max>", "stepsize <s>", "slidertype float|int"
	float		rangeMin, rangeMax, stepSize;
	qboolean	sliderInt;
	// pulldowns: 'menushader "MENU" <shader>', 'addpopup "MENU" <text> command <cmd>'
	char		menuShaderName[MAX_QPATH];
	char		selMenuShaderName[MAX_QPATH];
	qhandle_t	menuShader, selMenuShader;
	int			numPopups;
	struct {
		char	text[64];
		char	command[128];
	}			popups[16];
	char		bindFile[MAX_QPATH];	// FakkBindList: "filename ui/bind.scr"
	int			playerstat;			// -1 none
	int			maxplayerstat;		// -1 none
	int			statbar;			// 0 none, 1 vertical, 2 horizontal
	float		statMin, statMax;
	int			invmodelrender;		// -1 none
	urcDir_t	dir;
	float		dirTime;
	float		fadein;
	int			ordernumber;
	// runtime
	int			lastStat;
	int			statChangeTime;
} urcWidget_t;

typedef struct {
	char		name[64];
	char		file[MAX_QPATH];
	float		width, height;
	urcDir_t	dir;
	float		dirTime;
	vec4_t		bgcolor;
	qboolean	hasBgfill;
	vec4_t		bgfill;
	qboolean	fullscreen;
	int			alignH;				// 0 left, 1 centre, 2 right
	int			alignV;				// 0 top, 1 centre, 2 bottom
	float		fadein;
	int			firstWidget, numWidgets;
	// runtime
	qboolean	visible;
	int			showTime;
} urcMenu_t;

// "aliascache <name> <file> [weight <w>]": a sound a widget names by alias;
// "hoversound menu_newgame" picks among menu_newgame1.. by weight
typedef struct {
	char		name[64];
	char		path[MAX_QPATH];
	float		weight;
} urcAlias_t;

#define URC_MAX_ALIASES		256

typedef struct {
	qboolean	active;
	qboolean	located;
	int			x, y;
	char		text[1024];
	qhandle_t	shader;				// text beginning with '@'
	float		alpha;
	int			startTime;
} urcPrint_t;

static urcMenu_t	urc_menus[URC_MAX_MENUS];
static int			urc_numMenus;
static urcWidget_t	urc_widgets[URC_MAX_WIDGETS];
static int			urc_numWidgets;
static urcFont_t	urc_fonts[URC_MAX_FONTS];
static int			urc_numFonts;
static int			urc_stack[URC_MAX_STACK];
static int			urc_stackDepth;
static urcPrint_t	urc_centerprint;
static urcPrint_t	urc_locprint;
static qboolean		urc_initialised;
static int			urc_lastState = -1;
static int			urc_pausedByMenu;		// 1: the menus paused the game, 2: it was paused already
static urcAlias_t	urc_aliases[URC_MAX_ALIASES];
static int			urc_numAliases;
static float		urc_cursorX = 320, urc_cursorY = 240;	// 640x480 units
static int			urc_hoverWidget = -1;
static int			urc_dragSlider = -1;		// the slider the left button is held on
static int			urc_popupWidget = -1;		// the pulldown whose list is open
static qboolean		urc_loading;				// from a map load's start to its first snapshot
static qhandle_t	urc_loadingShader;

#define URC_LOADING_SHADER	"textures/menu/loading"	// 256x256, pulsing (scripts/menu.shader)

// the key bind list (FakkBindList), from its "filename" script
typedef struct {
	char		name[64];
	char		command[128];
	char		shader[MAX_QPATH];
} urcBindItem_t;

#define URC_MAX_BINDITEMS	64

typedef struct {
	char			file[MAX_QPATH];
	qboolean		loaded;
	char			headerName[3][MAX_QPATH];
	qhandle_t		headerShader[3];
	int				fillWidth, cmdWidth, rowHeight, keyWidth;
	char			fillName[MAX_QPATH];
	qhandle_t		fillShader;
	vec4_t			titleFg, titleBg, activeFg, activeBg, inactiveFg, inactiveBg, selectFg, selectBg;
	char			changeSound[MAX_QPATH], activeSound[MAX_QPATH], enterSound[MAX_QPATH];
	int				numItems;
	urcBindItem_t	items[URC_MAX_BINDITEMS];
	// runtime
	int				scroll;
	int				selected;
	int				waitingSlot;		// -1, or the key column waiting for a key press
} urcBindList_t;

static urcBindList_t	urc_bindList;
static qhandle_t		urc_sliderThumb, urc_sliderThumbSel;

static void URC_PlaySound( const char *name );
static void URC_DrawPopup( void );

static urcMenu_t	*urc_healthhud, *urc_weaphud, *urc_dualweaphud, *urc_bosshud;

static cvar_t		*ui_hud;
static cvar_t		*ui_urcdebug;
static qhandle_t	urc_addFadeShader;

/*
=======================================================================

FONTS

=======================================================================
*/

/*
=================
URC_LoadFont

fonts/<name>.ritualfont: "RitFont", height <h>, aspect <a>, indirections
{ 256 ints }, locations { { x y w h } ... }. The glyph image is
gfx/fonts/<name>, 256 pixels wide and 256/aspect high.
=================
*/
static urcFont_t *URC_LoadFont( const char *name ) {
	urcFont_t	*font;
	char		*buf, *p, *tok;
	int			i;

	for ( i = 0 ; i < urc_numFonts ; i++ ) {
		if ( !Q_stricmp( urc_fonts[i].name, name ) ) {
			return &urc_fonts[i];
		}
	}
	if ( urc_numFonts == URC_MAX_FONTS ) {
		return urc_numFonts ? &urc_fonts[0] : NULL;
	}
	font = &urc_fonts[urc_numFonts];
	Com_Memset( font, 0, sizeof( *font ) );
	Q_strncpyz( font->name, name, sizeof( font->name ) );
	font->height = 14;
	font->aspect = 4;
	for ( i = 0 ; i < URC_FONT_CHARS ; i++ ) {
		font->indirection[i] = -1;
	}

	if ( FS_ReadFile( va( "fonts/%s.ritualfont", name ), (void **)&buf ) <= 0 ) {
		Com_Printf( S_COLOR_YELLOW "WARNING: font fonts/%s.ritualfont not found\n", name );
		if ( Q_stricmp( name, URC_DEFAULT_FONT ) ) {
			return URC_LoadFont( URC_DEFAULT_FONT );
		}
		urc_numFonts++;		// a font that draws nothing
		return font;
	}
	urc_numFonts++;

	p = buf;
	while ( 1 ) {
		tok = COM_Parse( &p );
		if ( !tok[0] ) {
			break;
		}
		if ( !Q_stricmp( tok, "height" ) ) {
			font->height = atof( COM_Parse( &p ) );
		} else if ( !Q_stricmp( tok, "aspect" ) ) {
			font->aspect = atof( COM_Parse( &p ) );
			if ( font->aspect <= 0 ) {
				font->aspect = 1;
			}
		} else if ( !Q_stricmp( tok, "indirections" ) ) {
			COM_Parse( &p );	// {
			for ( i = 0 ; i < URC_FONT_CHARS ; i++ ) {
				tok = COM_Parse( &p );
				if ( !tok[0] || tok[0] == '}' ) {
					break;
				}
				font->indirection[i] = atoi( tok );
			}
			if ( i == URC_FONT_CHARS ) {
				COM_Parse( &p );	// }
			}
		} else if ( !Q_stricmp( tok, "locations" ) ) {
			COM_Parse( &p );	// {
			while ( 1 ) {
				tok = COM_Parse( &p );
				if ( !tok[0] || tok[0] == '}' ) {
					break;
				}
				// tok is {
				if ( font->numGlyphs < URC_FONT_GLYPHS ) {
					for ( i = 0 ; i < 4 ; i++ ) {
						font->glyph[font->numGlyphs][i] = atof( COM_Parse( &p ) );
					}
					font->numGlyphs++;
				} else {
					for ( i = 0 ; i < 4 ; i++ ) {
						COM_Parse( &p );
					}
				}
				COM_Parse( &p );	// }
			}
		}
	}
	FS_FreeFile( buf );

	font->shader = re.RegisterShaderNoMip( va( "gfx/fonts/%s", name ) );
	return font;
}

static float URC_CharWidth( const urcFont_t *font, int c ) {
	int		idx;

	if ( !font ) {
		return 0;
	}
	idx = font->indirection[c & 0xff];
	if ( idx < 0 || idx >= font->numGlyphs ) {
		return c == ' ' ? font->height * 0.3f : 0;
	}
	return font->glyph[idx][2] + 1;		// the glyph list leaves one pixel between glyphs
}

static float URC_StringWidth( const urcFont_t *font, const char *s ) {
	float	w = 0;

	for ( ; *s && *s != '\n' ; s++ ) {
		w += URC_CharWidth( font, *s );
	}
	return w;
}

/*
=================
URC_DrawString

Draws one line (up to a newline) at x, y in 640x480 units, with the
font's own pixel sizes. Returns the width drawn.
=================
*/
static float URC_DrawString( const urcFont_t *font, float x, float y, const char *s, const vec4_t color ) {
	float	texH, startX = x;
	vec4_t	c;

	if ( !font || !font->shader ) {
		return 0;
	}
	Vector4Copy( color, c );
	re.SetColor( c );
	texH = URC_FONT_TEXWIDTH / font->aspect;
	for ( ; *s && *s != '\n' ; s++ ) {
		int		idx = font->indirection[*s & 0xff];
		float	gx, gy, gw, gh, dx, dy, dw, dh;

		if ( idx < 0 || idx >= font->numGlyphs ) {
			x += URC_CharWidth( font, *s );
			continue;
		}
		gx = font->glyph[idx][0];
		gy = font->glyph[idx][1];
		gw = font->glyph[idx][2];
		gh = font->glyph[idx][3];
		dx = x;
		dy = y;
		dw = gw;
		dh = gh;
		SCR_AdjustFrom640( &dx, &dy, &dw, &dh );
		re.DrawStretchPic( dx, dy, dw, dh, gx / URC_FONT_TEXWIDTH, gy / texH,
			( gx + gw ) / URC_FONT_TEXWIDTH, ( gy + gh ) / texH, font->shader );
		x += gw + 1;
	}
	re.SetColor( NULL );
	return x - startX;
}

/*
=======================================================================

PARSING

=======================================================================
*/

static urcDir_t URC_ParseDir( const char *s ) {
	if ( !Q_stricmp( s, "from_top" ) ) return DIR_FROM_TOP;
	if ( !Q_stricmp( s, "from_bottom" ) ) return DIR_FROM_BOTTOM;
	if ( !Q_stricmp( s, "from_left" ) ) return DIR_FROM_LEFT;
	if ( !Q_stricmp( s, "from_right" ) ) return DIR_FROM_RIGHT;
	return DIR_NONE;
}

/*
=================
URC_LineArgs

The rest of the current line as tokens.
=================
*/
static int URC_LineArgs( char **p, char args[][MAX_QPATH], int maxArgs ) {
	int		n = 0;

	while ( 1 ) {
		char	*tok = COM_ParseExt( p, qfalse );

		if ( !tok[0] ) {
			break;
		}
		if ( n < maxArgs ) {
			Q_strncpyz( args[n], tok, MAX_QPATH );
		}
		n++;
	}
	return n < maxArgs ? n : maxArgs;
}

static void URC_ParseColor( char args[][MAX_QPATH], int n, vec4_t color ) {
	int		i;

	for ( i = 0 ; i < 4 ; i++ ) {
		color[i] = i < n ? atof( args[i] ) : ( i == 3 ? 1.0f : 0.0f );
	}
}

static void URC_ParseAlign( urcMenu_t *menu, char args[][MAX_QPATH], int n ) {
	int		i;

	for ( i = 0 ; i < n ; i++ ) {
		if ( !Q_stricmp( args[i], "left" ) ) menu->alignH = 0;
		else if ( !Q_stricmp( args[i], "right" ) ) menu->alignH = 2;
		else if ( !Q_stricmp( args[i], "centerx" ) || !Q_stricmp( args[i], "center" ) ) menu->alignH = 1;
		else if ( !Q_stricmp( args[i], "top" ) ) menu->alignV = 0;
		else if ( !Q_stricmp( args[i], "bottom" ) ) menu->alignV = 2;
		else if ( !Q_stricmp( args[i], "centery" ) ) menu->alignV = 1;
	}
}

// keys the shipped files use that this implementation reads and ignores
static const char *urc_ignoredKeys[] = {
	"additem", "initdata", "updatecvar", "edit",
	"bordercolor", "noparentclip", "noparentadjust", "hidecommand",
	"showcommand", "stretchdir", "vidmode", "alpha", "soundname", NULL
};

static qboolean URC_IgnoredKey( const char *key ) {
	int		i;

	for ( i = 0 ; urc_ignoredKeys[i] ; i++ ) {
		if ( !Q_stricmp( key, urc_ignoredKeys[i] ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

/*
=================
URC_ParseWidget

After "resource <Class>": the { key args... } block.
=================
*/
static void URC_ParseWidget( urcMenu_t *menu, char **p, const char *className, const char *file ) {
	urcWidget_t	*w;
	char		args[8][MAX_QPATH];
	char		key[64];
	char		*tok;
	int			n;

	tok = COM_Parse( p );
	if ( tok[0] != '{' ) {
		Com_Printf( S_COLOR_YELLOW "WARNING: %s: expected { after resource %s\n", file, className );
		return;
	}
	if ( urc_numWidgets == URC_MAX_WIDGETS ) {
		Com_Printf( S_COLOR_YELLOW "WARNING: %s: too many widgets\n", file );
		SkipBracedSection( p, 1 );
		return;
	}
	w = &urc_widgets[urc_numWidgets++];
	menu->numWidgets++;
	Com_Memset( w, 0, sizeof( *w ) );
	if ( !Q_stricmp( className, "Label" ) ) w->cls = WC_LABEL;
	else if ( !Q_stricmp( className, "Button" ) ) w->cls = WC_BUTTON;
	else if ( !Q_stricmp( className, "CheckBox" ) ) w->cls = WC_CHECKBOX;
	else if ( !Q_stricmp( className, "Field" ) ) w->cls = WC_FIELD;
	else if ( !Q_stricmp( className, "Slider" ) ) w->cls = WC_SLIDER;
	else if ( !Q_stricmp( className, "List" ) ) w->cls = WC_LIST;
	else if ( !Q_stricmp( className, "PulldownMenuContainer" ) ) w->cls = WC_PULLDOWN;
	else w->cls = WC_OTHER;
	Vector4Set( w->fgcolor, 1, 1, 1, 1 );
	Vector4Set( w->bgcolor, 0, 0, 0, 0 );
	Q_strncpyz( w->font, URC_DEFAULT_FONT, sizeof( w->font ) );
	w->playerstat = w->maxplayerstat = w->invmodelrender = -1;
	w->statMin = 0;
	w->statMax = 100;
	w->lastStat = -99999;
	w->rangeMin = 0;
	w->rangeMax = 1;
	w->stepSize = 0;

	while ( 1 ) {
		tok = COM_Parse( p );
		if ( !tok[0] ) {
			Com_Printf( S_COLOR_YELLOW "WARNING: %s: unterminated resource block\n", file );
			return;
		}
		if ( tok[0] == '}' ) {
			return;
		}
		Q_strncpyz( key, tok, sizeof( key ) );		// the parser's buffer is about to be reused
		tok = key;
		n = URC_LineArgs( p, args, 8 );
		if ( !Q_stricmp( tok, "name" ) ) {
			Q_strncpyz( w->name, n ? args[0] : "", sizeof( w->name ) );
		} else if ( !Q_stricmp( tok, "title" ) ) {
			Q_strncpyz( w->title, n ? args[0] : "", sizeof( w->title ) );
		} else if ( !Q_stricmp( tok, "rect" ) && n >= 4 ) {
			w->rect[0] = atof( args[0] ); w->rect[1] = atof( args[1] );
			w->rect[2] = atof( args[2] ); w->rect[3] = atof( args[3] );
		} else if ( !Q_stricmp( tok, "fgcolor" ) ) {
			URC_ParseColor( args, n, w->fgcolor );
		} else if ( !Q_stricmp( tok, "bgcolor" ) ) {
			URC_ParseColor( args, n, w->bgcolor );
		} else if ( !Q_stricmp( tok, "borderstyle" ) ) {
			// NONE, RAISED, INDENT_BORDER, 3D_BORDER: not drawn yet
		} else if ( !Q_stricmp( tok, "font" ) && n ) {
			Q_strncpyz( w->font, args[0], sizeof( w->font ) );
		} else if ( !Q_stricmp( tok, "shader" ) && n ) {
			Q_strncpyz( w->shaderName, args[0], sizeof( w->shaderName ) );
		} else if ( !Q_stricmp( tok, "tileshader" ) && n ) {
			Q_strncpyz( w->tileShaderName, args[0], sizeof( w->tileShaderName ) );
		} else if ( !Q_stricmp( tok, "hovershader" ) && n ) {
			Q_strncpyz( w->hoverShaderName, args[0], sizeof( w->hoverShaderName ) );
		} else if ( !Q_stricmp( tok, "statbar_shader" ) && n ) {
			Q_strncpyz( w->statShaderName, args[0], sizeof( w->statShaderName ) );
		} else if ( !Q_stricmp( tok, "statbar_tileshader" ) && n ) {
			Q_strncpyz( w->statTileName, args[0], sizeof( w->statTileName ) );
		} else if ( !Q_stricmp( tok, "statbar_tileshader_flash" ) && n ) {
			Q_strncpyz( w->statFlashName, args[0], sizeof( w->statFlashName ) );
		} else if ( !Q_stricmp( tok, "checked_shader" ) && n ) {
			Q_strncpyz( w->checkedName, args[0], sizeof( w->checkedName ) );
		} else if ( !Q_stricmp( tok, "unchecked_shader" ) && n ) {
			Q_strncpyz( w->uncheckedName, args[0], sizeof( w->uncheckedName ) );
		} else if ( !Q_stricmp( tok, "checked_command" ) && n ) {
			Q_strncpyz( w->checkedCommand, args[0], sizeof( w->checkedCommand ) );
		} else if ( !Q_stricmp( tok, "unchecked_command" ) && n ) {
			Q_strncpyz( w->uncheckedCommand, args[0], sizeof( w->uncheckedCommand ) );
		} else if ( !Q_stricmp( tok, "linkstring" ) && n >= 2 ) {
			if ( w->numLinkStrings < ARRAY_LEN( w->linkStrings ) ) {
				Q_strncpyz( w->linkStrings[w->numLinkStrings].value, args[0], sizeof( w->linkStrings[0].value ) );
				Q_strncpyz( w->linkStrings[w->numLinkStrings].text, args[1], sizeof( w->linkStrings[0].text ) );
				w->numLinkStrings++;
			}
		} else if ( !Q_stricmp( tok, "setrange" ) && n >= 2 ) {
			w->rangeMin = atof( args[0] );
			w->rangeMax = atof( args[1] );
		} else if ( !Q_stricmp( tok, "stepsize" ) && n ) {
			w->stepSize = atof( args[0] );
		} else if ( !Q_stricmp( tok, "slidertype" ) && n ) {
			w->sliderInt = !Q_stricmp( args[0], "int" );
		} else if ( !Q_stricmp( tok, "menushader" ) && n >= 2 ) {
			Q_strncpyz( w->menuShaderName, args[1], sizeof( w->menuShaderName ) );
		} else if ( !Q_stricmp( tok, "selmenushader" ) && n >= 2 ) {
			Q_strncpyz( w->selMenuShaderName, args[1], sizeof( w->selMenuShaderName ) );
		} else if ( !Q_stricmp( tok, "addpopup" ) && n >= 4 ) {
			if ( w->numPopups < ARRAY_LEN( w->popups ) ) {
				Q_strncpyz( w->popups[w->numPopups].text, args[1], sizeof( w->popups[0].text ) );
				Q_strncpyz( w->popups[w->numPopups].command, args[3], sizeof( w->popups[0].command ) );
				w->numPopups++;
			}
		} else if ( !Q_stricmp( tok, "filename" ) && n ) {
			Q_strncpyz( w->bindFile, args[0], sizeof( w->bindFile ) );
		} else if ( !Q_stricmp( tok, "clicksound" ) && n ) {
			Q_strncpyz( w->clickSound, args[0], sizeof( w->clickSound ) );
		} else if ( !Q_stricmp( tok, "hoversound" ) && n ) {
			Q_strncpyz( w->hoverSound, args[0], sizeof( w->hoverSound ) );
		} else if ( !Q_stricmp( tok, "aliascache" ) && n >= 2 ) {
			if ( urc_numAliases < URC_MAX_ALIASES ) {
				urcAlias_t	*a = &urc_aliases[urc_numAliases++];

				Q_strncpyz( a->name, args[0], sizeof( a->name ) );
				Q_strncpyz( a->path, args[1], sizeof( a->path ) );
				a->weight = ( n >= 4 && !Q_stricmp( args[2], "weight" ) ) ? atof( args[3] ) : 1.0f;
			}
		} else if ( !Q_stricmp( tok, "linkcvar" ) && n ) {
			Q_strncpyz( w->linkcvar, args[0], sizeof( w->linkcvar ) );
		} else if ( !Q_stricmp( tok, "stuffcommand" ) && n ) {
			Q_strncpyz( w->stuffcommand, args[0], sizeof( w->stuffcommand ) );
		} else if ( !Q_stricmp( tok, "playerstat" ) && n ) {
			w->playerstat = atoi( args[0] );
		} else if ( !Q_stricmp( tok, "maxplayerstat" ) && n ) {
			w->maxplayerstat = atoi( args[0] );
		} else if ( !Q_stricmp( tok, "statbar" ) && n ) {
			w->statbar = !Q_stricmp( args[0], "horizontal" ) ? 2 : 1;
			if ( n >= 3 ) {
				w->statMin = atof( args[1] );
				w->statMax = atof( args[2] );
			}
		} else if ( !Q_stricmp( tok, "invmodelrender" ) && n ) {
			w->invmodelrender = atoi( args[0] );
		} else if ( !Q_stricmp( tok, "direction" ) && n ) {
			w->dir = URC_ParseDir( args[0] );
			w->dirTime = n > 1 ? atof( args[1] ) : 0;
		} else if ( !Q_stricmp( tok, "fadein" ) && n ) {
			w->fadein = atof( args[0] );
		} else if ( !Q_stricmp( tok, "ordernumber" ) && n ) {
			w->ordernumber = atoi( args[0] );
		} else if ( !URC_IgnoredKey( tok ) ) {
			Com_DPrintf( "%s: unknown widget key %s\n", file, tok );
		}
	}
}

static void URC_ParseMenuBody( urcMenu_t *menu, char *text, const char *file, qboolean included );

/*
=================
URC_ParseMenuBody

Menu-level keys and resource blocks up to "end.".
=================
*/
static void URC_ParseMenuBody( urcMenu_t *menu, char *text, const char *file, qboolean included ) {
	char	*p = text;
	char	*tok;
	char	key[64];
	char	args[8][MAX_QPATH];
	int		n;

	while ( 1 ) {
		tok = COM_Parse( &p );
		if ( !tok[0] || !Q_stricmp( tok, "end." ) || !Q_stricmp( tok, "end" ) ) {
			return;
		}
		if ( !Q_stricmp( tok, "resource" ) ) {
			char	className[64];

			Q_strncpyz( className, COM_Parse( &p ), sizeof( className ) );
			URC_ParseWidget( menu, &p, className, file );
			continue;
		}
		Q_strncpyz( key, tok, sizeof( key ) );		// the parser's buffer is about to be reused
		tok = key;
		n = URC_LineArgs( &p, args, 8 );
		if ( !Q_stricmp( tok, "include" ) && n ) {
			char	*inc;

			if ( FS_ReadFile( args[0], (void **)&inc ) > 0 ) {
				URC_ParseMenuBody( menu, inc, args[0], qtrue );
				FS_FreeFile( inc );
			} else {
				Com_Printf( S_COLOR_YELLOW "WARNING: %s: include %s not found\n", file, args[0] );
			}
		} else if ( included ) {
			continue;		// an included file only contributes resources
		} else if ( !Q_stricmp( tok, "bgcolor" ) ) {
			URC_ParseColor( args, n, menu->bgcolor );
		} else if ( !Q_stricmp( tok, "bgfill" ) ) {
			URC_ParseColor( args, n, menu->bgfill );
			menu->hasBgfill = qtrue;
		} else if ( !Q_stricmp( tok, "fullscreen" ) ) {
			menu->fullscreen = n ? atoi( args[0] ) != 0 : qtrue;
		} else if ( !Q_stricmp( tok, "align" ) ) {
			URC_ParseAlign( menu, args, n );
		} else if ( !Q_stricmp( tok, "fadein" ) && n ) {
			menu->fadein = atof( args[0] );
		} else if ( !Q_stricmp( tok, "borderstyle" ) ) {
		} else if ( !URC_IgnoredKey( tok ) ) {
			Com_DPrintf( "%s: unknown menu key %s\n", file, tok );
		}
	}
}

/*
=================
URC_LoadMenuFile

ui/<file>.urc: "menu <name> <width> <height> <direction> <time>" then
the body. A menu is found by the name in the file, not the file name.
=================
*/
static urcMenu_t *URC_LoadMenuFile( const char *path ) {
	urcMenu_t	*menu;
	char		*buf, *p, *tok;
	int			i;

	if ( FS_ReadFile( path, (void **)&buf ) <= 0 ) {
		return NULL;
	}
	p = buf;
	tok = COM_Parse( &p );
	if ( Q_stricmp( tok, "menu" ) ) {
		Com_Printf( S_COLOR_YELLOW "WARNING: %s does not begin with menu\n", path );
		FS_FreeFile( buf );
		return NULL;
	}
	if ( urc_numMenus == URC_MAX_MENUS ) {
		FS_FreeFile( buf );
		return NULL;
	}
	menu = &urc_menus[urc_numMenus];
	Com_Memset( menu, 0, sizeof( *menu ) );
	Q_strncpyz( menu->name, COM_Parse( &p ), sizeof( menu->name ) );
	for ( i = 0 ; i < urc_numMenus ; i++ ) {
		if ( !Q_stricmp( urc_menus[i].name, menu->name ) ) {
			Com_DPrintf( "%s: menu %s already loaded from %s\n", path, menu->name, urc_menus[i].file );
			FS_FreeFile( buf );
			return &urc_menus[i];
		}
	}
	urc_numMenus++;
	Q_strncpyz( menu->file, path, sizeof( menu->file ) );
	menu->width = atof( COM_ParseExt( &p, qfalse ) );
	menu->height = atof( COM_ParseExt( &p, qfalse ) );
	menu->dir = URC_ParseDir( COM_ParseExt( &p, qfalse ) );
	menu->dirTime = atof( COM_ParseExt( &p, qfalse ) );
	Vector4Set( menu->bgcolor, 0, 0, 0, 0 );
	menu->firstWidget = urc_numWidgets;
	URC_ParseMenuBody( menu, p, path, qfalse );
	FS_FreeFile( buf );
	Com_DPrintf( "urc: %s: menu %s, %d widgets\n", path, menu->name, menu->numWidgets );
	return menu;
}

static urcMenu_t *URC_FindMenu( const char *name ) {
	int		i;

	for ( i = 0 ; i < urc_numMenus ; i++ ) {
		if ( !Q_stricmp( urc_menus[i].name, name ) ) {
			return &urc_menus[i];
		}
	}
	return NULL;
}

/*
=======================================================================

DRAWING

=======================================================================
*/

static void URC_FillRect( float x, float y, float w, float h, const vec4_t color ) {
	if ( color[3] <= 0 ) {
		return;
	}
	SCR_FillRect( x, y, w, h, color );
}

/*
=================
URC_TileST

The texture coordinates that lay a shader's image over a w x h widget at
the image's own size, from the top left: FAKK2's UI tiles its "tileshader"
pictures rather than stretching them.
=================
*/
static void URC_TileST( qhandle_t shader, float w, float h, float *s, float *t ) {
	int		iw = 0, ih = 0;

	if ( re.ShaderImageSize ) {
		re.ShaderImageSize( shader, &iw, &ih );
	}
	*s = iw > 0 ? w / iw : 1;
	*t = ih > 0 ? h / ih : 1;
}

static void URC_DrawPicST( float x, float y, float w, float h, float s1, float t1, float s2, float t2, qhandle_t shader, const vec4_t color ) {
	re.SetColor( color );
	SCR_AdjustFrom640( &x, &y, &w, &h );
	re.DrawStretchPic( x, y, w, h, s1, t1, s2, t2, shader );
	re.SetColor( NULL );
}

static void URC_LoadWidgetShaders( urcWidget_t *w ) {
	if ( w->shadersLoaded ) {
		return;
	}
	w->shadersLoaded = qtrue;
	if ( w->shaderName[0] ) w->shader = re.RegisterShaderNoMip( w->shaderName );
	if ( w->tileShaderName[0] ) w->tileShader = re.RegisterShaderNoMip( w->tileShaderName );
	if ( w->hoverShaderName[0] ) w->hoverShader = re.RegisterShaderNoMip( w->hoverShaderName );
	if ( w->statShaderName[0] ) w->statShader = re.RegisterShaderNoMip( w->statShaderName );
	if ( w->statTileName[0] ) w->statTile = re.RegisterShaderNoMip( w->statTileName );
	if ( w->statFlashName[0] ) w->statFlash = re.RegisterShaderNoMip( w->statFlashName );
	if ( w->checkedName[0] ) w->checkedShader = re.RegisterShaderNoMip( w->checkedName );
	if ( w->uncheckedName[0] ) w->uncheckedShader = re.RegisterShaderNoMip( w->uncheckedName );
	if ( w->menuShaderName[0] ) w->menuShader = re.RegisterShaderNoMip( w->menuShaderName );
	if ( w->selMenuShaderName[0] ) w->selMenuShader = re.RegisterShaderNoMip( w->selMenuShaderName );
}

/*
=======================================================================

THE KEY BIND LIST

"ui/bind.scr": header <n> <text> <shader>, bind_width/height,
bind_fillwidth <w> <shader>, bind_commandwidth/height,
bind_primarykeywidth/height, bind_alternatekeywidth/height, colours,
changesound/activesound/entersound, binditem <name> <command> <shader>.

Drawn as the retail list draws it: a header row, then a row per item
with the command's name and the first two keys bound to it; a key cell
clicked waits for the next key, which takes the place of the old one;
escape leaves it; the wheel scrolls.

=======================================================================
*/

static void URC_LoadBindList( const char *file ) {
	urcBindList_t	*bl = &urc_bindList;
	char			*buf, *p, *tok;
	char			key[64];
	char			args[8][MAX_QPATH];
	int				n, i;

	if ( bl->loaded && !Q_stricmp( bl->file, file ) ) {
		return;
	}
	Com_Memset( bl, 0, sizeof( *bl ) );
	Q_strncpyz( bl->file, file, sizeof( bl->file ) );
	bl->loaded = qtrue;
	bl->fillWidth = 16;
	bl->cmdWidth = 128;
	bl->rowHeight = 16;
	bl->keyWidth = 64;
	bl->waitingSlot = -1;
	Vector4Set( bl->titleFg, 1, 1, 0, 1 );
	Vector4Set( bl->titleBg, 0, 0, 0, 1 );
	Vector4Set( bl->activeFg, 1, 1, 1, 1 );
	Vector4Set( bl->activeBg, 0, 0, 0, 1 );
	Vector4Set( bl->inactiveFg, 1, 1, 1, 1 );
	Vector4Set( bl->inactiveBg, 0.1f, 0.1f, 0.1f, 1 );
	Vector4Set( bl->selectFg, 1, 1, 0, 1 );
	Vector4Set( bl->selectBg, 0, 0, 0, 1 );

	if ( FS_ReadFile( file, (void **)&buf ) <= 0 ) {
		Com_Printf( S_COLOR_YELLOW "WARNING: bind list %s not found\n", file );
		return;
	}
	p = buf;
	while ( 1 ) {
		tok = COM_Parse( &p );
		if ( !tok[0] ) {
			break;
		}
		Q_strncpyz( key, tok, sizeof( key ) );
		n = URC_LineArgs( &p, args, 8 );
		if ( !Q_stricmp( key, "header" ) && n >= 3 ) {
			i = atoi( args[0] ) - 1;
			if ( i >= 0 && i < 3 ) {
				Q_strncpyz( bl->headerName[i], args[2], sizeof( bl->headerName[i] ) );
			}
		} else if ( !Q_stricmp( key, "bind_fillwidth" ) && n >= 1 ) {
			bl->fillWidth = atoi( args[0] );
			if ( n >= 2 ) {
				Q_strncpyz( bl->fillName, args[1], sizeof( bl->fillName ) );
			}
		} else if ( !Q_stricmp( key, "bind_commandwidth" ) && n ) {
			bl->cmdWidth = atoi( args[0] );
		} else if ( !Q_stricmp( key, "bind_commandheight" ) && n ) {
			bl->rowHeight = atoi( args[0] );
		} else if ( !Q_stricmp( key, "bind_primarykeywidth" ) && n ) {
			bl->keyWidth = atoi( args[0] );
		} else if ( !Q_stricmp( key, "titleforegroundcolor" ) ) {
			URC_ParseColor( args, n, bl->titleFg );
		} else if ( !Q_stricmp( key, "titlebackgroundcolor" ) ) {
			URC_ParseColor( args, n, bl->titleBg );
		} else if ( !Q_stricmp( key, "activeforegroundcolor" ) ) {
			URC_ParseColor( args, n, bl->activeFg );
		} else if ( !Q_stricmp( key, "activebackgroundcolor" ) ) {
			URC_ParseColor( args, n, bl->activeBg );
		} else if ( !Q_stricmp( key, "inactiveforegroundcolor" ) ) {
			URC_ParseColor( args, n, bl->inactiveFg );
		} else if ( !Q_stricmp( key, "inactivebackgroundcolor" ) ) {
			URC_ParseColor( args, n, bl->inactiveBg );
		} else if ( !Q_stricmp( key, "selectforegroundcolor" ) ) {
			URC_ParseColor( args, n, bl->selectFg );
		} else if ( !Q_stricmp( key, "selectbackgroundcolor" ) ) {
			URC_ParseColor( args, n, bl->selectBg );
		} else if ( !Q_stricmp( key, "changesound" ) && n ) {
			Q_strncpyz( bl->changeSound, args[0], sizeof( bl->changeSound ) );
		} else if ( !Q_stricmp( key, "activesound" ) && n ) {
			Q_strncpyz( bl->activeSound, args[0], sizeof( bl->activeSound ) );
		} else if ( !Q_stricmp( key, "entersound" ) && n ) {
			Q_strncpyz( bl->enterSound, args[0], sizeof( bl->enterSound ) );
		} else if ( !Q_stricmp( key, "binditem" ) && n >= 2 ) {
			if ( bl->numItems < URC_MAX_BINDITEMS ) {
				urcBindItem_t	*item = &bl->items[bl->numItems++];

				Q_strncpyz( item->name, args[0], sizeof( item->name ) );
				Q_strncpyz( item->command, args[1], sizeof( item->command ) );
				if ( n >= 3 ) {
					Q_strncpyz( item->shader, args[2], sizeof( item->shader ) );
				}
			}
		}
		// bind_width, bind_height, the key heights, align, the other colours: the widget's rect rules
	}
	FS_FreeFile( buf );

	for ( i = 0 ; i < 3 ; i++ ) {
		if ( bl->headerName[i][0] ) {
			bl->headerShader[i] = re.RegisterShaderNoMip( bl->headerName[i] );
		}
	}
	if ( bl->fillName[0] ) {
		bl->fillShader = re.RegisterShaderNoMip( bl->fillName );
	}
}

// the first two keys bound to a command; -1 for none
static void URC_BindKeys( const char *command, int bound[2] ) {
	int		k, n = 0;

	bound[0] = bound[1] = -1;
	for ( k = 0 ; k < MAX_KEYS && n < 2 ; k++ ) {
		const char	*b = Key_GetBinding( k );

		if ( b && b[0] && !Q_stricmp( b, command ) ) {
			bound[n++] = k;
		}
	}
}

static int URC_BindVisibleRows( const urcWidget_t *w ) {
	int		rows = (int)( w->rect[3] / urc_bindList.rowHeight ) - 1;

	return rows < 1 ? 1 : rows;
}

/*
=================
URC_DrawBindList
=================
*/
static void URC_DrawBindList( const urcWidget_t *w, float x, float y, float alpha ) {
	urcBindList_t	*bl = &urc_bindList;
	urcFont_t		*font;
	float			colX[3], colW[3], rowH, fillX[3], barX, barW;
	int				rows, r, c, i;
	vec4_t			color;

	URC_LoadBindList( w->bindFile );
	font = URC_LoadFont( "verdana-12" );	// the retail list's rows are set in the smaller face
	rowH = bl->rowHeight;
	colX[0] = x;
	colW[0] = bl->cmdWidth;
	fillX[0] = colX[0] + colW[0];
	colX[1] = fillX[0] + bl->fillWidth;
	colW[1] = bl->keyWidth;
	fillX[1] = colX[1] + colW[1];
	colX[2] = fillX[1] + bl->fillWidth;
	colW[2] = bl->keyWidth;
	fillX[2] = colX[2] + colW[2];
	barX = fillX[2] + bl->fillWidth;
	barW = w->rect[0] + w->rect[2] - barX;
	rows = URC_BindVisibleRows( w );
	if ( bl->scroll > bl->numItems - rows ) {
		bl->scroll = bl->numItems - rows;
	}
	if ( bl->scroll < 0 ) {
		bl->scroll = 0;
	}

	// the fills between the columns, the whole height
	Vector4Set( color, 1, 1, 1, alpha );
	for ( c = 0 ; c < 3 ; c++ ) {
		if ( bl->fillShader ) {
			URC_DrawPicST( fillX[c], y, bl->fillWidth, w->rect[3], 0, 0, 1, w->rect[3] / rowH, bl->fillShader, color );
		}
	}

	// the header row
	for ( c = 0 ; c < 3 ; c++ ) {
		Vector4Copy( bl->titleBg, color );
		color[3] *= alpha;
		URC_FillRect( colX[c], y, colW[c], rowH, color );
		if ( bl->headerShader[c] ) {
			Vector4Set( color, 1, 1, 1, alpha );
			URC_DrawPicST( colX[c], y, colW[c], rowH, 0, 0, 1, 1, bl->headerShader[c], color );
		}
	}

	// the items
	for ( r = 0 ; r < rows ; r++ ) {
		float			ry = y + rowH * ( r + 1 );
		urcBindItem_t	*item;
		int				bound[2];
		qboolean		selected;

		i = bl->scroll + r;
		if ( i >= bl->numItems ) {
			break;
		}
		item = &bl->items[i];
		selected = ( i == bl->selected );
		URC_BindKeys( item->command, bound );
		for ( c = 0 ; c < 3 ; c++ ) {
			const char	*text;
			const float	*bg, *fg;

			if ( c == 0 ) {
				text = item->name;
			} else {
				text = bound[c - 1] >= 0 ? Key_KeynumToString( bound[c - 1] ) : "None";
			}
			if ( selected && bl->waitingSlot == c - 1 && c > 0 ) {
				bg = bl->activeBg;
				fg = bl->activeFg;
				text = "???";
			} else if ( selected ) {
				bg = bl->selectBg;
				fg = bl->selectFg;
			} else {
				bg = bl->inactiveBg;
				fg = bl->inactiveFg;
			}
			Vector4Copy( bg, color );
			color[3] *= alpha;
			URC_FillRect( colX[c], ry, colW[c], rowH, color );
			if ( font ) {
				float	tw = URC_StringWidth( font, text );

				Vector4Copy( fg, color );
				color[3] *= alpha;
				URC_DrawString( font, colX[c] + ( colW[c] - tw ) * 0.5f, ry + ( rowH - font->height ) * 0.5f, text, color );
			}
		}
	}

	// the scroll bar
	if ( barW > 0 ) {
		float	trackH = w->rect[3] - rowH;
		float	thumbH, thumbY;

		Vector4Copy( bl->inactiveBg, color );
		color[3] *= alpha;
		URC_FillRect( barX, y + rowH, barW, trackH, color );
		if ( bl->numItems > rows ) {
			thumbH = trackH * rows / bl->numItems;
			thumbY = y + rowH + ( trackH - thumbH ) * bl->scroll / ( bl->numItems - rows );
		} else {
			thumbH = trackH;
			thumbY = y + rowH;
		}
		Vector4Set( color, 0.6f, 0.6f, 0.6f, alpha );
		URC_FillRect( barX + 2, thumbY, barW - 4, thumbH, color );
	}
}

/*
=================
URC_BindListClick

A row selects; a key cell of the selected row waits for a key.
=================
*/
static void URC_BindListClick( const urcWidget_t *w, const float rect[4] ) {
	urcBindList_t	*bl = &urc_bindList;
	int				row, col, i;
	float			cx = urc_cursorX - rect[0];

	row = (int)( ( urc_cursorY - rect[1] ) / bl->rowHeight ) - 1;
	if ( row < 0 ) {
		return;
	}
	i = bl->scroll + row;
	if ( i >= bl->numItems ) {
		return;
	}
	if ( cx < bl->cmdWidth ) {
		col = 0;
	} else if ( cx >= bl->cmdWidth + bl->fillWidth && cx < bl->cmdWidth + bl->fillWidth + bl->keyWidth ) {
		col = 1;
	} else if ( cx >= bl->cmdWidth + 2 * bl->fillWidth + bl->keyWidth && cx < bl->cmdWidth + 2 * bl->fillWidth + 2 * bl->keyWidth ) {
		col = 2;
	} else {
		return;
	}
	bl->selected = i;
	if ( col > 0 ) {
		bl->waitingSlot = col - 1;
		URC_PlaySound( bl->enterSound );
	} else {
		bl->waitingSlot = -1;
	}
}

/*
=================
URC_BindListKey

The key pressed while a cell waits. Returns whether it was taken.
=================
*/
static qboolean URC_BindListKey( int key ) {
	urcBindList_t	*bl = &urc_bindList;
	urcBindItem_t	*item;
	int				bound[2];

	if ( bl->waitingSlot < 0 || bl->selected < 0 || bl->selected >= bl->numItems ) {
		return qfalse;
	}
	if ( key == K_ESCAPE ) {
		bl->waitingSlot = -1;
		URC_PlaySound( bl->activeSound );
		return qtrue;
	}
	if ( key == K_MWHEELUP || key == K_MWHEELDOWN ) {
		return qfalse;
	}
	item = &bl->items[bl->selected];
	URC_BindKeys( item->command, bound );
	if ( bound[bl->waitingSlot] >= 0 ) {
		Key_SetBinding( bound[bl->waitingSlot], "" );
	}
	Key_SetBinding( key, item->command );
	bl->waitingSlot = -1;
	URC_PlaySound( bl->changeSound );
	return qtrue;
}

/*
=================
URC_PlaySound

A file, or an alias: the weighted pick among the aliases whose names are
the given one followed by a number.
=================
*/
static void URC_PlaySound( const char *name ) {
	const char	*path = NULL;
	float		total = 0, r;
	int			i, len;

	if ( !name[0] ) {
		return;
	}
	S_StopLocalSound( CHAN_MENU );		// one voice at a time
	if ( strchr( name, '/' ) ) {
		path = name;
	} else {
		len = strlen( name );
		for ( i = 0 ; i < urc_numAliases ; i++ ) {
			if ( !Q_stricmpn( urc_aliases[i].name, name, len ) ) {
				total += urc_aliases[i].weight;
			}
		}
		if ( total <= 0 ) {
			return;
		}
		r = random() * total;
		for ( i = 0 ; i < urc_numAliases ; i++ ) {
			if ( !Q_stricmpn( urc_aliases[i].name, name, len ) ) {
				path = urc_aliases[i].path;
				r -= urc_aliases[i].weight;
				if ( r <= 0 ) {
					break;
				}
			}
		}
	}
	if ( path ) {
		sfxHandle_t	sfx = S_RegisterSound( path, qfalse );

		if ( sfx ) {
			S_StartLocalSound( sfx, CHAN_MENU );
		}
	}
}

static void URC_MotionOffset( urcDir_t dir, float dirTime, int elapsedMs, float parentW, float parentH, float *dx, float *dy );

/*
=================
URC_MenuOrigin, URC_WidgetRect

Where a menu and its widgets are this frame, in 640x480 units.
=================
*/
static void URC_MenuOrigin( const urcMenu_t *menu, int elapsed, float *ox, float *oy ) {
	float	dx, dy;

	switch ( menu->alignH ) {
	case 1: *ox = ( 640 - menu->width ) * 0.5f; break;
	case 2: *ox = 640 - menu->width; break;
	default: *ox = 0; break;
	}
	switch ( menu->alignV ) {
	case 1: *oy = ( 480 - menu->height ) * 0.5f; break;
	case 2: *oy = 480 - menu->height; break;
	default: *oy = 0; break;
	}
	URC_MotionOffset( menu->dir, menu->dirTime, elapsed, 640, 480, &dx, &dy );
	*ox += dx;
	*oy += dy;
}

static void URC_WidgetRect( const urcWidget_t *w, const urcMenu_t *menu, float ox, float oy, int elapsed, float rect[4] ) {
	float	dx, dy;

	URC_MotionOffset( w->dir, w->dirTime, elapsed, menu->width, menu->height, &dx, &dy );
	rect[0] = ox + w->rect[0] + dx;
	rect[1] = oy + w->rect[1] + dy;
	rect[2] = w->rect[2];
	rect[3] = w->rect[3];
}

static qboolean URC_CursorIn( const float rect[4] ) {
	return urc_cursorX >= rect[0] && urc_cursorX < rect[0] + rect[2] &&
		urc_cursorY >= rect[1] && urc_cursorY < rect[1] + rect[3];
}

/*
=================
URC_WidgetUnderCursor

In the menu on top of the stack; the last drawn wins.
=================
*/
static int URC_WidgetUnderCursor( void ) {
	urcMenu_t	*menu;
	float		ox, oy, rect[4];
	int			elapsed, i;

	if ( !urc_stackDepth ) {
		return -1;
	}
	menu = &urc_menus[urc_stack[urc_stackDepth - 1]];
	elapsed = cls.realtime - menu->showTime;
	URC_MenuOrigin( menu, elapsed, &ox, &oy );
	for ( i = menu->numWidgets - 1 ; i >= 0 ; i-- ) {
		urcWidget_t	*w = &urc_widgets[menu->firstWidget + i];

		if ( w->cls != WC_BUTTON && w->cls != WC_CHECKBOX && w->cls != WC_SLIDER &&
			w->cls != WC_PULLDOWN && !w->bindFile[0] ) {
			continue;
		}
		URC_WidgetRect( w, menu, ox, oy, elapsed, rect );
		if ( URC_CursorIn( rect ) ) {
			return menu->firstWidget + i;
		}
	}
	return -1;
}

/*
=================
URC_MotionOffset

A widget or menu with a direction slides in from off its parent's edge
over its time; returns the current offset.
=================
*/
static void URC_MotionOffset( urcDir_t dir, float dirTime, int elapsedMs, float parentW, float parentH, float *dx, float *dy ) {
	float	frac;

	*dx = *dy = 0;
	if ( dir == DIR_NONE || dirTime <= 0 ) {
		return;
	}
	frac = 1.0f - elapsedMs / ( dirTime * 1000.0f );
	if ( frac <= 0 ) {
		return;
	}
	switch ( dir ) {
	case DIR_FROM_LEFT:		*dx = -frac * parentW; break;
	case DIR_FROM_RIGHT:	*dx = frac * parentW; break;
	case DIR_FROM_TOP:		*dy = -frac * parentH; break;
	case DIR_FROM_BOTTOM:	*dy = frac * parentH; break;
	default: break;
	}
}

static float URC_FadeAlpha( float fadein, int elapsedMs ) {
	if ( fadein <= 0 ) {
		return 1;
	}
	if ( elapsedMs >= fadein * 1000 ) {
		return 1;
	}
	return elapsedMs / ( fadein * 1000.0f );
}

static int URC_PlayerStat( int stat ) {
	if ( stat < 0 || stat >= MAX_STATS || !cl.snap.valid ) {
		return 0;
	}
	return cl.snap.ps.stats[stat];
}

/*
=================
URC_DrawWidget
=================
*/
static void URC_DrawWidget( urcWidget_t *w, const urcMenu_t *menu, float ox, float oy, int menuElapsed, float menuAlpha, qboolean onTop ) {
	float		x, y, wd, ht, alpha, rect[4];
	vec4_t		color;
	qhandle_t	shader;

	URC_LoadWidgetShaders( w );
	URC_WidgetRect( w, menu, ox, oy, menuElapsed, rect );
	alpha = menuAlpha * URC_FadeAlpha( w->fadein, menuElapsed );
	x = rect[0];
	y = rect[1];
	wd = rect[2];
	ht = rect[3];

	Vector4Copy( w->bgcolor, color );
	color[3] *= alpha;
	URC_FillRect( x, y, wd, ht, color );

	// pictures take the widget's foreground colour: the menu shaders use
	// "rgbGen global" and "alphaGen global" for it, the HUD's use identity
	Vector4Copy( w->fgcolor, color );
	color[3] *= alpha;
	if ( w->tileShader ) {
		float	s1, t1;

		URC_TileST( w->tileShader, wd, ht, &s1, &t1 );
		URC_DrawPicST( x, y, wd, ht, 0, 0, s1, t1, w->tileShader, color );
	}
	shader = w->shader;
	if ( onTop && w->hoverShader && ( w - urc_widgets ) == urc_hoverWidget ) {
		shader = w->hoverShader;
	}
	if ( w->cls == WC_CHECKBOX ) {
		// a 16x16 box at the left of the rect (the images are 16x16); the
		// caption beside it is another widget's picture
		float	box = 16;

		shader = ( w->linkcvar[0] && Cvar_VariableValue( w->linkcvar ) ) ? w->checkedShader : w->uncheckedShader;
		if ( shader ) {
			URC_DrawPicST( x + 2, y + ( ht - box ) * 0.5f, box, box, 0, 0, 1, 1, shader, color );
		}
		shader = 0;
	} else if ( w->cls == WC_SLIDER ) {
		// the track is the shader; the thumb (8x16 images) sits at the value
		float		thumbW = 8, frac = 0;
		qhandle_t	thumb;

		if ( !urc_sliderThumb ) {
			urc_sliderThumb = re.RegisterShaderNoMip( "textures/menu/slider_thumb" );
			urc_sliderThumbSel = re.RegisterShaderNoMip( "textures/menu/slider_thumb_sel" );
		}
		if ( shader ) {
			URC_DrawPicST( x, y, wd, ht, 0, 0, 1, 1, shader, color );
		}
		if ( w->linkcvar[0] && w->rangeMax > w->rangeMin ) {
			frac = ( Cvar_VariableValue( w->linkcvar ) - w->rangeMin ) / ( w->rangeMax - w->rangeMin );
			if ( frac < 0 ) frac = 0;
			if ( frac > 1 ) frac = 1;
		}
		thumb = ( onTop && ( ( w - urc_widgets ) == urc_hoverWidget || ( w - urc_widgets ) == urc_dragSlider ) ) ?
			urc_sliderThumbSel : urc_sliderThumb;
		if ( thumb ) {
			URC_DrawPicST( x + frac * ( wd - thumbW ), y, thumbW, ht, 0, 0, 1, 1, thumb, color );
		}
		shader = 0;
	} else if ( w->cls == WC_PULLDOWN ) {
		shader = ( onTop && w->selMenuShader && ( ( w - urc_widgets ) == urc_hoverWidget || ( w - urc_widgets ) == urc_popupWidget ) ) ?
			w->selMenuShader : w->menuShader;
	}
	if ( shader ) {
		URC_DrawPicST( x, y, wd, ht, 0, 0, 1, 1, shader, color );
	}
	if ( w->bindFile[0] ) {
		URC_DrawBindList( w, x, y, alpha );
		return;
	}

	// a stat bar fills from the bottom or the left with the stat's share
	// of its range; it flashes with the other shader just after a change
	if ( w->statbar && w->playerstat >= 0 ) {
		int			stat = URC_PlayerStat( w->playerstat );
		float		max = w->maxplayerstat >= 0 ? URC_PlayerStat( w->maxplayerstat ) : w->statMax;
		float		frac;
		qhandle_t	barShader;

		if ( stat != w->lastStat ) {
			if ( w->lastStat != -99999 ) {
				w->statChangeTime = cls.realtime;
			}
			w->lastStat = stat;
		}
		frac = max > w->statMin ? ( stat - w->statMin ) / ( max - w->statMin ) : 0;
		if ( frac < 0 ) frac = 0;
		if ( frac > 1 ) frac = 1;
		barShader = ( w->statFlash && cls.realtime - w->statChangeTime < 500 ) ? w->statFlash :
			( w->statTile ? w->statTile : w->statShader );
		if ( barShader && frac > 0 ) {
			// a "statbar_tileshader" is laid over the widget at the image's own
			// size from the top left (the health and water gauges are 64x128
			// images whose arcs fill the top 72 rows of a 64x72 widget) and the
			// drained part is cut off; a "statbar_shader" is stretched over it
			float	s1 = 1, t1 = 1;

			if ( barShader != w->statShader ) {
				URC_TileST( barShader, wd, ht, &s1, &t1 );
			}
			if ( w->statbar == 1 ) {
				float	fh = ht * frac;

				URC_DrawPicST( x, y + ht - fh, wd, fh, 0, t1 * ( 1 - frac ), s1, t1, barShader, color );
			} else {
				URC_DrawPicST( x, y, wd * frac, ht, 0, 0, s1 * frac, t1, barShader, color );
			}
		}
	}

	// the HUD's weapon windows: the wielded item's model (cl_inv.c)
	if ( w->invmodelrender >= 0 ) {
		INV_DrawHudModel( w->invmodelrender, x, y, wd, ht, alpha );
	}

	// text: a label with a cvar shows its value (through its linkstrings);
	// otherwise the title. Check boxes, sliders and pulldowns show none.
	if ( w->cls == WC_LABEL || w->cls == WC_BUTTON || w->cls == WC_FIELD || w->cls == WC_LIST || w->cls == WC_OTHER ) {
		const char	*text = w->title;

		if ( w->linkcvar[0] ) {
			int		i;

			text = Cvar_VariableString( w->linkcvar );
			for ( i = 0 ; i < w->numLinkStrings ; i++ ) {
				if ( !Q_stricmp( w->linkStrings[i].value, text ) ||
					( atof( w->linkStrings[i].value ) == atof( text ) && Q_isanumber( text ) ) ) {
					text = w->linkStrings[i].text;
					break;
				}
			}
		}
		if ( text[0] && w->invmodelrender < 0 && w->fgcolor[3] > 0 ) {
			urcFont_t	*font = URC_LoadFont( w->font );

			if ( font ) {
				float	tw = URC_StringWidth( font, text );

				Vector4Copy( w->fgcolor, color );
				color[3] *= alpha;
				URC_DrawString( font, x + ( wd - tw ) * 0.5f, y + ( ht - font->height ) * 0.5f, text, color );
			}
		}
	}
}

/*
=================
URC_DrawMenu
=================
*/
static void URC_DrawMenu( urcMenu_t *menu, qboolean onTop ) {
	float	ox, oy, alpha;
	int		elapsed, i;
	vec4_t	color;

	elapsed = cls.realtime - menu->showTime;
	if ( menu->hasBgfill ) {
		URC_FillRect( 0, 0, 640, 480, menu->bgfill );
	}
	URC_MenuOrigin( menu, elapsed, &ox, &oy );
	alpha = URC_FadeAlpha( menu->fadein, elapsed );

	Vector4Copy( menu->bgcolor, color );
	color[3] *= alpha;
	URC_FillRect( ox, oy, menu->width, menu->height, color );

	for ( i = 0 ; i < menu->numWidgets ; i++ ) {
		URC_DrawWidget( &urc_widgets[menu->firstWidget + i], menu, ox, oy, elapsed, alpha, onTop );
	}
}

/*
=======================================================================

MENU STACK AND HUD

=======================================================================
*/

static void URC_ShowMenu( urcMenu_t *menu ) {
	if ( menu && !menu->visible ) {
		menu->visible = qtrue;
		menu->showTime = cls.realtime;
	}
}

static void URC_HideMenu( urcMenu_t *menu ) {
	if ( menu ) {
		menu->visible = qfalse;
	}
}

static void URC_UpdateCatcher( void ) {
	if ( urc_stackDepth ) {
		Key_SetCatcher( Key_GetCatcher() | KEYCATCH_UI );
	} else {
		Key_SetCatcher( Key_GetCatcher() & ~KEYCATCH_UI );
	}
}

qboolean URC_HasMenus( void ) {
	return urc_initialised && urc_stackDepth > 0;
}

qboolean URC_IsFullscreen( void ) {
	return urc_initialised && urc_stackDepth > 0 && urc_menus[urc_stack[urc_stackDepth - 1]].fullscreen;
}

void URC_PushMenu( const char *name ) {
	urcMenu_t	*menu = URC_FindMenu( name );
	int			i;

	if ( !menu ) {
		Com_Printf( "Couldn't find menu %s\n", name );
		return;
	}
	if ( urc_loading ) {
		return;		// the retail pushmenu (0x40bf90) does nothing while a map loads
	}
	for ( i = 0 ; i < urc_stackDepth ; i++ ) {
		if ( &urc_menus[urc_stack[i]] == menu ) {
			return;		// already up
		}
	}
	if ( urc_stackDepth == URC_MAX_STACK ) {
		return;
	}
	urc_stack[urc_stackDepth++] = menu - urc_menus;
	URC_ShowMenu( menu );
	URC_UpdateCatcher();
}

void URC_PopMenu( void ) {
	if ( !urc_stackDepth ) {
		return;
	}
	URC_HideMenu( &urc_menus[urc_stack[--urc_stackDepth]] );
	urc_popupWidget = -1;
	urc_dragSlider = -1;
	urc_bindList.waitingSlot = -1;
	URC_UpdateCatcher();
}

void URC_ClearMenus( void ) {
	while ( urc_stackDepth ) {
		URC_PopMenu();
	}
}

/*
=======================================================================

LOADING SCREEN

=======================================================================
*/

/*
=================
URC_BeginLoading / URC_EndLoading

The retail CL_MapLoading (0x4207f0) calls UI_BeginLoad (0x40d520), which
sets the loading flag, and pops every menu (0x4a29f0) before its first
screen update. UI_EndLoad (0x40d4f0) is called when the first active
snapshot makes the client active (0x41d520), when a server restart's
snapshot arrives (0x423450), and by Com_Error's disconnect (0x4209b0).
=================
*/
void URC_BeginLoading( void ) {
	if ( !urc_initialised ) {
		return;
	}
	URC_ClearMenus();
	urc_loading = qtrue;
}

void URC_EndLoading( void ) {
	urc_loading = qfalse;
}

/*
=================
URC_DrawLoading

The retail update (0x40af50) while the loading flag is set and no menu is
up (or the one on top is not fullscreen: retail tests a field of it): the
screen is filled black (0x40acd0), then textures/menu/loading, registered
at UI_Init (0x40e9d0), is drawn in white at its own pixel size, centred on
the screen. The HUD menus are hidden and the prints are not drawn.
=================
*/
static void URC_DrawLoading( void ) {
	float	w, h;

	if ( !urc_loading || URC_IsFullscreen() ) {
		return;
	}
	re.SetColor( colorBlack );
	re.DrawBox( 0, 0, cls.glconfig.vidWidth, cls.glconfig.vidHeight );
	re.SetColor( NULL );
	if ( !urc_loadingShader ) {
		return;
	}
	w = re.GetShaderWidth( urc_loadingShader );
	h = re.GetShaderHeight( urc_loadingShader );
	re.SetColor( colorWhite );
	re.DrawStretchPic( ( cls.glconfig.vidWidth - w ) * 0.5f, ( cls.glconfig.vidHeight - h ) * 0.5f,
		w, h, 0, 0, 1, 1, urc_loadingShader );
	re.SetColor( NULL );
}

/*
=================
URC_UpdateHud

The retail update (0x40af50): everything hidden unless ui_hud is set, a
map is active, no menu is up and the game is not in a cinematic; then
healthhud, weaphud or dualweaphud by the dual-weapon item slot, and
bosshud while the boss-health stat is at least 1.
=================
*/
static void URC_UpdateHud( void ) {
	qboolean	on;

	on = ui_hud->integer && clc.state == CA_ACTIVE && cl.snap.valid && !urc_stackDepth &&
		!cl.snap.ps.stats[URC_STAT_CINEMATIC];
	if ( !on ) {
		URC_HideMenu( urc_healthhud );
		URC_HideMenu( urc_weaphud );
		URC_HideMenu( urc_dualweaphud );
		URC_HideMenu( urc_bosshud );
		return;
	}
	URC_ShowMenu( urc_healthhud );
	if ( cl.snap.ps.activeItems[URC_ITEM_WEAPON_DUAL] < 0 ) {
		URC_ShowMenu( urc_weaphud );
		URC_HideMenu( urc_dualweaphud );
	} else {
		URC_ShowMenu( urc_dualweaphud );
		URC_HideMenu( urc_weaphud );
	}
	if ( cl.snap.ps.stats[URC_STAT_BOSSHEALTH] >= 1 ) {
		URC_ShowMenu( urc_bosshud );
	} else {
		URC_HideMenu( urc_bosshud );
	}
}

/*
=======================================================================

PRINTS

=======================================================================
*/

static void URC_SetPrint( urcPrint_t *pr, qboolean located, int x, int y, const char *text, float alpha ) {
	Com_Memset( pr, 0, sizeof( *pr ) );
	pr->active = qtrue;
	pr->located = located;
	pr->x = x;
	pr->y = y;
	pr->alpha = alpha;
	pr->startTime = cls.realtime;
	if ( text[0] == '@' ) {
		pr->shader = re.RegisterShaderNoMip( text + 1 );
		Q_strncpyz( pr->text, text + 1, sizeof( pr->text ) );
	} else {
		Q_strncpyz( pr->text, text, sizeof( pr->text ) );
	}
	if ( ui_urcdebug && ui_urcdebug->integer ) {
		Com_Printf( "%s: %s\n", located ? va( "locprint %d %d", x, y ) : "centerprint", text );
	}
}

void URC_CenterPrint( const char *text, float alpha ) {
	if ( !urc_initialised ) {
		return;
	}
	URC_SetPrint( &urc_centerprint, qfalse, 320, 160, text, alpha );
}

void URC_LocationPrint( int x, int y, const char *text, float alpha ) {
	if ( !urc_initialised ) {
		return;
	}
	URC_SetPrint( &urc_locprint, qtrue, x, y, text, alpha );
}

static void URC_DrawPrint( urcPrint_t *pr ) {
	int			elapsed;
	float		alpha;
	vec4_t		color;
	urcFont_t	*font;
	const char	*line;
	float		y;

	if ( !pr->active ) {
		return;
	}
	elapsed = cls.realtime - pr->startTime;
	if ( elapsed >= URC_PRINT_TIME ) {
		pr->active = qfalse;
		return;
	}
	alpha = pr->alpha;
	if ( elapsed < URC_PRINT_FADEIN ) {
		alpha *= elapsed / (float)URC_PRINT_FADEIN;
	} else if ( elapsed > URC_PRINT_HOLD ) {
		alpha *= ( URC_PRINT_TIME - elapsed ) / (float)URC_PRINT_FADE;
	}
	Vector4Set( color, 1, 1, 1, alpha );
	if ( pr->shader ) {
		float	w = 256, h = 128;		// the image's own size is not known here

		URC_DrawPicST( 320 - w * 0.5f, 240 - h * 0.5f, w, h, 0, 0, 1, 1, pr->shader, color );
		return;
	}
	font = URC_LoadFont( URC_PRINT_FONT );
	if ( !font ) {
		return;
	}
	// retail captures: a centre print is centred on the screen, line by
	// line; a location print's x is its left edge and its y is measured up
	// from the bottom of the screen to the bottom of its first line, the
	// other lines following below
	if ( pr->located ) {
		y = 480 - pr->y - font->height;
	} else {
		int		lines = 1;

		for ( line = pr->text ; *line ; line++ ) {
			lines += ( *line == '\n' );
		}
		y = 240 - lines * font->height * 0.5f;
	}
	for ( line = pr->text ; *line ; ) {
		float	w = URC_StringWidth( font, line );

		URC_DrawString( font, pr->located ? pr->x : 320 - w * 0.5f, y, line, color );
		y += font->height;
		while ( *line && *line != '\n' ) {
			line++;
		}
		if ( *line == '\n' ) {
			line++;
		}
	}
}

/*
=======================================================================

FRAME

=======================================================================
*/

/*
=================
URC_DrawFade

The script's fadein/fadeout and letterbox reach the client in the player
state (the game's player.cpp: ps.blend, STAT_ADDFADE, STAT_LETTERBOX); the
SDK cgame never draws them, so the retail engine does, under the prints and
the menus. The letterbox stat is the fraction of the screen each bar
covers (the game's default is 1/8); the bar heights are not read from
retail.
=================
*/
static void URC_DrawFade( void ) {
	const playerState_t	*ps = &cl.snap.ps;
	vec4_t				color;
	float				frac;

	if ( clc.state != CA_ACTIVE || !cl.snap.valid ) {
		return;
	}
	if ( ps->blend[3] > 0 ) {
		Vector4Copy( ps->blend, color );
		if ( color[3] > 1 ) {
			color[3] = 1;
		}
		if ( ps->stats[URC_STAT_ADDFADE] && urc_addFadeShader ) {
			URC_DrawPicST( 0, 0, 640, 480, 0, 0, 1, 1, urc_addFadeShader, color );
		} else {
			URC_FillRect( 0, 0, 640, 480, color );
		}
	}
	frac = ps->stats[URC_STAT_LETTERBOX] / (float)URC_MAX_LETTERBOX_SIZE;
	if ( frac > 0 ) {
		float	h = frac * 480;

		if ( h > 240 ) {
			h = 240;
		}
		URC_FillRect( 0, 0, 640, h, colorBlack );
		URC_FillRect( 0, 480 - h, 640, h, colorBlack );
	}
}

/*
=================
URC_Draw

After the game's scene and the map viewer, before the console.
=================
*/
void URC_Draw( void ) {
	int		i;

	if ( !urc_initialised ) {
		return;
	}

	// leaving the menu for a map drops the menus, as the retail client's
	// map load does; disconnecting brings the main menu back
	if ( clc.state != urc_lastState ) {
		if ( urc_lastState == CA_DISCONNECTED && clc.state != CA_DISCONNECTED ) {
			URC_ClearMenus();
		}
		urc_lastState = clc.state;
	}
	if ( clc.state == CA_DISCONNECTED && !urc_stackDepth && !CL_Viewer_Active() &&
		!( Key_GetCatcher() & KEYCATCH_CONSOLE ) && URC_FindMenu( "main" ) ) {
		URC_PushMenu( "main" );
		// the retail client's return to the menu (0x4212f0) puts on
		// music/intro.mus, the title theme, when no map is active
		MUSIC_NewSoundtrack( "music/intro" );
		MUSIC_UpdateMood( 1, 1 );		// mood_normal
	}

	// the retail update (0x40af50) pauses the game while a menu is up, unless
	// it was paused already, and lifts the pause it put on when the last menu
	// closes with the game active (the pause itself refuses without a local
	// server, so the main menu of a game that is not running changes nothing)
	if ( urc_stackDepth ) {
		if ( !urc_pausedByMenu ) {
			if ( !cl_paused->integer ) {
				Com_Pause();
				urc_pausedByMenu = 1;
			} else {
				urc_pausedByMenu = 2;
			}
		}
	} else if ( clc.state == CA_ACTIVE && urc_pausedByMenu ) {
		if ( urc_pausedByMenu == 1 && cl_paused->integer ) {
			Com_Pause();
		}
		urc_pausedByMenu = 0;
	}

	URC_UpdateHud();
	URC_DrawLoading();
	URC_DrawFade();

	// the widget under the cursor, and its hover sound on arrival
	{
		int		hover = URC_WidgetUnderCursor();

		if ( hover != urc_hoverWidget ) {
			urc_hoverWidget = hover;
			if ( hover >= 0 ) {
				URC_PlaySound( urc_widgets[hover].hoverSound );
			}
		}
	}

	for ( i = 0 ; i < urc_numMenus ; i++ ) {
		urcMenu_t	*menu = &urc_menus[i];
		int			k;
		qboolean	stacked = qfalse;

		for ( k = 0 ; k < urc_stackDepth ; k++ ) {
			if ( urc_stack[k] == i ) {
				stacked = qtrue;
			}
		}
		if ( menu->visible && !stacked ) {
			URC_DrawMenu( menu, qfalse );
		}
	}
	for ( i = 0 ; i < urc_stackDepth ; i++ ) {
		URC_DrawMenu( &urc_menus[urc_stack[i]], i == urc_stackDepth - 1 );
	}
	URC_DrawPopup();
	INV_Draw();

	if ( clc.state == CA_ACTIVE ) {
		URC_DrawPrint( &urc_centerprint );
		URC_DrawPrint( &urc_locprint );
	}
}

/*
=================
URC_KeyEvent

Only while a menu is up: escape pops it, the left button works the
button or check box under the cursor. The other keys do nothing yet.
=================
*/
static void URC_SliderSet( urcWidget_t *w, const float rect[4] ) {
	float	thumbW = 8, frac, value;

	if ( !w->linkcvar[0] || w->rangeMax <= w->rangeMin ) {
		return;
	}
	frac = ( urc_cursorX - rect[0] - thumbW * 0.5f ) / ( rect[2] - thumbW );
	if ( frac < 0 ) frac = 0;
	if ( frac > 1 ) frac = 1;
	value = w->rangeMin + frac * ( w->rangeMax - w->rangeMin );
	if ( w->stepSize > 0 ) {
		value = w->rangeMin + floor( ( value - w->rangeMin ) / w->stepSize + 0.5f ) * w->stepSize;
	}
	if ( w->sliderInt ) {
		Cvar_Set( w->linkcvar, va( "%d", (int)value ) );
	} else {
		Cvar_Set( w->linkcvar, va( "%g", value ) );
	}
}

// the rect of a widget of the menu on top, this frame
static qboolean URC_TopWidgetRect( int index, float rect[4] ) {
	urcMenu_t	*menu;
	float		ox, oy;
	int			elapsed;

	if ( !urc_stackDepth || index < 0 ) {
		return qfalse;
	}
	menu = &urc_menus[urc_stack[urc_stackDepth - 1]];
	elapsed = cls.realtime - menu->showTime;
	URC_MenuOrigin( menu, elapsed, &ox, &oy );
	URC_WidgetRect( &urc_widgets[index], menu, ox, oy, elapsed, rect );
	return qtrue;
}

#define URC_POPUP_WIDTH		184

static void URC_PopupRect( int index, float rect[4] ) {
	float	arrow[4];

	URC_TopWidgetRect( index, arrow );
	rect[0] = arrow[0];
	rect[1] = arrow[1] + arrow[3];
	rect[2] = URC_POPUP_WIDTH;
	rect[3] = urc_widgets[index].numPopups * 16.0f;
}

static void URC_DrawPopup( void ) {
	urcWidget_t	*w;
	urcFont_t	*font;
	float		rect[4];
	vec4_t		color;
	int			i;

	if ( urc_popupWidget < 0 || !urc_stackDepth ) {
		return;
	}
	w = &urc_widgets[urc_popupWidget];
	URC_PopupRect( urc_popupWidget, rect );
	font = URC_LoadFont( w->font );
	for ( i = 0 ; i < w->numPopups ; i++ ) {
		float	ry = rect[1] + i * 16.0f;
		float	rowRect[4] = { rect[0], ry, rect[2], 16 };

		Vector4Set( color, 0, 0, 0, 0.85f );
		if ( URC_CursorIn( rowRect ) ) {
			Vector4Set( color, 0.25f, 0.25f, 0.5f, 0.95f );
		}
		URC_FillRect( rect[0], ry, rect[2], 16, color );
		if ( font ) {
			Vector4Set( color, 1, 1, 1, 1 );
			URC_DrawString( font, rect[0] + 4, ry + ( 16 - font->height ) * 0.5f, w->popups[i].text, color );
		}
	}
}

/*
=================
URC_KeyEvent

Only while a menu is up: escape pops it, the left button works the
button, check box, slider, pulldown or bind list under the cursor, the
wheel scrolls the bind list. The other keys do nothing.
=================
*/
void URC_KeyEvent( int key, qboolean down ) {
	float	rect[4];
	int		i;

	if ( !urc_stackDepth ) {
		return;
	}
	if ( !down ) {
		if ( key == K_MOUSE1 ) {
			urc_dragSlider = -1;
		}
		return;
	}

	// a bind list cell waiting for its key takes everything
	if ( URC_BindListKey( key ) ) {
		return;
	}

	// an open pulldown list takes the click
	if ( urc_popupWidget >= 0 ) {
		urcWidget_t	*w = &urc_widgets[urc_popupWidget];

		if ( key == K_MOUSE1 ) {
			URC_PopupRect( urc_popupWidget, rect );
			if ( URC_CursorIn( rect ) ) {
				i = (int)( ( urc_cursorY - rect[1] ) / 16 );
				if ( i >= 0 && i < w->numPopups ) {
					Cbuf_AddText( va( "%s\n", w->popups[i].command ) );
					URC_PlaySound( w->clickSound );
				}
			}
		}
		urc_popupWidget = -1;
		return;
	}

	if ( key == K_ESCAPE ) {
		URC_PopMenu();
		return;
	}
	if ( key == K_MWHEELUP || key == K_MWHEELDOWN ) {
		urc_bindList.scroll += ( key == K_MWHEELUP ) ? -1 : 1;
		return;
	}
	if ( key == K_MOUSE1 ) {
		i = URC_WidgetUnderCursor();
		if ( i >= 0 ) {
			urcWidget_t	*w = &urc_widgets[i];

			URC_TopWidgetRect( i, rect );
			switch ( w->cls ) {
			case WC_CHECKBOX:
				URC_PlaySound( w->clickSound );
				if ( w->linkcvar[0] ) {
					qboolean	on = Cvar_VariableValue( w->linkcvar ) == 0;

					Cvar_Set( w->linkcvar, on ? "1" : "0" );
					if ( on && w->checkedCommand[0] ) {
						Cbuf_AddText( va( "%s\n", w->checkedCommand ) );
					} else if ( !on && w->uncheckedCommand[0] ) {
						Cbuf_AddText( va( "%s\n", w->uncheckedCommand ) );
					}
				}
				break;
			case WC_SLIDER:
				urc_dragSlider = i;
				URC_SliderSet( w, rect );
				break;
			case WC_PULLDOWN:
				URC_PlaySound( w->clickSound );
				urc_popupWidget = w->numPopups ? i : -1;
				break;
			default:
				if ( w->bindFile[0] ) {
					URC_BindListClick( w, rect );
					break;
				}
				URC_PlaySound( w->clickSound );
				break;
			}
			if ( w->stuffcommand[0] ) {
				Cbuf_AddText( va( "%s\n", w->stuffcommand ) );
			}
		}
	}
}

/*
=================
CL_MouseAbsEvent

The system cursor, in window pixels, while a menu is up.
=================
*/
void CL_MouseAbsEvent( int x, int y ) {
	float	rect[4];

	if ( cls.glconfig.vidWidth > 0 && cls.glconfig.vidHeight > 0 ) {
		urc_cursorX = x * 640.0f / cls.glconfig.vidWidth;
		urc_cursorY = y * 480.0f / cls.glconfig.vidHeight;
	}
	if ( urc_dragSlider >= 0 && URC_TopWidgetRect( urc_dragSlider, rect ) ) {
		URC_SliderSet( &urc_widgets[urc_dragSlider], rect );
	}
}

/*
=======================================================================

COMMANDS

=======================================================================
*/

static void URC_PushMenu_f( void ) {
	if ( Cmd_Argc() < 2 ) {
		Com_Printf( "Usage: pushmenu <menuname>\n" );
		return;
	}
	URC_PushMenu( Cmd_Argv( 1 ) );
}

static void URC_PopMenu_f( void ) {
	URC_PopMenu();
}

static void URC_ShowMenu_f( void ) {
	if ( Cmd_Argc() < 2 ) {
		Com_Printf( "Usage: showmenu <menuname>\n" );
		return;
	}
	URC_ShowMenu( URC_FindMenu( Cmd_Argv( 1 ) ) );
}

static void URC_HideMenu_f( void ) {
	if ( Cmd_Argc() < 2 ) {
		Com_Printf( "Usage: hidemenu <menuname>\n" );
		return;
	}
	URC_HideMenu( URC_FindMenu( Cmd_Argv( 1 ) ) );
}

static void URC_ToggleMenu_f( void ) {
	urcMenu_t	*menu;
	const char	*name;
	int			i;

	// default.cfg binds escape to "togglemenu" alone: the main menu
	name = Cmd_Argc() < 2 ? "main" : Cmd_Argv( 1 );
	menu = URC_FindMenu( name );
	if ( !menu ) {
		Com_Printf( "Couldn't find menu %s\n", name );
		return;
	}
	for ( i = 0 ; i < urc_stackDepth ; i++ ) {
		if ( &urc_menus[urc_stack[i]] == menu ) {
			while ( urc_stackDepth > i ) {
				URC_PopMenu();
			}
			return;
		}
	}
	URC_PushMenu( menu->name );
}

static void URC_ListMenus_f( void ) {
	int		i;

	for ( i = 0 ; i < urc_numMenus ; i++ ) {
		urcMenu_t	*m = &urc_menus[i];

		Com_Printf( "%-20s %4.0fx%-4.0f %3d widgets %s%s (%s)\n", m->name, m->width, m->height, m->numWidgets,
			m->visible ? "visible " : "", m->fullscreen ? "fullscreen " : "", m->file );
	}
	Com_Printf( "%d menus, %d widgets, %d fonts; stack:", urc_numMenus, urc_numWidgets, urc_numFonts );
	for ( i = 0 ; i < urc_stackDepth ; i++ ) {
		Com_Printf( " %s", urc_menus[urc_stack[i]].name );
	}
	Com_Printf( "\n" );
}

static void URC_CenterPrint_f( void ) {
	if ( Cmd_Argc() < 2 ) {
		Com_Printf( "Usage: centerprint string [alpha]\n" );
		return;
	}
	URC_CenterPrint( Cmd_Argv( 1 ), Cmd_Argc() > 2 ? atof( Cmd_Argv( 2 ) ) : 1.0f );
}

static void URC_LocationPrint_f( void ) {
	if ( Cmd_Argc() < 4 ) {
		Com_Printf( "Usage: locationprint x y string [alpha]\n" );
		return;
	}
	URC_LocationPrint( atoi( Cmd_Argv( 1 ) ), atoi( Cmd_Argv( 2 ) ), Cmd_Argv( 3 ), Cmd_Argc() > 4 ? atof( Cmd_Argv( 4 ) ) : 1.0f );
}

/*
=======================================================================

INIT

=======================================================================
*/

static void URC_LoadAllMenus( void ) {
	char	**files;
	int		numFiles, i;

	urc_numMenus = urc_numWidgets = urc_numAliases = 0;
	urc_hoverWidget = urc_dragSlider = urc_popupWidget = -1;
	urc_sliderThumb = urc_sliderThumbSel = 0;
	Com_Memset( &urc_bindList, 0, sizeof( urc_bindList ) );
	urc_bindList.waitingSlot = -1;
	files = FS_ListFiles( "ui", ".urc", &numFiles );
	for ( i = 0 ; i < numFiles ; i++ ) {
		URC_LoadMenuFile( va( "ui/%s", files[i] ) );
	}
	FS_FreeFileList( files );
	Com_Printf( "%d menus from %d ui/*.urc files\n", urc_numMenus, numFiles );

	urc_healthhud = URC_FindMenu( "healthhud" );
	urc_weaphud = URC_FindMenu( "weaphud" );
	urc_dualweaphud = URC_FindMenu( "dualweaphud" );
	urc_bosshud = URC_FindMenu( "bosshud" );
}

static void URC_LoadMenu_f( void ) {
	// reload everything; the stack is dropped
	URC_ClearMenus();
	urc_numFonts = 0;
	URC_LoadAllMenus();
}

/*
=======================================================================

HELPERS FOR THE INVENTORY BAR (cl_inv.c)

=======================================================================
*/

float URC_TextWidth( const char *font, const char *text ) {
	urcFont_t	*f = URC_LoadFont( font );

	return f ? URC_StringWidth( f, text ) : 0;
}

float URC_DrawText( const char *font, float x, float y, const char *text, const vec4_t color ) {
	urcFont_t	*f = URC_LoadFont( font );

	return f ? URC_DrawString( f, x, y, text, color ) : 0;
}

void URC_DrawPic( float x, float y, float w, float h, qhandle_t shader, const vec4_t color ) {
	if ( shader ) {
		URC_DrawPicST( x, y, w, h, 0, 0, 1, 1, shader, color );
	}
}

void URC_PlayAliasSound( const char *name ) {
	URC_PlaySound( name );
}

void URC_Init( void ) {
	ui_hud = Cvar_Get( "ui_hud", "1", CVAR_ARCHIVE );
	ui_urcdebug = Cvar_Get( "ui_urcdebug", "0", CVAR_TEMP );
	Cvar_Get( "shortversion", "1.02", CVAR_ROM );	// the main menu's version label; the game data's version

	urc_numFonts = 0;
	urc_stackDepth = 0;
	Com_Memset( &urc_centerprint, 0, sizeof( urc_centerprint ) );
	Com_Memset( &urc_locprint, 0, sizeof( urc_locprint ) );
	urc_addFadeShader = re.RegisterShaderNoMip( URC_ADDFADE_SHADER );
	urc_loadingShader = re.RegisterShaderNoMip( URC_LOADING_SHADER );
	URC_LoadAllMenus();
	INV_Init();

	Cmd_AddCommand( "pushmenu", URC_PushMenu_f );
	Cmd_AddCommand( "forcemenu", URC_PushMenu_f );
	Cmd_AddCommand( "popmenu", URC_PopMenu_f );
	Cmd_AddCommand( "showmenu", URC_ShowMenu_f );
	Cmd_AddCommand( "hidemenu", URC_HideMenu_f );
	Cmd_AddCommand( "togglemenu", URC_ToggleMenu_f );
	Cmd_AddCommand( "listmenus", URC_ListMenus_f );
	Cmd_AddCommand( "loadmenu", URC_LoadMenu_f );
	Cmd_AddCommand( "centerprint", URC_CenterPrint_f );
	Cmd_AddCommand( "locationprint", URC_LocationPrint_f );
	urc_initialised = qtrue;
	urc_lastState = -1;
}

void URC_Shutdown( void ) {
	if ( !urc_initialised ) {
		return;
	}
	INV_Shutdown();
	URC_ClearMenus();
	Cmd_RemoveCommand( "pushmenu" );
	Cmd_RemoveCommand( "forcemenu" );
	Cmd_RemoveCommand( "popmenu" );
	Cmd_RemoveCommand( "showmenu" );
	Cmd_RemoveCommand( "hidemenu" );
	Cmd_RemoveCommand( "togglemenu" );
	Cmd_RemoveCommand( "listmenus" );
	Cmd_RemoveCommand( "loadmenu" );
	Cmd_RemoveCommand( "centerprint" );
	Cmd_RemoveCommand( "locationprint" );
	urc_initialised = qfalse;
	urc_numFonts = 0;		// shader handles die with the renderer
	urc_loadingShader = 0;
}
