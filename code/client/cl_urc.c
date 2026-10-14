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
starts with '@' names a shader to draw instead.

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
#define URC_ITEM_WEAPON_DUAL	4

#define URC_MAX_MENUS		64
#define URC_MAX_WIDGETS		1024
#define URC_MAX_FONTS		16
#define URC_MAX_STACK		16
#define URC_FONT_CHARS		256
#define URC_FONT_GLYPHS		256
#define URC_FONT_TEXWIDTH	256.0f
#define URC_DEFAULT_FONT	"verdana-14"
#define URC_PRINT_FONT		"facfont-20"
#define URC_PRINT_TIME		4000		// 0x418cc0 sets 4000.0
#define URC_PRINT_FADE		1000		// not read from retail

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
static urcAlias_t	urc_aliases[URC_MAX_ALIASES];
static int			urc_numAliases;
static float		urc_cursorX = 320, urc_cursorY = 240;	// 640x480 units
static int			urc_hoverWidget = -1;

static urcMenu_t	*urc_healthhud, *urc_weaphud, *urc_dualweaphud, *urc_bosshud;

static cvar_t		*ui_hud;
static cvar_t		*ui_urcdebug;

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
	"linkstring", "additem", "addpopup",
	"setrange", "slidertype", "stepsize", "initdata",
	"menushader", "selmenushader", "updatecvar", "filename", "edit", "checked_command",
	"unchecked_command", "bordercolor", "noparentclip", "noparentadjust", "hidecommand",
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

		if ( w->cls != WC_BUTTON && w->cls != WC_CHECKBOX ) {
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
		URC_DrawPicST( x, y, wd, ht, 0, 0, 1, 1, w->tileShader, color );
	}
	shader = w->shader;
	if ( w->cls == WC_CHECKBOX && w->linkcvar[0] ) {
		shader = Cvar_VariableValue( w->linkcvar ) ? w->checkedShader : w->uncheckedShader;
	}
	if ( onTop && w->hoverShader && ( w - urc_widgets ) == urc_hoverWidget ) {
		shader = w->hoverShader;
	}
	if ( shader ) {
		URC_DrawPicST( x, y, wd, ht, 0, 0, 1, 1, shader, color );
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
			if ( w->statbar == 1 ) {
				float	fh = ht * frac;

				URC_DrawPicST( x, y + ht - fh, wd, fh, 0, 1 - frac, 1, 1, barShader, color );
			} else {
				URC_DrawPicST( x, y, wd * frac, ht, 0, 0, frac, 1, barShader, color );
			}
		}
	}

	// text: the title, or the linked cvar's value
	{
		const char	*text = w->title;

		if ( !text[0] && w->linkcvar[0] && w->cls != WC_CHECKBOX && w->cls != WC_SLIDER ) {
			text = Cvar_VariableString( w->linkcvar );
		}
		if ( text[0] && w->invmodelrender < 0 ) {
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
	URC_UpdateCatcher();
}

void URC_ClearMenus( void ) {
	while ( urc_stackDepth ) {
		URC_PopMenu();
	}
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
	if ( elapsed > URC_PRINT_TIME - URC_PRINT_FADE ) {
		alpha *= ( URC_PRINT_TIME - elapsed ) / (float)URC_PRINT_FADE;
	}
	Vector4Set( color, 1, 1, 1, alpha );
	if ( pr->shader ) {
		float	w = 256, h = 128;		// the image's own size is not known here

		URC_DrawPicST( pr->x - w * 0.5f, pr->y, w, h, 0, 0, 1, 1, pr->shader, color );
		return;
	}
	font = URC_LoadFont( URC_PRINT_FONT );
	if ( !font ) {
		return;
	}
	y = pr->y;
	for ( line = pr->text ; *line ; ) {
		float	w = URC_StringWidth( font, line );

		URC_DrawString( font, pr->x - w * 0.5f, y, line, color );
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
	}

	URC_UpdateHud();

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
void URC_KeyEvent( int key, qboolean down ) {
	if ( !down || !urc_stackDepth ) {
		return;
	}
	if ( key == K_ESCAPE ) {
		URC_PopMenu();
		return;
	}
	if ( key == K_MOUSE1 ) {
		int		i = URC_WidgetUnderCursor();

		if ( i >= 0 ) {
			urcWidget_t	*w = &urc_widgets[i];

			URC_PlaySound( w->clickSound );
			if ( w->cls == WC_CHECKBOX && w->linkcvar[0] ) {
				Cvar_Set( w->linkcvar, Cvar_VariableValue( w->linkcvar ) ? "0" : "1" );
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
	if ( cls.glconfig.vidWidth > 0 && cls.glconfig.vidHeight > 0 ) {
		urc_cursorX = x * 640.0f / cls.glconfig.vidWidth;
		urc_cursorY = y * 480.0f / cls.glconfig.vidHeight;
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
	int			i;

	if ( Cmd_Argc() < 2 ) {
		Com_Printf( "Usage: togglemenu <menuname>\n" );
		return;
	}
	menu = URC_FindMenu( Cmd_Argv( 1 ) );
	if ( !menu ) {
		Com_Printf( "Couldn't find menu %s\n", Cmd_Argv( 1 ) );
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
	urc_hoverWidget = -1;
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

void URC_Init( void ) {
	ui_hud = Cvar_Get( "ui_hud", "1", CVAR_ARCHIVE );
	ui_urcdebug = Cvar_Get( "ui_urcdebug", "0", CVAR_TEMP );
	Cvar_Get( "shortversion", "1.02", CVAR_ROM );	// the main menu's version label; the game data's version

	urc_numFonts = 0;
	urc_stackDepth = 0;
	Com_Memset( &urc_centerprint, 0, sizeof( urc_centerprint ) );
	Com_Memset( &urc_locprint, 0, sizeof( urc_locprint ) );
	URC_LoadAllMenus();

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
}
