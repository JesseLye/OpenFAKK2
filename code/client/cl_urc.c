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
drawn with the facfont-20 font for 4 seconds (0x418cc0), and a centre
print's text that starts with '@' names a shader to draw instead. There is
one print: each replaces the one up, whichever kind (0x418cc0, 0x418ec0). While a map loads (from
CL_MapLoading, 0x4207f0, to the first active snapshot, 0x41d520) the update
fills the screen black and draws textures/menu/loading in the middle.

This is a fresh implementation of that: the file grammar is what the 31
shipped files use, and the fonts are the game's fonts/*.ritualfont (a text
file: height, aspect, 256 character indirections, and a glyph rectangle
list in texture pixels over a 256-wide image).

The menus are drawn as retail's widget library (uilib, in fakk2.exe) draws
them. "resource X" makes a UIX ("Label" is aliased to FakkLabel, 0x40cc40),
created with InitFrame(..., "verdana-12") by the loader (0x4aad60). Every
widget is drawn by UIWidget::Display (0x49ef30) inside its own frame (a
viewport, so what overruns it is cut off): the background colour, the
border, the material ("shader" stretched, "tileshader" tiled at the image's
size) in the foreground colour, the hover material over it, then the
class's Draw. Labels and buttons centre their text (UIFont::PrintJustified,
0x4b03c0); the renderer's DrawString (0x481690) advances each glyph by its
width and draws it the font's height tall, at whole-pixel positions. The
class draws are UISlider (0x4b64e0), UICheckBox (0x4bb6e0), UIButton
(0x4ae7e0), UIFakkBindList (0x4c1360, its rows built at 0x4c0390),
UIBindButton (0x4bc410), UIVertScroll (0x4b3ab0), UIListCtrl (0x4ace40) as
UIFAKKLoadGameClass (columns 0x414710, rows 0x4147d0, its picture
0x415390), UIPulldownMenu (0x4be190) and UIPopupMenu (0x4bf4a0).
===========================================================================
*/

#include "client.h"
#include "../qcommon/alias.h"

#include <sys/stat.h>
#include <time.h>

// the SDK's bg_public.h values the HUD menus read (code/fakk/fgame)
#define URC_STAT_BOSSHEALTH		13
#define URC_STAT_CINEMATIC		CL_STAT_CINEMATIC	// client.h: cl_keys.c reads it too
#define URC_STAT_ADDFADE		15			// ps.blend is added rather than blended
#define URC_STAT_LETTERBOX		16			// letterbox fraction * URC_MAX_LETTERBOX_SIZE
#define URC_MAX_LETTERBOX_SIZE	0x7fff
#define URC_ITEM_WEAPON_DUAL	4
#define URC_ADDFADE_SHADER		"textures/jack/whitefade"	// scripts/sunflare.shader: GL_SRC_ALPHA GL_ONE, fromClient

#define URC_MAX_MENUS		64
#define URC_MAX_WIDGETS		1024
#define URC_MAX_FONTS		16
#define URC_MAX_STACK		16
#define URC_MAX_ARG			256		// a key's argument: a stuffcommand or a pulldown's command can be long
#define URC_FONT_CHARS		256
#define URC_FONT_GLYPHS		256
#define URC_FONT_TEXWIDTH	256.0f
#define URC_DEFAULT_FONT	"verdana-12"	// the urc loader's InitFrame (0x4aad60)
#define URC_POPUP_FONT		"verdana-14"	// UIPopupMenu's Create (0x4bf470)
#define URC_SCROLL_FONT		"marlett"		// UIVertScroll's arrows: "5" up, "6" down (0x4b3ab0)
#define URC_PRINT_FONT		"facfont-20"
#define URC_PRINT_TIME		4000		// 0x418cc0 sets 4000.0; retail shows the intro's quote at 1 s and not at 4.5 s
#define URC_PRINT_FADE		750			// in and out (0x419480, 0x419770: 3250 and 750 of the 4000 left)

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
	WC_LABEL, WC_BUTTON, WC_CHECKBOX, WC_FIELD, WC_SLIDER, WC_LIST, WC_PULLDOWN,
	WC_BINDLIST, WC_LOADGAME, WC_MULTILINE, WC_OTHER
} urcClass_t;

// UIWidget's border styles (0xc4): "NONE", "RAISED", "INDENT_BORDER", "3D_BORDER"
typedef enum {
	BORDER_NONE, BORDER_RAISED, BORDER_INDENT, BORDER_3D
} urcBorder_t;

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
	urcBorder_t	borderStyle;
	char		font[64];
	char		shaderName[MAX_QPATH];		// the material: "shader" (stretched) or "tileshader" (tiled)
	qboolean	tiled;
	char		hoverShaderName[MAX_QPATH];
	char		statShaderName[MAX_QPATH];
	char		statTileName[MAX_QPATH];
	char		statFlashName[MAX_QPATH];
	char		checkedName[MAX_QPATH];
	char		uncheckedName[MAX_QPATH];
	qhandle_t	shader, hoverShader, statShader, statTile, statFlash, checkedShader, uncheckedShader;
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
	char		linkFormat[64];		// "linkformat <text>" (OpenFAKK2's): the label's text with %s for the value
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
	qboolean	videoModes;			// "popupsource videomodes" (OpenFAKK2's): the list is the window sizes (URC_BuildModeList)
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
	int			lineTop;			// a multi-line text's first line shown
	float		lastStatFrac;		// the stat bar's fill when it last drew, -1 before then
	int			statChangeTime;
	qboolean	disabled;			// UIWidget's "disable" (0x49e670): hidden, out of the cursor's reach
	char		linkSnapshot[MAX_CVAR_VALUE_STRING];	// the linked cvar's value when the menu came up (0x4a3a30)
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
	qboolean	hasAlignH;			// "align" named one: anchored to the window's side when it is wider than 4:3
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

// the one print the retail interface keeps (+0x160 to +0x178 of its root
// widget): a centre print and a location print each replace whatever print
// was up (0x418cc0, 0x418ec0)
typedef struct {
	qboolean	active;
	qboolean	located;			// +0x170
	int			x, y;				// +0x174, +0x178
	char		text[1024];			// +0x168
	qhandle_t	shader;				// +0x16c: a centre print's text beginning with '@'
	float		alpha;				// +0x164
	int			startTime;			// +0x160 counts the 4000 ms down
} urcPrint_t;

static urcMenu_t	urc_menus[URC_MAX_MENUS];
static int			urc_numMenus;
static urcWidget_t	urc_widgets[URC_MAX_WIDGETS];
static int			urc_numWidgets;
static urcFont_t	urc_fonts[URC_MAX_FONTS];
static int			urc_numFonts;
static int			urc_stack[URC_MAX_STACK];
static int			urc_stackDepth;
static urcPrint_t	urc_print;
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

// the intro's logos (UI_Init, 0x40e9d0), 256x256 pictures with no shader
#define URC_RITUAL_SHADER		"textures/menu/ritual"
#define URC_GATHERING_SHADER	"textures/menu/gathering"
#define URC_INTRO_SCALE			1.5f
static qhandle_t	urc_ritualShader, urc_gatheringShader;
static int			urc_introStep;				// the next step of the intro; 0 when none is running
static int			urc_introNextTime;			// cls.realtime of the next step
static qhandle_t	urc_introShader;			// the logo up, or none
static float		urc_introAlphaFrom, urc_introAlphaTo;
static int			urc_introFadeTime, urc_introFadeStart;
static qboolean		urc_introQueued;			// once a run: not again after a vid_restart
static qboolean		urc_introCommands;			// dointro and _intro outlive a UI restart

// the key bind list (FakkBindList), from its "filename" script
typedef struct {
	char		name[64];
	char		command[128];
	char		shader[MAX_QPATH];
	qhandle_t	shaderHandle;
	// the key cells' pictures: textures/bind/<key name>, or the empty key
	// with the name written over it; looked up again when the key changes
	int			cellKey[2];
	qhandle_t	cellShader[2];
	qboolean	cellNamed[2];
} urcBindItem_t;

#define URC_MAX_BINDITEMS	64

typedef struct {
	char			file[MAX_QPATH];
	qboolean		loaded;
	char			headerName[3][MAX_QPATH];
	qhandle_t		headerShader[3];
	int				fillWidth, cmdWidth, rowHeight, primWidth, primHeight, altWidth, altHeight;
	char			fillName[MAX_QPATH];
	qhandle_t		fillShader;
	vec4_t			titleFg, titleBg, activeFg, activeBg, activeBorder, inactiveFg, inactiveBg;
	vec4_t			highlightFg, highlightBg, selectFg, selectBg;
	char			changeSound[MAX_QPATH], activeSound[MAX_QPATH], enterSound[MAX_QPATH];
	int				numItems;
	urcBindItem_t	items[URC_MAX_BINDITEMS];
	// runtime: the selected cell (row 0.., column 1 the action, 2 primary,
	// 3 alternate; retail starts at row 0 column 2), the cell waiting for a
	// key (focus), the scroll position
	int				scroll;
	int				selRow, selCol;
	qboolean		waiting;
	int				scrollDrag;			// -1, or the thumb grab offset in rows
} urcBindList_t;

static urcBindList_t	urc_bindList;
static qhandle_t		urc_emptyKeyShader, urc_pressKeyShader;

// the saved games (FakkLoadGameClass), from save/<config>_*.ssv
typedef struct {
	char		file[MAX_QPATH];		// save/<config>_<name>.ssv
	char		name[64];				// <name>, as its header has it
	char		comment[64];
	int			mapTime;				// milliseconds
	int			time;					// when it was saved
} urcSave_t;

#define URC_MAX_SAVES		512

static urcSave_t	urc_saves[URC_MAX_SAVES];
static int			urc_numSaves;
static int			urc_saveSelected;		// 1.., 0 for none
static int			urc_saveTop;			// the first row shown
static int			urc_saveClickTime, urc_saveClickRow;
static int			urc_saveWidget = -1;	// the list's widget
static int			urc_scrollWidget = -1;	// the list whose bar the left button holds

// the slider pictures (UISlider's constructor, 0x4b4aa0)
static qhandle_t	urc_sliderThumb, urc_sliderThumbSel, urc_arrowLeft, urc_arrowLeftSel, urc_arrowRight, urc_arrowRightSel;
static int			urc_sliderPress = -1;	// the slider whose arrow is held: -1, else its index * 2 + right

// what draws are cut to: the frame of the widget being drawn, in 640x480 units
// (the whole window outside one, which reaches past 0 and 640 on a wide one)
static float		urc_clip[4] = { 0, 0, 640, 480 };	// x0 y0 x1 y1

static void URC_PlaySound( const char *name );
static void URC_DrawPopup( void );

static urcMenu_t	*urc_healthhud, *urc_weaphud, *urc_dualweaphud, *urc_bosshud;
static qboolean		urc_saveShot;		// drawing a saved game's picture

static qboolean		urc_hudOn = qtrue;		// "ui_hud", a command in retail (0x40e010) setting 0x4df7b8
static cvar_t		*ui_drawcoords;			// UI_Init (0x40e9df)
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
gfx/fonts/<name>, 256 pixels wide and 256/aspect high. This is what
Ritual's fontgen writes and what the retail loader (fakk2.exe 0x481190,
an undefined function) reads: it keeps a glyph as x/256, y*aspect/256,
w/256, h*aspect/256 and registers gfx/fonts/<name>.
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

/*
=================
URC_CharWidth, URC_StringWidth, URC_FontHeight

The retail metrics (UIFont, 0x4b04c0 and 0x4b0690): a glyph advances by its
own width, a tab by three spaces, a character without a glyph not at all;
the width of a text is that of its longest line, its height the font's
height per line.
=================
*/
static float URC_CharWidth( const urcFont_t *font, int c ) {
	int		idx;

	if ( !font ) {
		return 0;
	}
	if ( c == '\t' ) {
		return 3 * URC_CharWidth( font, ' ' );
	}
	idx = font->indirection[c & 0xff];
	if ( idx < 0 || idx >= font->numGlyphs ) {
		return 0;
	}
	return font->glyph[idx][2];
}

// the width of the first line
static float URC_StringWidth( const urcFont_t *font, const char *s ) {
	float	w = 0;

	for ( ; *s && *s != '\n' ; s++ ) {
		w += URC_CharWidth( font, *s );
	}
	return w;
}

// the width of the longest line
static float URC_TextBlockWidth( const urcFont_t *font, const char *s ) {
	float	w = 0, best = 0;

	for ( ; *s ; s++ ) {
		if ( *s == '\n' ) {
			w = 0;
			continue;
		}
		w += URC_CharWidth( font, *s );
		if ( w > best ) {
			best = w;
		}
	}
	return (int)best;
}

static float URC_FontHeight( const urcFont_t *font ) {
	return font ? (int)font->height : 16;
}

static void URC_DrawPicST( float x, float y, float w, float h, float s1, float t1, float s2, float t2, qhandle_t shader, const vec4_t color );

/*
=================
URC_DrawString

Draws one line (up to a newline) at x, y in 640x480 units as the retail
renderer's DrawString (0x481690) does: from a whole-pixel position (UIFont's
Print truncates it, 0x4b0360), each glyph its location's width wide and the
font's height tall, a carriage return going back to the start. Returns the
width drawn.
=================
*/
static float URC_DrawString( const urcFont_t *font, float x, float y, const char *s, const vec4_t color ) {
	float	texH, startX;

	if ( !font || !font->shader ) {
		return 0;
	}
	x = (int)x;
	y = (int)y;
	startX = x;
	texH = URC_FONT_TEXWIDTH / font->aspect;
	for ( ; *s && *s != '\n' ; s++ ) {
		int		idx;
		float	gx, gy, gw, gh;

		if ( *s == '\r' ) {
			x = startX;
			continue;
		}
		if ( *s == '\t' ) {
			x += URC_CharWidth( font, '\t' );
			continue;
		}
		idx = font->indirection[*s & 0xff];
		if ( idx < 0 || idx >= font->numGlyphs ) {
			continue;
		}
		gx = font->glyph[idx][0];
		gy = font->glyph[idx][1];
		gw = font->glyph[idx][2];
		gh = font->glyph[idx][3];
		URC_DrawPicST( x, y, gw, font->height, gx / URC_FONT_TEXWIDTH, gy / texH,
			( gx + gw ) / URC_FONT_TEXWIDTH, ( gy + gh ) / texH, font->shader, color );
		x += gw;
	}
	return x - startX;
}

// every line of a text, the next a font's height below
static void URC_DrawLines( const urcFont_t *font, float x, float y, const char *s, const vec4_t color ) {
	while ( 1 ) {
		URC_DrawString( font, x, y, s, color );
		while ( *s && *s != '\n' ) {
			s++;
		}
		if ( !*s ) {
			break;
		}
		s++;
		y += URC_FontHeight( font );
	}
}

/*
=================
URC_PrintJustified

UIFont::PrintJustified (0x4b03c0): the text in a rectangle, horizontally
0 centred, 1 at the left, 2 at the right; vertically 0 at the top, 1
centred, 2 at the bottom; by whole widths.
=================
*/
#define URC_JUSTIFY_CENTER	0
#define URC_JUSTIFY_LEFT	1
#define URC_JUSTIFY_RIGHT	2
#define URC_JUSTIFY_TOP		0
#define URC_JUSTIFY_MIDDLE	1
#define URC_JUSTIFY_BOTTOM	2

static void URC_PrintJustified( const urcFont_t *font, const float rect[4], int horz, int vert, const char *text, const vec4_t color ) {
	float	x, y, tw, th;
	int		lines = 1;
	const char	*s;

	if ( !font || !text[0] ) {
		return;
	}
	for ( s = text ; *s ; s++ ) {
		lines += ( *s == '\n' );
	}
	tw = URC_TextBlockWidth( font, text );
	th = URC_FontHeight( font ) * lines;
	switch ( horz ) {
	case URC_JUSTIFY_CENTER: x = rect[0] + ( rect[2] - tw ) * 0.5f; break;
	case URC_JUSTIFY_RIGHT: x = rect[0] + rect[2] - tw; break;
	default: x = rect[0]; break;
	}
	switch ( vert ) {
	case URC_JUSTIFY_MIDDLE: y = rect[1] + ( rect[3] - th ) * 0.5f; break;
	case URC_JUSTIFY_BOTTOM: y = rect[1] + rect[3] - th; break;
	default: y = rect[1]; break;
	}
	URC_DrawLines( font, x, y, text, color );
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
static int URC_LineArgs( char **p, char args[][URC_MAX_ARG], int maxArgs ) {
	int		n = 0;

	while ( 1 ) {
		char	*tok = COM_ParseExt( p, qfalse );

		if ( !tok[0] ) {
			break;
		}
		if ( n < maxArgs ) {
			Q_strncpyz( args[n], tok, URC_MAX_ARG );
		}
		n++;
	}
	return n < maxArgs ? n : maxArgs;
}

static void URC_ParseColor( char args[][URC_MAX_ARG], int n, vec4_t color ) {
	int		i;

	for ( i = 0 ; i < 4 ; i++ ) {
		color[i] = i < n ? atof( args[i] ) : ( i == 3 ? 1.0f : 0.0f );
	}
}

static void URC_ParseAlign( urcMenu_t *menu, char args[][URC_MAX_ARG], int n ) {
	int		i;

	for ( i = 0 ; i < n ; i++ ) {
		if ( !Q_stricmp( args[i], "top" ) ) menu->alignV = 0;
		else if ( !Q_stricmp( args[i], "bottom" ) ) menu->alignV = 2;
		else if ( !Q_stricmp( args[i], "centery" ) ) menu->alignV = 1;
		else {
			if ( !Q_stricmp( args[i], "left" ) ) menu->alignH = 0;
			else if ( !Q_stricmp( args[i], "right" ) ) menu->alignH = 2;
			else if ( !Q_stricmp( args[i], "centerx" ) || !Q_stricmp( args[i], "center" ) ) menu->alignH = 1;
			else continue;
			menu->hasAlignH = qtrue;
		}
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

static void URC_ParseWidgetKeys( urcWidget_t *w, char **p, const char *file );

/*
=================
URC_ParseWidget

After "resource <Class>": the { key args... } block.
=================
*/
static void URC_ParseWidget( urcMenu_t *menu, char **p, const char *className, const char *file ) {
	urcWidget_t	*w;
	char		*tok;

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
	else if ( !Q_stricmp( className, "FakkBindList" ) ) w->cls = WC_BINDLIST;
	else if ( !Q_stricmp( className, "FakkLoadGameClass" ) ) w->cls = WC_LOADGAME;
	else if ( !Q_stricmp( className, "MultiLineEdit" ) ) w->cls = WC_MULTILINE;
	else w->cls = WC_OTHER;
	// UIWidget's constructor (0x49d320): white on UGrey, an indented border
	Vector4Set( w->fgcolor, 1, 1, 1, 1 );
	Vector4Set( w->bgcolor, 0.75f, 0.75f, 0.75f, 1 );
	w->borderStyle = BORDER_INDENT;
	Q_strncpyz( w->font, URC_DEFAULT_FONT, sizeof( w->font ) );
	w->playerstat = w->maxplayerstat = w->invmodelrender = -1;
	w->statMin = 0;
	w->statMax = 100;
	w->lastStatFrac = -1;
	w->rangeMin = 0;
	w->rangeMax = 1;
	w->stepSize = 0;
	URC_ParseWidgetKeys( w, p, file );
}

/*
=================
URC_ParseWidgetKeys

A resource block's keys after its "{", up to its "}": for a new widget,
or for one an additions file changes.
=================
*/
static void URC_ParseWidgetKeys( urcWidget_t *w, char **p, const char *file ) {
	char		args[8][URC_MAX_ARG];
	char		key[64];
	char		*tok;
	int			n;

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
		} else if ( !Q_stricmp( tok, "borderstyle" ) && n ) {
			if ( !Q_stricmp( args[0], "RAISED" ) ) w->borderStyle = BORDER_RAISED;
			else if ( !Q_stricmp( args[0], "INDENT_BORDER" ) ) w->borderStyle = BORDER_INDENT;
			else if ( !Q_stricmp( args[0], "3D_BORDER" ) ) w->borderStyle = BORDER_3D;
			else w->borderStyle = BORDER_NONE;
		} else if ( !Q_stricmp( tok, "font" ) && n ) {
			Q_strncpyz( w->font, args[0], sizeof( w->font ) );
		} else if ( !Q_stricmp( tok, "shader" ) && n ) {
			Q_strncpyz( w->shaderName, args[0], sizeof( w->shaderName ) );
			w->tiled = qfalse;
		} else if ( !Q_stricmp( tok, "tileshader" ) && n ) {
			Q_strncpyz( w->shaderName, args[0], sizeof( w->shaderName ) );
			w->tiled = qtrue;
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
		} else if ( !Q_stricmp( tok, "linkformat" ) && n ) {
			Q_strncpyz( w->linkFormat, args[0], sizeof( w->linkFormat ) );
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
		} else if ( !Q_stricmp( tok, "popupsource" ) && n ) {
			w->videoModes = !Q_stricmp( args[0], "videomodes" );
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

static void URC_ParseMenuBody( urcMenu_t *menu, char **p, const char *file, qboolean included );

/*
=================
URC_ParseMenuBody

Menu-level keys and resource blocks up to "end.".
=================
*/
static void URC_ParseMenuBody( urcMenu_t *menu, char **p, const char *file, qboolean included ) {
	char	*tok;
	char	key[64];
	char	args[8][URC_MAX_ARG];
	int		n;

	while ( 1 ) {
		tok = COM_Parse( p );
		if ( !tok[0] || !Q_stricmp( tok, "end." ) || !Q_stricmp( tok, "end" ) ) {
			return;
		}
		if ( !Q_stricmp( tok, "resource" ) ) {
			char	className[64];

			Q_strncpyz( className, COM_Parse( p ), sizeof( className ) );
			URC_ParseWidget( menu, p, className, file );
			continue;
		}
		Q_strncpyz( key, tok, sizeof( key ) );		// the parser's buffer is about to be reused
		tok = key;
		n = URC_LineArgs( p, args, 8 );
		if ( !Q_stricmp( tok, "include" ) && n ) {
			char	*inc, *incp;

			if ( FS_ReadFile( args[0], (void **)&inc ) > 0 ) {
				incp = inc;
				URC_ParseMenuBody( menu, &incp, args[0], qtrue );
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
URC_LoadMenuText

ui/<file>.urc: "menu <name> <width> <height> <direction> <time>" then
the body. A menu is found by the name in the file, not the file name.
=================
*/
static urcMenu_t *URC_LoadMenuText( const char *path, char *buf ) {
	urcMenu_t	*menu;
	char		*p, *tok;
	int			i;

	p = buf;
	tok = COM_Parse( &p );
	if ( Q_stricmp( tok, "menu" ) ) {
		Com_Printf( S_COLOR_YELLOW "WARNING: %s does not begin with menu\n", path );
		return NULL;
	}
	if ( urc_numMenus == URC_MAX_MENUS ) {
		return NULL;
	}
	menu = &urc_menus[urc_numMenus];
	Com_Memset( menu, 0, sizeof( *menu ) );
	Q_strncpyz( menu->name, COM_Parse( &p ), sizeof( menu->name ) );
	for ( i = 0 ; i < urc_numMenus ; i++ ) {
		if ( !Q_stricmp( urc_menus[i].name, menu->name ) ) {
			Com_DPrintf( "%s: menu %s already loaded from %s\n", path, menu->name, urc_menus[i].file );
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
	URC_ParseMenuBody( menu, &p, path, qfalse );
	Com_DPrintf( "urc: %s: menu %s, %d widgets\n", path, menu->name, menu->numWidgets );
	return menu;
}

static urcMenu_t *URC_LoadMenuFile( const char *path ) {
	urcMenu_t	*menu;
	char		*buf;

	if ( FS_ReadFile( path, (void **)&buf ) <= 0 ) {
		return NULL;
	}
	menu = URC_LoadMenuText( path, buf );
	FS_FreeFile( buf );
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
=================
URC_LoadAdditions

OpenFAKK2's own grammar, for what it adds to the game's menus without
shipping changed copies of Ritual's files, any number of:

  "addto <menu>", then resources up to "end.": widgets added to it. A
  menu's widgets are kept together, so the new ones are moved in after
  its last and the menus loaded after it move up.

  "change <menu> <widget>", then a { key args... } block: keys read over
  that widget's, as if its own block ended with them. The widget is the
  first whose name, or else title, is <widget>.
=================
*/
static void URC_ChangeWidget( const char *path, char **p ) {
	char		menuName[64], widgetName[64];
	urcMenu_t	*menu;
	urcWidget_t	*w = NULL;
	int			i;

	Q_strncpyz( menuName, COM_Parse( p ), sizeof( menuName ) );
	Q_strncpyz( widgetName, COM_Parse( p ), sizeof( widgetName ) );
	if ( COM_Parse( p )[0] != '{' ) {
		Com_Printf( S_COLOR_YELLOW "WARNING: %s: expected { after change %s %s\n", path, menuName, widgetName );
		return;
	}
	menu = URC_FindMenu( menuName );
	for ( i = 0 ; menu && i < menu->numWidgets && !w ; i++ ) {
		if ( !Q_stricmp( urc_widgets[menu->firstWidget + i].name, widgetName ) ) {
			w = &urc_widgets[menu->firstWidget + i];
		}
	}
	for ( i = 0 ; menu && i < menu->numWidgets && !w ; i++ ) {
		if ( !Q_stricmp( urc_widgets[menu->firstWidget + i].title, widgetName ) ) {
			w = &urc_widgets[menu->firstWidget + i];
		}
	}
	if ( !w ) {
		// a widget the game data lacks: its keys are read and dropped
		SkipBracedSection( p, 1 );
		return;
	}
	URC_ParseWidgetKeys( w, p, path );
}

static void URC_LoadAdditions( const char *path, char *buf ) {
	urcMenu_t	*menu;
	urcWidget_t	*added;
	char		*p = buf, *tok;
	int			oldEnd, insertAt, count, i;

	while ( 1 ) {
		tok = COM_Parse( &p );
		if ( !tok[0] ) {
			return;
		}
		if ( !Q_stricmp( tok, "change" ) ) {
			URC_ChangeWidget( path, &p );
			continue;
		}
		if ( Q_stricmp( tok, "addto" ) ) {
			Com_Printf( S_COLOR_YELLOW "WARNING: %s: expected addto or change, found %s\n", path, tok );
			return;
		}
		menu = URC_FindMenu( COM_Parse( &p ) );
		if ( !menu ) {
			// a menu the game data lacks: its additions are read and dropped
			urcMenu_t	scratch;

			Com_Memset( &scratch, 0, sizeof( scratch ) );
			oldEnd = urc_numWidgets;
			URC_ParseMenuBody( &scratch, &p, path, qtrue );
			urc_numWidgets = oldEnd;
			continue;
		}
		oldEnd = urc_numWidgets;
		insertAt = menu->firstWidget + menu->numWidgets;
		URC_ParseMenuBody( menu, &p, path, qtrue );
		count = urc_numWidgets - oldEnd;
		if ( !count || insertAt == oldEnd ) {
			continue;
		}
		added = Z_Malloc( count * sizeof( *added ) );
		Com_Memcpy( added, &urc_widgets[oldEnd], count * sizeof( *added ) );
		memmove( &urc_widgets[insertAt + count], &urc_widgets[insertAt], ( oldEnd - insertAt ) * sizeof( *added ) );
		Com_Memcpy( &urc_widgets[insertAt], added, count * sizeof( *added ) );
		Z_Free( added );
		for ( i = 0 ; i < urc_numMenus ; i++ ) {
			if ( &urc_menus[i] != menu && urc_menus[i].firstWidget >= insertAt ) {
				urc_menus[i].firstWidget += count;
			}
		}
	}
}

// code/client/urc/*.urc, built in (cmake/client.cmake)
extern const char *urc_builtin_openfakk2;
extern const char *urc_builtin_additions;

/*
=================
URC_LoadBuiltinMenus

OpenFAKK2's options menu, after the game's files (so a ui/*.urc of the
same menu name takes its place), and the button the video and audio menu
gains for it.
=================
*/
static void URC_LoadBuiltinMenus( void ) {
	char	*buf;

	buf = CopyString( urc_builtin_openfakk2 );
	URC_LoadMenuText( "openfakk2.urc (built in)", buf );
	Z_Free( buf );
	buf = CopyString( urc_builtin_additions );
	URC_LoadAdditions( "additions.urc (built in)", buf );
	Z_Free( buf );
}

/*
=======================================================================

DRAWING

=======================================================================
*/

/*
=================
URC_PushClip / URC_PopClip

Retail draws each widget in its own viewport (UIWidget::Display sets it
from the widget's frame, 0x49ee80), so nothing it draws lands outside it:
the bind list's "MWHEELDOWN" is cut to "MWHEELDOW".
=================
*/
static void URC_PushClip( const float rect[4], float saved[4] ) {
	Com_Memcpy( saved, urc_clip, sizeof( urc_clip ) );
	if ( rect[0] > urc_clip[0] ) urc_clip[0] = rect[0];
	if ( rect[1] > urc_clip[1] ) urc_clip[1] = rect[1];
	if ( rect[0] + rect[2] < urc_clip[2] ) urc_clip[2] = rect[0] + rect[2];
	if ( rect[1] + rect[3] < urc_clip[3] ) urc_clip[3] = rect[1] + rect[3];
}

static void URC_PopClip( const float saved[4] ) {
	Com_Memcpy( urc_clip, saved, sizeof( urc_clip ) );
}

// a box cut to the clip rectangle, with the renderer's alpha-blended white
// (as SCR_FillRect: the game's "white" shader is additive)
static void URC_FillRect( float x, float y, float w, float h, const vec4_t color ) {
	float	x1 = x + w, y1 = y + h;

	if ( color[3] <= 0 ) {
		return;
	}
	if ( x < urc_clip[0] ) x = urc_clip[0];
	if ( y < urc_clip[1] ) y = urc_clip[1];
	if ( x1 > urc_clip[2] ) x1 = urc_clip[2];
	if ( y1 > urc_clip[3] ) y1 = urc_clip[3];
	if ( x1 <= x || y1 <= y ) {
		return;
	}
	w = x1 - x;
	h = y1 - y;
	re.SetColor( color );
	SCR_AdjustFrom640( &x, &y, &w, &h );
	re.DrawBox( x, y, w, h );
	re.SetColor( NULL );
}

// a box in a colour whose alpha is scaled (DrawBox, 0x49a230)
static void URC_Box( float x, float y, float w, float h, const vec4_t color, float alpha ) {
	vec4_t	c;

	Vector4Set( c, color[0], color[1], color[2], color[3] * alpha );
	URC_FillRect( x, y, w, h, c );
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

/*
=================
URC_EdgeTexels

One axis of a picture drawn from screen pixel x0 to x1: moves its texture
coordinates (c1, c2, in images of size texels) so that the first and last
pixels it covers sample no further out than the middle of the texel at
each end that lies on a texel boundary. Retail draws the UI at its size in
screen pixels, where every pixel samples a texel's middle; enlarged (or
placed between pixels), an edge pixel samples past it and filtering mixes
in the texel beyond, which on a "map" image is the opposite edge: the
water gauge's mask is white at the left and black at the right, so its
right edge showed a light line, and the weapon windows (weapon_left and
weapon_right, opaque where they meet and clear at their far sides) a seam
between them. A side cut off mid-texel (a clip, a drained stat bar) keeps
what its edge pixel samples; drawn at its size on whole pixels, nothing
changes.
=================
*/
static void URC_EdgeTexels( float *c1, float *c2, int size, float x0, float x1 ) {
	float	a = *c1 * size, b = *c2 * size, r, p0, p1, f0, f1;

	if ( size <= 0 || b - a <= 1 ) {
		return;
	}
	// the middles of the first and last pixels covered, and what they sample
	p0 = ceil( x0 - 0.5f ) + 0.5f;
	p1 = ceil( x1 - 0.5f ) - 0.5f;
	if ( p1 - p0 < 1 ) {
		return;
	}
	r = ( b - a ) / ( x1 - x0 );
	f0 = a + r * ( p0 - x0 );
	f1 = b - r * ( x1 - p1 );
	if ( fabs( a - floor( a + 0.5f ) ) < 0.001f && f0 < a + 0.5f ) {
		f0 = a + 0.5f;
	}
	if ( fabs( b - floor( b + 0.5f ) ) < 0.001f && f1 > b - 0.5f ) {
		f1 = b - 0.5f;
	}
	if ( f1 <= f0 ) {
		return;
	}
	r = ( f1 - f0 ) / ( p1 - p0 );
	*c1 = ( f0 - r * ( p0 - x0 ) ) / size;
	*c2 = ( f1 + r * ( x1 - p1 ) ) / size;
}

// a picture, cut to the clip rectangle with its texture coordinates
static void URC_DrawPicST( float x, float y, float w, float h, float s1, float t1, float s2, float t2, qhandle_t shader, const vec4_t color ) {
	float	d;
	int		iw = 0, ih = 0;

	if ( w <= 0 || h <= 0 ) {
		return;
	}
	if ( x < urc_clip[0] ) {
		d = urc_clip[0] - x;
		s1 += ( s2 - s1 ) * d / w;
		x += d;
		w -= d;
	}
	if ( x + w > urc_clip[2] ) {
		d = x + w - urc_clip[2];
		s2 -= ( s2 - s1 ) * d / w;
		w -= d;
	}
	if ( y < urc_clip[1] ) {
		d = urc_clip[1] - y;
		t1 += ( t2 - t1 ) * d / h;
		y += d;
		h -= d;
	}
	if ( y + h > urc_clip[3] ) {
		d = y + h - urc_clip[3];
		t2 -= ( t2 - t1 ) * d / h;
		h -= d;
	}
	if ( w <= 0 || h <= 0 ) {
		return;
	}
	re.SetColor( color );
	SCR_AdjustFrom640( &x, &y, &w, &h );
	if ( re.ShaderImageSize ) {
		re.ShaderImageSize( shader, &iw, &ih );
		URC_EdgeTexels( &s1, &s2, iw, x, x + w );
		URC_EdgeTexels( &t1, &t2, ih, y, y + h );
	}
	re.DrawStretchPic( x, y, w, h, s1, t1, s2, t2, shader );
	re.SetColor( NULL );
}

/*
=================
URC_DrawStatBar

One layer of a stat bar filled to frac (0x418270), vertical (1) from the
bottom or horizontal from the left. A tiled bar ("statbar_tileshader") lays
the image over the widget at its own size from the top left, as the
renderer's DrawTilePic (0x481c60) takes texture coordinates from the
position, and the drained part is cut off. A stretched one
("statbar_shader") squeezes the whole image into the filled width; retail
draws a vertical one from frac down, which no .urc uses, so it is cut off
from the top here instead.
=================
*/
static void URC_DrawStatBar( int statbar, qboolean tile, float x, float y, float w, float h, float frac, qhandle_t shader, const vec4_t color ) {
	float	s1 = 1, t1 = 1;

	if ( tile ) {
		URC_TileST( shader, w, h, &s1, &t1 );
	}
	if ( statbar == 1 ) {
		float	fh = h * frac;

		URC_DrawPicST( x, y + h - fh, w, fh, 0, t1 * ( 1 - frac ), s1, t1, shader, color );
	} else {
		URC_DrawPicST( x, y, w * frac, h, 0, 0, tile ? s1 * frac : 1, t1, shader, color );
	}
}

/*
=================
URC_BorderColors

What a background colour gives its border (UIWidget's colour setter,
0x49e800): [0] two thirds of it, [1] a third, [2] a lighter one (its HSV
saturation times 0.75 and value times 4/3), [3] the colour; all with its
alpha.
=================
*/
static void URC_BorderColors( const vec4_t bg, vec4_t out[4] ) {
	float	mx, mn, h, s, v, f, p, q, t;
	int		i;

	VectorScale( bg, 2.0f / 3.0f, out[0] );
	VectorScale( bg, 1.0f / 3.0f, out[1] );
	mx = MAX( bg[0], MAX( bg[1], bg[2] ) );
	mn = MIN( bg[0], MIN( bg[1], bg[2] ) );
	v = mx;
	s = mx > 0 ? ( mx - mn ) / mx : 0;
	if ( mx == mn ) {
		h = 0;
	} else if ( mx == bg[0] ) {
		h = ( bg[1] - bg[2] ) / ( mx - mn );
	} else if ( mx == bg[1] ) {
		h = 2 + ( bg[2] - bg[0] ) / ( mx - mn );
	} else {
		h = 4 + ( bg[0] - bg[1] ) / ( mx - mn );
	}
	if ( h < 0 ) {
		h += 6;
	}
	s *= 0.75f;
	v *= 4.0f / 3.0f;
	if ( v > 1 ) {
		v = 1;
	}
	i = (int)h % 6;
	f = h - (int)h;
	p = v * ( 1 - s );
	q = v * ( 1 - s * f );
	t = v * ( 1 - s * ( 1 - f ) );
	switch ( i ) {
	case 0: VectorSet( out[2], v, t, p ); break;
	case 1: VectorSet( out[2], q, v, p ); break;
	case 2: VectorSet( out[2], p, v, t ); break;
	case 3: VectorSet( out[2], p, q, v ); break;
	case 4: VectorSet( out[2], t, p, v ); break;
	default: VectorSet( out[2], v, p, q ); break;
	}
	VectorCopy( bg, out[3] );
	for ( i = 0 ; i < 4 ; i++ ) {
		out[i][3] = bg[3];
	}
}

/*
=================
URC_Draw3DBox

A raised or indented edge one pixel wide (0x49a260): raised has the light
colour along the top and left, two thirds along the inner bottom and
right and a third along the outer; indented the other way round.
=================
*/
static void URC_Draw3DBox( const float r[4], qboolean indent, vec4_t colors[4], float alpha ) {
	float		x = r[0], y = r[1], w = r[2], h = r[3];
	const float	*hi = indent ? colors[0] : colors[2];
	const float	*lo = indent ? colors[2] : colors[0];
	const float	*outer = colors[1];

	if ( indent ) {
		URC_Box( x, y, 1, h - 1, outer, alpha );
		URC_Box( x, y, w - 1, 1, outer, alpha );
		URC_Box( x + 1, y + 1, 1, h - 2, hi, alpha );
		URC_Box( x + 1, y + 1, w - 2, 1, hi, alpha );
		URC_Box( x + w - 1, y, 1, h, lo, alpha );
		URC_Box( x, y + h - 1, w, 1, lo, alpha );
		return;
	}
	URC_Box( x + w - 2, y + 1, 1, h - 2, lo, alpha );
	URC_Box( x + w - 1, y, 1, h, outer, alpha );
	URC_Box( x + 1, y + h - 2, w - 2, 1, lo, alpha );
	URC_Box( x, y + h - 1, w, 1, outer, alpha );
	URC_Box( x, y, 1, h - 1, hi, alpha );
	URC_Box( x, y, w - 1, 1, hi, alpha );
}

// the same inside a rectangle shrunk by one pixel, a light and a dark
// line only (0x49a4b0, the scroll bar's arrows and thumb)
static void URC_Draw3DBoxInset( const float r[4], qboolean indent, vec4_t colors[4], float alpha ) {
	float		x = r[0] + 1, y = r[1] + 1, w = r[2] - 2, h = r[3] - 2;
	const float	*hi = indent ? colors[0] : colors[2];
	const float	*lo = indent ? colors[2] : colors[0];

	URC_Box( x, y, w, 1, hi, alpha );
	URC_Box( x, y + 1, 1, h - 1, hi, alpha );
	URC_Box( x + w - 1, y + 1, 1, h - 1, lo, alpha );
	URC_Box( x + 1, y + h - 1, w - 2, 1, lo, alpha );
}

/*
=================
URC_DrawBorder

0x49a650: with flags & 1 the inside filled with one colour, with flags & 2
a border of the given thickness in the other.
=================
*/
static void URC_DrawBorder( const float r[4], const vec4_t fill, const vec4_t border, float t, int flags, float alpha ) {
	if ( flags & 1 ) {
		URC_Box( r[0] + t, r[1] + t, r[2] - 2 * t, r[3] - 2 * t, fill, alpha );
	}
	if ( t > 0 && ( flags & 2 ) ) {
		URC_Box( r[0], r[1], t, r[3] - t, border, alpha );
		URC_Box( r[0] + t, r[1], r[2] - t, t, border, alpha );
		URC_Box( r[0] + r[2] - t, r[1] + t, t, r[3] - t, border, alpha );
		URC_Box( r[0], r[1] + r[3] - t, r[2] - t, t, border, alpha );
	}
}

static void URC_LoadWidgetShaders( urcWidget_t *w ) {
	if ( w->shadersLoaded ) {
		return;
	}
	w->shadersLoaded = qtrue;
	if ( w->shaderName[0] ) w->shader = re.RegisterShaderNoMip( w->shaderName );
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

THE SCROLL BAR

UIVertScroll (0x4b3380), placed down the right edge of its owner, 16
pixels wide (InitFrameAlignRight, 0x4b4620). Its Draw (0x4b3ab0): its frame
filled with its background colour inside a black line; an arrow box at
each end (the background inside a black line, then while there is
something to scroll a one-pixel raised edge, pressed in while held, and
the marlett font's "5" or "6" centred in the foreground colour, or in two
thirds of the background when there is nothing to scroll); and, when
there is, the thumb (0x4b36b0): UDarkGrey inside a black line with a
raised edge, its height the shown share of the track between the arrows
(at least 6), placed by the first item shown.

=======================================================================
*/

#define URC_SCROLL_WIDTH	16

typedef enum {
	SCROLL_NONE, SCROLL_UP, SCROLL_DOWN, SCROLL_THUMB, SCROLL_PAGEUP, SCROLL_PAGEDOWN
} urcScrollPart_t;

// the scroll bar of a widget with this frame
static void URC_ScrollRect( const float owner[4], float r[4] ) {
	r[0] = owner[0] + owner[2] - URC_SCROLL_WIDTH;
	r[1] = owner[1];
	r[2] = URC_SCROLL_WIDTH;
	r[3] = owner[3];
}

// the thumb's place in the track, from the bar's top
static void URC_ScrollThumb( const float r[4], int numItems, int pageSize, int top, float *ty, float *th ) {
	float	track = r[3] - 2 * URC_SCROLL_WIDTH;

	*th = pageSize * track / numItems;
	if ( *th < 6 ) {
		*th = 6;
	}
	*ty = top * track / numItems;
	if ( *ty + *th > track ) {
		*ty = track - *th;
	}
	*ty += URC_SCROLL_WIDTH;
}

static void URC_DrawScrollArrow( const float bar[4], float y, const char *glyph, qboolean pressed, qboolean live, float alpha ) {
	static const vec4_t	bg = { 0.75f, 0.75f, 0.75f, 1 };		// a UIWidget's default background
	static const vec4_t	fg = { 1, 1, 1, 1 };
	static const vec4_t	black = { 0, 0, 0, 1 };
	vec4_t				colors[4], text;
	float				r[4];

	r[0] = bar[0];
	r[1] = y;
	r[2] = URC_SCROLL_WIDTH;
	r[3] = URC_SCROLL_WIDTH;
	URC_BorderColors( bg, colors );
	Vector4Copy( live ? fg : colors[0], text );
	text[3] *= alpha;
	URC_DrawBorder( r, bg, black, 1, 3, alpha );
	if ( live ) {
		URC_Draw3DBoxInset( r, pressed, colors, alpha );
	}
	URC_PrintJustified( URC_LoadFont( URC_SCROLL_FONT ), r, URC_JUSTIFY_CENTER, URC_JUSTIFY_MIDDLE, glyph, text );
}

static void URC_DrawScrollBar( const float owner[4], int numItems, int pageSize, int top, urcScrollPart_t held, float alpha ) {
	static const vec4_t	bg = { 0.75f, 0.75f, 0.75f, 1 };
	static const vec4_t	black = { 0, 0, 0, 1 };
	vec4_t				thumbColor, colors[4];
	float				r[4], t[4], saved[4];
	qboolean			live = numItems > pageSize;

	URC_ScrollRect( owner, r );
	URC_PushClip( r, saved );
	URC_DrawBorder( r, bg, black, 1, 3, alpha );
	URC_DrawScrollArrow( r, r[1], "5", held == SCROLL_UP, live, alpha );
	URC_DrawScrollArrow( r, r[1] + r[3] - URC_SCROLL_WIDTH, "6", held == SCROLL_DOWN, live, alpha );
	if ( live ) {
		URC_ScrollThumb( r, numItems, pageSize, top, &t[1], &t[3] );
		t[0] = r[0];
		t[1] += r[1];
		t[2] = r[2];
		Vector4Set( thumbColor, 0.5f, 0.5f, 0.5f, 1 );		// UDarkGrey
		if ( held == SCROLL_THUMB ) {
			thumbColor[0] -= 0.25f;
			thumbColor[1] -= 0.25f;
			thumbColor[2] -= 0.25f;
		}
		URC_BorderColors( thumbColor, colors );
		URC_DrawBorder( t, thumbColor, black, 1, 3, alpha );
		URC_Draw3DBoxInset( t, qfalse, colors, alpha );
	}
	URC_PopClip( saved );
}

// which part of the bar a point is on
static urcScrollPart_t URC_ScrollPart( const float owner[4], int numItems, int pageSize, int top, float x, float y ) {
	float	r[4], ty, th;

	URC_ScrollRect( owner, r );
	if ( x < r[0] || x >= r[0] + r[2] || y < r[1] || y >= r[1] + r[3] ) {
		return SCROLL_NONE;
	}
	if ( y < r[1] + URC_SCROLL_WIDTH ) {
		return SCROLL_UP;
	}
	if ( y >= r[1] + r[3] - URC_SCROLL_WIDTH ) {
		return SCROLL_DOWN;
	}
	if ( numItems <= pageSize ) {
		return SCROLL_NONE;
	}
	URC_ScrollThumb( r, numItems, pageSize, top, &ty, &th );
	if ( y < r[1] + ty ) {
		return SCROLL_PAGEUP;
	}
	if ( y >= r[1] + ty + th ) {
		return SCROLL_PAGEDOWN;
	}
	return SCROLL_THUMB;
}

// the first item shown for a thumb dragged so its top is at y
static int URC_ScrollTopAt( const float owner[4], int numItems, int pageSize, float y ) {
	float	r[4], track;
	int		top;

	URC_ScrollRect( owner, r );
	track = r[3] - 2 * URC_SCROLL_WIDTH;
	top = (int)( ( y - r[1] - URC_SCROLL_WIDTH ) * numItems / track + 0.5f );
	if ( top > numItems - pageSize ) {
		top = numItems - pageSize;
	}
	return top < 0 ? 0 : top;
}

static urcScrollPart_t	urc_scrollHeld;			// what the left button holds on a bind or save list's bar
static float			urc_scrollGrab;			// where on the thumb it was taken

/*
=======================================================================

THE KEY BIND LIST

"ui/bind.scr" (read by 0x412690): header <n> <text> <shader>,
bind_fillwidth <w> <shader>, bind_commandwidth/height,
bind_primarykeywidth/height, bind_alternatekeywidth/height, the colours,
changesound/activesound/entersound, binditem <name> <command> <shader>.

The retail list (UIFakkBindList, its children built at 0x4c0390) is a row
of header labels (the header pictures in the title colours, a raised
border) over, per item, a label (the item's picture) and two key buttons,
with tiled fill columns between them and a scroll bar down the right; it
shows as many rows as fit under the header ((height - row) / row). Its
Draw (0x4c1360) colours them each frame: the selected row in the highlight
colours, its selected cell in the select colours or, while that cell
waits for a key, the active ones; the other rows in the inactive colours.
A key button (UIBindButton, 0x4bc410) shows textures/bind/<key name>, or
when there is no such picture textures/bind/emptykey with the name
written over it; the cell waiting for its key has a two-pixel border in
the active border colour, and textures/menu/presskey is shown in the
middle of the screen (0x4c10e0).

=======================================================================
*/

static void URC_LoadBindList( const char *file ) {
	urcBindList_t	*bl = &urc_bindList;
	char			*buf, *p, *tok;
	char			key[64];
	char			args[8][URC_MAX_ARG];
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
	bl->primWidth = bl->altWidth = 64;
	bl->primHeight = bl->altHeight = 16;
	bl->selRow = 0;
	bl->selCol = 2;		// UIFakkBindList's constructor (0x4c00e0)
	bl->scrollDrag = -1;
	Vector4Set( bl->titleFg, 1, 1, 0, 1 );
	Vector4Set( bl->titleBg, 0, 0, 0, 1 );
	Vector4Set( bl->activeFg, 1, 1, 1, 1 );
	Vector4Set( bl->activeBg, 0, 0, 0, 1 );
	Vector4Set( bl->activeBorder, 1, 0, 0, 1 );
	Vector4Set( bl->inactiveFg, 1, 1, 1, 1 );
	Vector4Set( bl->inactiveBg, 0.1f, 0.1f, 0.1f, 1 );
	Vector4Set( bl->highlightFg, 0.5f, 0.5f, 1, 1 );
	Vector4Set( bl->highlightBg, 0, 0, 0, 1 );
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
			bl->primWidth = atoi( args[0] );
		} else if ( !Q_stricmp( key, "bind_primarykeyheight" ) && n ) {
			bl->primHeight = atoi( args[0] );
		} else if ( !Q_stricmp( key, "bind_alternatekeywidth" ) && n ) {
			bl->altWidth = atoi( args[0] );
		} else if ( !Q_stricmp( key, "bind_alternatekeyheight" ) && n ) {
			bl->altHeight = atoi( args[0] );
		} else if ( !Q_stricmp( key, "titleforegroundcolor" ) ) {
			URC_ParseColor( args, n, bl->titleFg );
		} else if ( !Q_stricmp( key, "titlebackgroundcolor" ) ) {
			URC_ParseColor( args, n, bl->titleBg );
		} else if ( !Q_stricmp( key, "activebordercolor" ) ) {
			URC_ParseColor( args, n, bl->activeBorder );
		} else if ( !Q_stricmp( key, "activeforegroundcolor" ) ) {
			URC_ParseColor( args, n, bl->activeFg );
		} else if ( !Q_stricmp( key, "activebackgroundcolor" ) ) {
			URC_ParseColor( args, n, bl->activeBg );
		} else if ( !Q_stricmp( key, "inactiveforegroundcolor" ) ) {
			URC_ParseColor( args, n, bl->inactiveFg );
		} else if ( !Q_stricmp( key, "inactivebackgroundcolor" ) ) {
			URC_ParseColor( args, n, bl->inactiveBg );
		} else if ( !Q_stricmp( key, "highlightforegroundcolor" ) ) {
			URC_ParseColor( args, n, bl->highlightFg );
		} else if ( !Q_stricmp( key, "highlightbackgroundcolor" ) ) {
			URC_ParseColor( args, n, bl->highlightBg );
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
				item->cellKey[0] = item->cellKey[1] = -2;
			}
		}
		// bind_width, bind_height, align: the widget's rect rules
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
	for ( i = 0 ; i < bl->numItems ; i++ ) {
		if ( bl->items[i].shader[0] ) {
			bl->items[i].shaderHandle = re.RegisterShaderNoMip( bl->items[i].shader );
		}
	}
	urc_emptyKeyShader = re.RegisterShaderNoMip( "textures/bind/emptykey" );
	urc_pressKeyShader = re.RegisterShaderNoMip( "textures/menu/presskey" );
}

// the first two keys bound to a command; -1 for none. OpenFAKK2: while a
// controller is connected its keys come first, so the list shows (and
// rebinds) them rather than the keyboard's
static void URC_BindKeys( const char *command, int bound[2] ) {
	int		k, n = 0, pass;

	bound[0] = bound[1] = -1;
	for ( pass = in_padConnected ? 0 : 1 ; pass < 2 ; pass++ ) {
		for ( k = 0 ; k < MAX_KEYS && n < 2 ; k++ ) {
			const char	*b;

			if ( in_padConnected && Key_IsPadKey( k ) != ( pass == 0 ) ) {
				continue;
			}
			b = Key_GetBinding( k );
			if ( b && b[0] && !Q_stricmp( b, command ) ) {
				bound[n++] = k;
			}
		}
	}
}

static int URC_BindVisibleRows( const float rect[4] ) {
	int		rows = urc_bindList.rowHeight > 0 ? (int)( ( rect[3] - urc_bindList.rowHeight ) / urc_bindList.rowHeight ) : 1;

	return rows < 1 ? 1 : rows;
}

static void URC_BindClampScroll( const float rect[4] ) {
	urcBindList_t	*bl = &urc_bindList;
	int				rows = URC_BindVisibleRows( rect );

	if ( bl->scroll > bl->numItems - rows ) {
		bl->scroll = bl->numItems - rows;
	}
	if ( bl->scroll < 0 ) {
		bl->scroll = 0;
	}
}

// a column's cell in a row shown (-1 the header row); column 1 the
// action, 2 the primary key, 3 the alternate
static void URC_BindCellRect( const float rect[4], int shownRow, int col, float r[4] ) {
	urcBindList_t	*bl = &urc_bindList;

	r[1] = rect[1] + bl->rowHeight * ( shownRow + 1 );
	if ( col == 1 ) {
		r[0] = 0;
		r[2] = bl->cmdWidth;
		r[3] = bl->rowHeight;
	} else if ( col == 2 ) {
		r[0] = bl->cmdWidth + bl->fillWidth;
		r[2] = bl->primWidth;
		r[3] = shownRow < 0 ? bl->rowHeight : bl->primHeight;
	} else {
		r[0] = bl->cmdWidth + 2 * bl->fillWidth + bl->primWidth;
		r[2] = bl->altWidth;
		r[3] = shownRow < 0 ? bl->rowHeight : bl->altHeight;
	}
	r[0] += rect[0];
}

// one of the list's labels or buttons: UIWidget::Display's background,
// border and material, cut to the cell
static void URC_DrawBindCell( const float r[4], const vec4_t fg, const vec4_t bg, urcBorder_t border, qhandle_t material, float alpha ) {
	float	saved[4];
	vec4_t	c;

	URC_PushClip( r, saved );
	URC_Box( r[0], r[1], r[2], r[3], bg, alpha );
	if ( border == BORDER_RAISED || border == BORDER_INDENT ) {
		vec4_t	colors[4];

		URC_BorderColors( bg, colors );
		URC_Draw3DBox( r, border == BORDER_INDENT, colors, alpha );
	}
	if ( material ) {
		Vector4Set( c, fg[0], fg[1], fg[2], fg[3] * alpha );
		URC_DrawPicST( r[0], r[1], r[2], r[3], 0, 0, 1, 1, material, c );
	}
	URC_PopClip( saved );
}

/*
=================
URC_DrawBindList
=================
*/
static void URC_DrawBindList( const urcWidget_t *w, const float rect[4], float alpha ) {
	urcBindList_t	*bl = &urc_bindList;
	urcFont_t		*font;
	float			r[4];
	int				rows, row, col, i;
	vec4_t			color;

	URC_LoadBindList( w->bindFile );
	font = URC_LoadFont( w->font );
	rows = URC_BindVisibleRows( rect );
	URC_BindClampScroll( rect );

	// the fill columns: labels with the default background and the
	// fill picture tiled over it, the list's whole height
	if ( bl->fillWidth > 0 ) {
		static const vec4_t	grey = { 0.75f, 0.75f, 0.75f, 1 };
		float				fillX[3];

		fillX[0] = bl->cmdWidth;
		fillX[1] = bl->cmdWidth + bl->primWidth + bl->fillWidth;
		fillX[2] = bl->cmdWidth + bl->primWidth + bl->altWidth + 2 * bl->fillWidth;
		for ( i = 0 ; i < 3 ; i++ ) {
			float	saved[4], s, t;

			r[0] = rect[0] + fillX[i];
			r[1] = rect[1];
			r[2] = bl->fillWidth;
			r[3] = rect[3];
			URC_PushClip( r, saved );
			URC_Box( r[0], r[1], r[2], r[3], grey, alpha );
			if ( bl->fillShader ) {
				Vector4Set( color, 1, 1, 1, alpha );
				URC_TileST( bl->fillShader, r[2], r[3], &s, &t );
				URC_DrawPicST( r[0], r[1], r[2], r[3], 0, 0, s, t, bl->fillShader, color );
			}
			URC_PopClip( saved );
		}
	}

	// the header row
	for ( col = 1 ; col <= 3 ; col++ ) {
		URC_BindCellRect( rect, -1, col, r );
		URC_DrawBindCell( r, bl->titleFg, bl->titleBg, BORDER_RAISED, bl->headerShader[col - 1], alpha );
	}

	// the rows shown
	for ( row = 0 ; row < rows ; row++ ) {
		urcBindItem_t	*item;
		int				bound[2];

		i = bl->scroll + row;
		if ( i >= bl->numItems ) {
			break;
		}
		item = &bl->items[i];
		URC_BindKeys( item->command, bound );
		for ( col = 1 ; col <= 3 ; col++ ) {
			const float	*fg, *bg;
			qboolean	focus = qfalse;
			qhandle_t	material;

			if ( i == bl->selRow ) {
				fg = bl->highlightFg;
				bg = bl->highlightBg;
				if ( col == bl->selCol ) {
					focus = bl->waiting;
					fg = focus ? bl->activeFg : bl->selectFg;
					bg = focus ? bl->activeBg : bl->selectBg;
				}
			} else {
				fg = bl->inactiveFg;
				bg = bl->inactiveBg;
			}
			URC_BindCellRect( rect, row, col, r );
			if ( col == 1 ) {
				URC_DrawBindCell( r, fg, bg, BORDER_NONE, item->shaderHandle, alpha );
				continue;
			}

			// a key button: its picture follows the key bound in its slot
			{
				int		slot = col - 2;
				int		k = bound[slot];

				if ( item->cellKey[slot] != k ) {
					const char	*name = k >= 0 ? Key_KeynumToString( k ) : "None";
					qhandle_t	h = re.RegisterShaderNoMip( va( "textures/bind/%s", name ) );

					item->cellKey[slot] = k;
					item->cellNamed[slot] = !h;		// no such picture
					item->cellShader[slot] = item->cellNamed[slot] ? urc_emptyKeyShader : h;
				}
				material = item->cellShader[slot];
				URC_DrawBindCell( r, fg, bg, BORDER_NONE, material, alpha );
				if ( focus ) {
					// UIBindButton draws a focused cell's border over it
					float	saved[4];
					vec4_t	borders[4];

					URC_BorderColors( bl->activeBorder, borders );
					URC_PushClip( r, saved );
					URC_DrawBorder( r, bg, borders[3], 2, 2, 1 );
					URC_PopClip( saved );
				}
				if ( item->cellNamed[slot] && font ) {
					float	saved[4], tr[4];

					tr[0] = r[0] + 1;
					tr[1] = r[1] + 1;
					tr[2] = r[2];
					tr[3] = r[3];
					Vector4Copy( fg, color );
					URC_PushClip( r, saved );
					URC_PrintJustified( font, tr, URC_JUSTIFY_CENTER, URC_JUSTIFY_MIDDLE,
						k >= 0 ? Key_KeynumToDisplayString( k ) : "None", color );
					URC_PopClip( saved );
				}
			}
		}
	}

	URC_DrawScrollBar( rect, bl->numItems, rows, bl->scroll, urc_scrollHeld, alpha );
}

// the "press a key" picture in the middle of the screen while a cell waits
static void URC_DrawPressKey( void ) {
	static const vec4_t	grey = { 0.75f, 0.75f, 0.75f, 1 };
	static const vec4_t	white = { 1, 1, 1, 1 };
	float				w, h;

	if ( !urc_bindList.waiting || !urc_pressKeyShader || !urc_stackDepth ) {
		return;
	}
	w = re.GetShaderWidth( urc_pressKeyShader );
	h = re.GetShaderHeight( urc_pressKeyShader );
	URC_Box( ( 640 - w ) * 0.5f, ( 480 - h ) * 0.5f, w, h, grey, 1 );
	URC_DrawPicST( ( 640 - w ) * 0.5f, ( 480 - h ) * 0.5f, w, h, 0, 0, 1, 1, urc_pressKeyShader, white );
}

/*
=================
URC_BindListClick

A cell selects its row and column, a key cell starts waiting for a key
(UIFakkBindList's focus handling, 0x4c11c0); the bar scrolls.
=================
*/
static void URC_BindListClick( const urcWidget_t *w, const float rect[4] ) {
	urcBindList_t	*bl = &urc_bindList;
	urcScrollPart_t	part;
	int				rows = URC_BindVisibleRows( rect );
	int				row, col, i;
	float			r[4];

	part = URC_ScrollPart( rect, bl->numItems, rows, bl->scroll, urc_cursorX, urc_cursorY );
	if ( part != SCROLL_NONE ) {
		urc_scrollHeld = part;
		switch ( part ) {
		case SCROLL_UP: bl->scroll--; break;
		case SCROLL_DOWN: bl->scroll++; break;
		case SCROLL_PAGEUP: bl->scroll -= rows; break;
		case SCROLL_PAGEDOWN: bl->scroll += rows; break;
		case SCROLL_THUMB:
			{
				float	sr[4], ty, th;

				URC_ScrollRect( rect, sr );
				URC_ScrollThumb( sr, bl->numItems, rows, bl->scroll, &ty, &th );
				urc_scrollGrab = urc_cursorY - ( sr[1] + ty );
			}
			break;
		default: break;
		}
		URC_BindClampScroll( rect );
		return;
	}

	row = (int)( ( urc_cursorY - rect[1] ) / bl->rowHeight ) - 1;
	if ( row < 0 || row >= rows ) {
		return;
	}
	i = bl->scroll + row;
	if ( i >= bl->numItems ) {
		return;
	}
	for ( col = 1 ; col <= 3 ; col++ ) {
		URC_BindCellRect( rect, row, col, r );
		if ( urc_cursorX >= r[0] && urc_cursorX < r[0] + r[2] && urc_cursorY >= r[1] && urc_cursorY < r[1] + r[3] ) {
			break;
		}
	}
	if ( col > 3 ) {
		return;
	}
	bl->selRow = i;
	bl->selCol = col;
	bl->waiting = col > 1;
	if ( bl->waiting ) {
		URC_PlaySound( bl->enterSound );
	}
}

/*
=================
URC_BindListKey

The key pressed while a cell waits takes the place of the key in its
slot; escape leaves it. Returns whether it was taken.
=================
*/
static qboolean URC_BindListKey( int key ) {
	urcBindList_t	*bl = &urc_bindList;
	urcBindItem_t	*item;
	int				bound[2], slot;

	if ( !bl->waiting || bl->selRow < 0 || bl->selRow >= bl->numItems || bl->selCol < 2 ) {
		return qfalse;
	}
	if ( key == K_ESCAPE ) {
		bl->waiting = qfalse;
		URC_PlaySound( bl->activeSound );
		return qtrue;
	}
	if ( key == K_MWHEELUP || key == K_MWHEELDOWN ) {
		return qfalse;
	}
	item = &bl->items[bl->selRow];
	slot = bl->selCol - 2;
	URC_BindKeys( item->command, bound );
	if ( bound[slot] >= 0 ) {
		Key_SetBinding( bound[slot], "" );
	}
	Key_SetBinding( key, item->command );
	bl->waiting = qfalse;
	URC_PlaySound( bl->changeSound );
	return qtrue;
}

/*
=======================================================================

THE SAVED GAMES

UIFAKKLoadGameClass, a UIListCtrl. Its columns (0x414710): "Location"
four tenths of its width, "Elapased Time" (retail's spelling) two tenths,
"Date & Time" four tenths. Its rows (0x4147d0) come from the saved games'
headers, save/<config>_*.ssv (Com_GetArchiveFileName, sv_save.c): the time
it was saved, the map time in milliseconds, the location (64 characters),
the map and the save's name (64 each), shown as the location, H:MM:SS and
ctime of the save time; they are sorted newest first and the first is
selected. Loading and deleting go by the save's name.
The selected game's picture, save/<config>_<name>.tga, or
textures/menu/no_saved_games.tga when there is none, becomes the
material of the menu's "LoadSaveShot" label (0x415390).

The list's Draw (0x4ace40): the column headers (0x4ac8e0) a font height
plus four tall, each UIWhite-bordered and 0.3 grey, its title at 2, 2 in
white, the rest of the row likewise; then a row per game (0x4acad0) a
font height tall, each cell filled and its text at its top left: the
selected row white with black text, the others the list's background
colour, opaque, with its foreground colour; and its scroll bar.

=======================================================================
*/

#define URC_SAVE_HEADER		16		// verdana-12's 12 + UIPadding 2 above and below
#define URC_SAVE_NOSHOT		"textures/menu/no_saved_games.tga"

static int URC_SaveCompare( const void *a, const void *b ) {
	return ( (const urcSave_t *)b )->time - ( (const urcSave_t *)a )->time;
}

static int URC_SaveRowHeight( const urcWidget_t *w ) {
	return URC_FontHeight( URC_LoadFont( w->font ) );
}

static int URC_SaveVisibleRows( const urcWidget_t *w ) {
	int		rows = (int)( ( w->rect[3] - URC_SAVE_HEADER ) / URC_SaveRowHeight( w ) );

	return rows < 1 ? 1 : rows;
}

// the "LoadSaveShot" label shows the selected game's picture
static void URC_SaveShowShot( void ) {
	const char	*name = URC_SAVE_NOSHOT;
	int			i;

	if ( urc_saveSelected > 0 && urc_saveSelected <= urc_numSaves ) {
		char	base[MAX_QPATH];

		COM_StripExtension( urc_saves[urc_saveSelected - 1].file, base, sizeof( base ) );
		name = va( "%s.tga", base );
	}
	for ( i = 0 ; i < urc_numWidgets ; i++ ) {
		urcWidget_t	*w = &urc_widgets[i];

		if ( !Q_stricmp( w->name, "LoadSaveShot" ) ) {
			URC_LoadWidgetShaders( w );
			w->shader = re.RegisterShaderNoMip( name );
			w->tiled = qfalse;
		}
	}
}

static void URC_LoadSaves( void ) {
	char	**files;
	char	prefix[MAX_QPATH];
	int		numFiles, i;

	// save/<config>_<name>.ssv, as Com_GetArchiveFileName names them
	COM_StripExtension( Com_ConfigName(), prefix, sizeof( prefix ) );
	Q_strcat( prefix, sizeof( prefix ), "_" );

	urc_numSaves = 0;
	files = FS_ListFiles( "save", ".ssv", &numFiles );
	for ( i = 0 ; i < numFiles && urc_numSaves < URC_MAX_SAVES ; i++ ) {
		fileHandle_t	f;
		byte			header[200];
		urcSave_t		*s;

		// another config's games are not listed
		if ( Q_stricmpn( files[i], prefix, strlen( prefix ) ) ) {
			continue;
		}
		Com_Memset( header, 0, sizeof( header ) );
		if ( FS_FOpenFileRead( va( "save/%s", files[i] ), &f, qtrue ) <= 0 || !f ) {
			continue;
		}
		FS_Read( header, sizeof( header ), f );
		FS_FCloseFile( f );
		s = &urc_saves[urc_numSaves++];
		Q_strncpyz( s->file, va( "save/%s", files[i] ), sizeof( s->file ) );
		s->time = LittleLong( ( (int *)header )[0] );
		s->mapTime = LittleLong( ( (int *)header )[1] );
		Q_strncpyz( s->comment, (char *)header + 8, sizeof( s->comment ) );
		Q_strncpyz( s->name, (char *)header + 136, sizeof( s->name ) );
	}
	FS_FreeFileList( files );
	qsort( urc_saves, urc_numSaves, sizeof( urc_saves[0] ), URC_SaveCompare );
	urc_saveSelected = urc_numSaves ? 1 : 0;
	urc_saveTop = 0;
	URC_SaveShowShot();
}

static void URC_SaveClampTop( const urcWidget_t *w ) {
	int		rows = URC_SaveVisibleRows( w );

	if ( urc_saveTop > urc_numSaves - rows ) {
		urc_saveTop = urc_numSaves - rows;
	}
	if ( urc_saveTop < 0 ) {
		urc_saveTop = 0;
	}
}

static void URC_SaveSelect( const urcWidget_t *w, int index ) {
	int		rows = URC_SaveVisibleRows( w );

	if ( index < 1 || index > urc_numSaves ) {
		return;
	}
	if ( index != urc_saveSelected ) {
		urc_saveSelected = index;
		URC_SaveShowShot();
	}
	if ( index - 1 < urc_saveTop ) {
		urc_saveTop = index - 1;
	} else if ( index - 1 >= urc_saveTop + rows ) {
		urc_saveTop = index - rows;
	}
	URC_SaveClampTop( w );
}

// what a cell shows
static const char *URC_SaveCell( const urcSave_t *s, int col ) {
	if ( col == 0 ) {
		return s->comment;
	}
	if ( col == 1 ) {
		int		secs = s->mapTime / 1000;

		return va( "%d:%02d:%02d", secs / 3600, secs % 3600 / 60, secs % 60 );
	} else {
		time_t		t = s->time;
		const char	*str = ctime( &t );

		return str ? str : "";
	}
}

static void URC_DrawSaveList( const urcWidget_t *w, const float rect[4], float alpha ) {
	static const char	*titles[3] = { "Location", "Elapased Time", "Date & Time" };
	static const vec4_t	headerFill = { 0.3f, 0.3f, 0.3f, 1 };
	static const vec4_t	white = { 1, 1, 1, 1 };
	static const vec4_t	black = { 0, 0, 0, 1 };
	urcFont_t			*font = URC_LoadFont( w->font );
	float				colW[3], x, y, rowH, r[4];
	vec4_t				fill, text;
	int					col, i, rows;

	colW[0] = colW[2] = (int)( rect[2] * 0.4f );
	colW[1] = (int)( rect[2] * 0.2f );
	rowH = URC_FontHeight( font );
	rows = URC_SaveVisibleRows( w );
	URC_SaveClampTop( w );

	// the column headers
	Vector4Set( text, 1, 1, 1, alpha );
	x = 0;
	for ( col = 0 ; col < 3 ; col++ ) {
		r[0] = rect[0] + x;
		r[1] = rect[1];
		r[2] = colW[col] + 1;
		r[3] = URC_SAVE_HEADER;
		URC_DrawBorder( r, headerFill, white, 1, 3, alpha );
		URC_DrawString( font, r[0] + 2, r[1] + 2, titles[col], text );
		x += colW[col];
	}
	if ( x < rect[2] ) {
		r[0] = rect[0] + x;
		r[1] = rect[1];
		r[2] = rect[2] - x + 1;
		r[3] = URC_SAVE_HEADER;
		URC_DrawBorder( r, headerFill, white, 1, 3, alpha );
	}

	// the games
	y = URC_SAVE_HEADER;
	for ( i = urc_saveTop ; i < urc_numSaves && y <= rect[3] ; i++, y += rowH ) {
		if ( i + 1 == urc_saveSelected ) {
			Vector4Copy( white, fill );
			Vector4Copy( black, text );
		} else {
			Vector4Set( fill, w->bgcolor[0], w->bgcolor[1], w->bgcolor[2], alpha );
			Vector4Copy( w->fgcolor, text );
		}
		text[3] *= alpha;
		x = 0;
		for ( col = 0 ; col < 3 ; col++ ) {
			URC_Box( rect[0] + x, rect[1] + y, colW[col], rowH, fill, alpha );
			URC_DrawString( font, rect[0] + x, rect[1] + y, URC_SaveCell( &urc_saves[i], col ), text );
			x += colW[col];
		}
		if ( x < rect[2] ) {
			URC_Box( rect[0] + x, rect[1] + y, rect[2] - x, rowH, fill, alpha );
		}
	}

	URC_DrawScrollBar( rect, urc_numSaves, rows, urc_saveTop, urc_scrollHeld, alpha );
}

/*
=================
URC_SaveListClick

A row selects its game; a second click on it within half a second loads
it; the bar scrolls.
=================
*/
static void URC_SaveListClick( const urcWidget_t *w, const float rect[4] ) {
	urcScrollPart_t	part;
	int				rows = URC_SaveVisibleRows( w );
	int				row;

	part = URC_ScrollPart( rect, urc_numSaves, rows, urc_saveTop, urc_cursorX, urc_cursorY );
	if ( part != SCROLL_NONE ) {
		urc_scrollHeld = part;
		switch ( part ) {
		case SCROLL_UP: urc_saveTop--; break;
		case SCROLL_DOWN: urc_saveTop++; break;
		case SCROLL_PAGEUP: urc_saveTop -= rows; break;
		case SCROLL_PAGEDOWN: urc_saveTop += rows; break;
		case SCROLL_THUMB:
			{
				float	sr[4], ty, th;

				URC_ScrollRect( rect, sr );
				URC_ScrollThumb( sr, urc_numSaves, rows, urc_saveTop, &ty, &th );
				urc_scrollGrab = urc_cursorY - ( sr[1] + ty );
			}
			break;
		default: break;
		}
		URC_SaveClampTop( w );
		return;
	}
	if ( urc_cursorY < rect[1] + URC_SAVE_HEADER ) {
		return;
	}
	row = urc_saveTop + (int)( ( urc_cursorY - rect[1] - URC_SAVE_HEADER ) / URC_SaveRowHeight( w ) ) + 1;
	if ( row < 1 || row > urc_numSaves ) {
		return;
	}
	if ( row == urc_saveClickRow && cls.realtime - urc_saveClickTime < 500 ) {
		Cbuf_AddText( "widgetcommand LoadSaveList loadgame\n" );
		urc_saveClickRow = 0;
		return;
	}
	urc_saveClickRow = row;
	urc_saveClickTime = cls.realtime;
	URC_SaveSelect( w, row );
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

Where a menu and its widgets are this frame, in 640x480 units. On a window
wider than 4:3 (cl_widescreenHud) the screen reaches past 0 and 640: a menu
aligned left or right goes to that side of the window (the HUD to its
corners), and one with no horizontal alignment (the full-screen menus)
stays in the 4:3 frame in the middle. A menu slides in from off the
window's edge. URC_MenuAnchor is the point the alignment holds the menu to,
which the HUD's size (cl_hudScale) shrinks it towards.
=================
*/
static void URC_MenuSides( const urcMenu_t *menu, float *left, float *right ) {
	*left = 0;
	*right = 640;
	if ( menu->hasAlignH ) {
		*left = SCR_ScreenLeft640();
		*right = SCR_ScreenRight640();
	}
}

static void URC_MenuAnchor( const urcMenu_t *menu, float *px, float *py ) {
	float	left, right;

	URC_MenuSides( menu, &left, &right );
	switch ( menu->alignH ) {
	case 1: *px = 320; break;
	case 2: *px = right; break;
	default: *px = left; break;
	}
	switch ( menu->alignV ) {
	case 1: *py = 240; break;
	case 2: *py = 480; break;
	default: *py = 0; break;
	}
}

static void URC_MenuOrigin( const urcMenu_t *menu, int elapsed, float *ox, float *oy ) {
	float	dx, dy, left, right;

	URC_MenuSides( menu, &left, &right );
	switch ( menu->alignH ) {
	case 1: *ox = ( 640 - menu->width ) * 0.5f; break;
	case 2: *ox = right - menu->width; break;
	default: *ox = left; break;
	}
	switch ( menu->alignV ) {
	case 1: *oy = ( 480 - menu->height ) * 0.5f; break;
	case 2: *oy = 480 - menu->height; break;
	default: *oy = 0; break;
	}
	URC_MotionOffset( menu->dir, menu->dirTime, elapsed, SCR_ScreenRight640() - SCR_ScreenLeft640(), 480, &dx, &dy );
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

		if ( w->disabled ) {
			continue;
		}
		if ( w->cls != WC_BUTTON && w->cls != WC_CHECKBOX && w->cls != WC_SLIDER &&
			w->cls != WC_PULLDOWN && w->cls != WC_BINDLIST && w->cls != WC_LOADGAME && w->cls != WC_MULTILINE ) {
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
URC_LinkValue

A widget's cvar's value: for a latched cvar, the value it takes at the
next restart, so a choice shows before Apply's restart applies it, as
the UI's cvar reader returns it (fakk2.exe 0x40cba0).
=================
*/
static const char *URC_LinkValue( const urcWidget_t *w ) {
	const char	*latched = Cvar_LatchedString( w->linkcvar );

	return latched ? latched : Cvar_VariableString( w->linkcvar );
}

/*
=================
URC_LabelText

What a label shows: its cvar's value, through its linkstrings, or its
title (UILabel::Draw, 0x4b0eb0); with a "linkformat", the value put into
that text in place of its %s.
=================
*/
static const char *URC_LabelText( const urcWidget_t *w ) {
	static char	formatted[256];
	const char	*text = w->title, *at;
	int			i;

	if ( !w->linkcvar[0] ) {
		return text;
	}
	text = URC_LinkValue( w );
	for ( i = 0 ; i < w->numLinkStrings ; i++ ) {
		if ( !Q_stricmp( w->linkStrings[i].value, text ) ||
			( atof( w->linkStrings[i].value ) == atof( text ) && Q_isanumber( text ) ) ) {
			text = w->linkStrings[i].text;
			break;
		}
	}
	if ( w->linkFormat[0] && ( at = strstr( w->linkFormat, "%s" ) ) != NULL ) {
		Com_sprintf( formatted, sizeof( formatted ), "%.*s%s%s", (int)( at - w->linkFormat ), w->linkFormat, text, at + 2 );
		return formatted;
	}
	return text;
}

/*
=================
URC_SliderGeometry

UISlider (0x4b5970, 0x4b64e0): arrows 15 wide at both ends, the thumb (its
image's width) moving between them over the track the rest of the width.
=================
*/
#define URC_SLIDER_ARROW	15

static float URC_SliderThumbWidth( void ) {
	float	tw = urc_sliderThumb ? re.GetShaderWidth( urc_sliderThumb ) : 0;

	return tw > 0 ? tw : 8;
}

static float URC_SliderFrac( const urcWidget_t *w ) {
	float	frac;

	if ( !w->linkcvar[0] || w->rangeMax <= w->rangeMin ) {
		return 0;
	}
	frac = ( atof( URC_LinkValue( w ) ) - w->rangeMin ) / ( w->rangeMax - w->rangeMin );
	if ( frac < 0 ) frac = 0;
	if ( frac > 1 ) frac = 1;
	return frac;
}

static void URC_DrawSlider( const urcWidget_t *w, const float r[4], float alpha ) {
	float		thumbW, frac;
	int			index = w - urc_widgets;
	vec4_t		white;
	qhandle_t	pic;

	thumbW = URC_SliderThumbWidth();
	frac = URC_SliderFrac( w );
	Vector4Set( white, 1, 1, 1, alpha );
	pic = urc_dragSlider == index ? urc_sliderThumbSel : urc_sliderThumb;
	if ( pic ) {
		URC_DrawPicST( r[0] + URC_SLIDER_ARROW + frac * ( r[2] - 2 * URC_SLIDER_ARROW - thumbW ), r[1],
			thumbW, r[3], 0, 0, 1, 1, pic, white );
	}
	pic = urc_sliderPress == index * 2 ? urc_arrowLeftSel : urc_arrowLeft;
	if ( pic ) {
		URC_DrawPicST( r[0], r[1], URC_SLIDER_ARROW, r[3], 0, 0, 1, 1, pic, white );
	}
	pic = urc_sliderPress == index * 2 + 1 ? urc_arrowRightSel : urc_arrowRight;
	if ( pic ) {
		URC_DrawPicST( r[0] + r[2] - URC_SLIDER_ARROW, r[1], URC_SLIDER_ARROW, r[3], 0, 0, 1, 1, pic, white );
	}
}

/*
=================
URC_DrawCheckBox

UICheckBox (0x4bb6e0): the 16x16 box 16 in from the left, centred
vertically, in the foreground colour; the title left-justified 48 in,
a pixel above centre.
=================
*/
static void URC_DrawCheckBox( const urcWidget_t *w, const float r[4], float alpha ) {
	urcFont_t	*font;
	qhandle_t	box;
	vec4_t		color;
	float		size = 16;

	Vector4Copy( w->fgcolor, color );
	color[3] *= alpha;
	box = ( w->linkcvar[0] && atof( URC_LinkValue( w ) ) ) ? w->checkedShader : w->uncheckedShader;
	if ( box ) {
		URC_DrawPicST( r[0] + size, r[1] + ( r[3] - size ) * 0.5f, size, size, 0, 0, 1, 1, box, color );
	}
	font = URC_LoadFont( w->font );
	if ( font && w->title[0] ) {
		URC_DrawLines( font, r[0] + 2 * size + size, r[1] + ( r[3] - URC_FontHeight( font ) ) * 0.5f - 1, w->title, color );
	}
}

static int		urc_pressWidget = -1;		// the button the left button went down on

/*
=================
URC_DrawMultiLine

UIMultiLineEdit (0x4b7530) as the GFX Info menu uses it, not editable: its
cvar's text a line per font height from its top left, in its foreground
colour, from the first line shown, with a scroll bar.
=================
*/
static int URC_MultiLineCount( const urcWidget_t *w ) {
	const char	*s = w->linkcvar[0] ? Cvar_VariableString( w->linkcvar ) : w->title;
	int			n = 1;

	for ( ; *s ; s++ ) {
		n += ( *s == '\n' );
	}
	return n;
}

static int URC_MultiLinePage( const urcWidget_t *w ) {
	int		page = (int)( w->rect[3] / URC_FontHeight( URC_LoadFont( w->font ) ) );

	return page < 1 ? 1 : page;
}

static void URC_MultiLineClamp( urcWidget_t *w ) {
	int		lines = URC_MultiLineCount( w ), page = URC_MultiLinePage( w );

	if ( w->lineTop > lines - page ) {
		w->lineTop = lines - page;
	}
	if ( w->lineTop < 0 ) {
		w->lineTop = 0;
	}
}

static void URC_DrawMultiLine( urcWidget_t *w, const float r[4], float alpha ) {
	urcFont_t	*font = URC_LoadFont( w->font );
	const char	*s = w->linkcvar[0] ? Cvar_VariableString( w->linkcvar ) : w->title;
	float		y = r[1], fh = URC_FontHeight( font );
	int			line = 0;
	vec4_t		color;

	URC_MultiLineClamp( w );
	Vector4Copy( w->fgcolor, color );
	color[3] *= alpha;
	while ( *s && y < r[1] + r[3] ) {
		if ( line >= w->lineTop ) {
			URC_DrawString( font, r[0], y, s, color );
			y += fh;
		}
		while ( *s && *s != '\n' ) {
			s++;
		}
		if ( *s ) {
			s++;
		}
		line++;
	}
	URC_DrawScrollBar( r, URC_MultiLineCount( w ), URC_MultiLinePage( w ), w->lineTop, urc_scrollHeld, alpha );
}

/*
=================
URC_DrawWidget

UIWidget::Display (0x49ef30) for one widget, then its class's Draw.
=================
*/
static void URC_DrawWidget( urcWidget_t *w, const urcMenu_t *menu, float ox, float oy, int menuElapsed, float menuAlpha, qboolean onTop ) {
	float		x, y, wd, ht, alpha, rect[4], saved[4];
	int			index = w - urc_widgets;
	qboolean	hovered;
	vec4_t		color;

	if ( w->disabled ) {
		return;
	}
	URC_LoadWidgetShaders( w );
	URC_WidgetRect( w, menu, ox, oy, menuElapsed, rect );
	alpha = menuAlpha * URC_FadeAlpha( w->fadein, menuElapsed );
	x = rect[0];
	y = rect[1];
	wd = rect[2];
	ht = rect[3];
	hovered = onTop && index == urc_hoverWidget;
	URC_PushClip( rect, saved );

	// the background, the border
	URC_Box( x, y, wd, ht, w->bgcolor, alpha );
	if ( w->borderStyle != BORDER_NONE ) {
		vec4_t	colors[4];

		URC_BorderColors( w->bgcolor, colors );
		if ( w->borderStyle == BORDER_3D ) {
			URC_DrawBorder( rect, colorWhite, colors[3], 2, 2, alpha );
		} else {
			URC_Draw3DBox( rect, w->borderStyle == BORDER_INDENT, colors, alpha );
		}
	}

	// the material in the foreground colour: the menu shaders use
	// "rgbGen global" and "alphaGen global" for it, the HUD's use identity;
	// the hover material over it
	Vector4Copy( w->fgcolor, color );
	color[3] *= alpha;
	if ( w->shader ) {
		if ( w->tiled ) {
			float	s1, t1;

			URC_TileST( w->shader, wd, ht, &s1, &t1 );
			URC_DrawPicST( x, y, wd, ht, 0, 0, s1, t1, w->shader, color );
		} else {
			URC_DrawPicST( x, y, wd, ht, 0, 0, 1, 1, w->shader, color );
		}
	}
	if ( hovered && w->hoverShader ) {
		URC_DrawPicST( x, y, wd, ht, 0, 0, 1, 1, w->hoverShader, color );
	}

	switch ( w->cls ) {
	case WC_CHECKBOX:
		URC_DrawCheckBox( w, rect, alpha );
		break;
	case WC_SLIDER:
		URC_DrawSlider( w, rect, alpha );
		break;
	case WC_PULLDOWN:
		{
			// UIPulldownMenu: the menu picture at its own size, the
			// selected one while its list is open
			qhandle_t	pic = ( onTop && urc_popupWidget == index && w->selMenuShader ) ? w->selMenuShader : w->menuShader;
			vec4_t		white;

			if ( pic ) {
				Vector4Set( white, 1, 1, 1, alpha );
				URC_DrawPicST( x, y, re.GetShaderWidth( pic ), re.GetShaderHeight( pic ), 0, 0, 1, 1, pic, white );
			}
		}
		break;
	case WC_BINDLIST:
		URC_DrawBindList( w, rect, alpha );
		break;
	case WC_LOADGAME:
		URC_DrawSaveList( w, rect, alpha );
		break;
	case WC_MULTILINE:
		URC_DrawMultiLine( w, rect, alpha );
		break;
	default:
		break;
	}

	// a stat bar fills from the bottom or the left with the stat's share
	// of its range (UIFakkLabel::Draw 0x4185e0, the bar 0x418270); after a
	// change its flash shader is added over it, fading out over 750 ms
	if ( w->statbar && w->playerstat >= 0 ) {
		float		stat = URC_PlayerStat( w->playerstat );
		float		range = w->maxplayerstat >= 0 ? URC_PlayerStat( w->maxplayerstat ) : w->statMax - w->statMin;
		float		frac;
		qhandle_t	barShader = w->statTile ? w->statTile : w->statShader;

		// retail divides by the range as it is, so an empty range (no ammo
		// for no weapon) is a NaN and draws nothing, and a full stat over it
		// fills the bar
		frac = range != 0 ? stat / range : ( stat > 0 ? 1 : 0 );
		if ( frac > 1 ) frac = 1;
		if ( frac < 0 ) frac = 0;
		if ( frac > 0 ) {
			qboolean	tile = w->statTile || !w->statShader;
			float		flash;

			// the flash restarts whenever the fill changes, a change of the
			// range (another weapon's ammo) included; an empty bar is not
			// drawn and so does not count. This port does not flash the
			// first fill it draws.
			if ( frac != w->lastStatFrac ) {
				if ( w->lastStatFrac >= 0 ) {
					w->statChangeTime = cls.realtime;
				}
				w->lastStatFrac = frac;
			}
			flash = w->statChangeTime ? 1 - ( cls.realtime - w->statChangeTime ) / 750.0f : 0;
			if ( flash > 1 ) flash = 1;

			if ( barShader ) {
				URC_DrawStatBar( w->statbar, tile, x, y, wd, ht, frac, barShader, color );
			}
			// over the bar, not instead of it: the flash shaders add their
			// image in the colour set for them (rgbGen global), white at the
			// change fading to nothing, not the widget's (black) colour
			if ( flash > 0 && w->statFlash ) {
				vec4_t	flashColor;

				Vector4Set( flashColor, flash, flash, flash, flash );
				URC_DrawStatBar( w->statbar, tile, x, y, wd, ht, frac, w->statFlash, flashColor );
			}
		}
	}

	// the HUD's weapon windows: the wielded item's model (cl_inv.c)
	if ( w->invmodelrender >= 0 ) {
		INV_DrawHudModel( w->invmodelrender, x, y, wd, ht, alpha );
	}

	// text: a label shows its cvar's value or its title, a button its
	// title, both centred (a held button's a pixel down and right)
	if ( ( w->cls == WC_LABEL || w->cls == WC_BUTTON || w->cls == WC_FIELD || w->cls == WC_LIST || w->cls == WC_OTHER ) &&
		w->invmodelrender < 0 && !w->statbar ) {
		const char	*text = w->cls == WC_BUTTON ? w->title : URC_LabelText( w );
		urcFont_t	*font = URC_LoadFont( w->font );

		if ( text[0] && font ) {
			float	tr[4];

			Vector4Copy( rect, tr );
			if ( w->cls == WC_BUTTON && urc_pressWidget == index && hovered ) {
				tr[0] += 1;
				tr[1] += 1;
			}
			Vector4Copy( w->fgcolor, color );
			color[3] *= alpha;
			URC_PrintJustified( font, tr, URC_JUSTIFY_CENTER, URC_JUSTIFY_MIDDLE, text, color );
		}
	}
	URC_PopClip( saved );
}

/*
=================
URC_DrawMenu

A HUD menu (one shown rather than pushed, which the cursor never reaches)
is drawn at the HUD's size, towards the point its alignment anchors it to.
=================
*/
static void URC_DrawMenu( urcMenu_t *menu, qboolean onTop, qboolean hud ) {
	float	ox, oy, alpha;
	int		elapsed, i;
	vec4_t	color;

	elapsed = cls.realtime - menu->showTime;
	if ( menu->hasBgfill ) {
		// the whole window: a full-screen menu on a wide one has its colour
		// (black in every shipped menu) at the sides
		URC_FillRect( SCR_ScreenLeft640(), 0, SCR_ScreenRight640() - SCR_ScreenLeft640(), 480, menu->bgfill );
	}
	if ( hud && !menu->fullscreen ) {
		float	px, py;

		URC_MenuAnchor( menu, &px, &py );
		SCR_BeginHudScale( px, py );
	}
	URC_MenuOrigin( menu, elapsed, &ox, &oy );
	alpha = URC_FadeAlpha( menu->fadein, elapsed );

	Vector4Copy( menu->bgcolor, color );
	color[3] *= alpha;
	URC_FillRect( ox, oy, menu->width, menu->height, color );

	for ( i = 0 ; i < menu->numWidgets ; i++ ) {
		URC_DrawWidget( &urc_widgets[menu->firstWidget + i], menu, ox, oy, elapsed, alpha, onTop );
	}
	SCR_EndHudScale();
}

/*
=======================================================================

MENU STACK AND HUD

=======================================================================
*/

/*
=================
URC_SnapshotCvars

What ui_checkrestart compares against and "popmenu 1" puts back: each
linked cvar's value as the menu comes up (0x4a3a30, from the window
manager's push, show and force, 0x4a2e90, 0x4a3020, 0x4a3260), read as the
UI reads cvars, a latched one's coming value (0x40cba0).
=================
*/
static void URC_SnapshotCvars( urcMenu_t *menu ) {
	int		i;

	for ( i = 0 ; i < menu->numWidgets ; i++ ) {
		urcWidget_t	*w = &urc_widgets[menu->firstWidget + i];

		if ( w->linkcvar[0] ) {
			Q_strncpyz( w->linkSnapshot, URC_LinkValue( w ), sizeof( w->linkSnapshot ) );
		}
	}
}

/*
=================
URC_RestoreCvars

"popmenu 1", the menus' Back (0x4a3420, 0x4a3c40): every cvar a widget of
the menu on top links to goes back to its value as the menu came up, set
as the UI sets cvars (0x40ccb0: Cvar_Set2, not forced), so a choice made
on the page and not applied is undone, latched or not.
=================
*/
static void URC_RestoreCvars( void ) {
	urcMenu_t	*menu;
	int			i;

	if ( !urc_stackDepth ) {
		return;
	}
	menu = &urc_menus[urc_stack[urc_stackDepth - 1]];
	for ( i = 0 ; i < menu->numWidgets ; i++ ) {
		urcWidget_t	*w = &urc_widgets[menu->firstWidget + i];

		if ( w->linkcvar[0] && strcmp( URC_LinkValue( w ), w->linkSnapshot ) ) {
			Cvar_SetLatched( w->linkcvar, w->linkSnapshot );
		}
	}
}

static void URC_ShowMenu( urcMenu_t *menu ) {
	if ( menu && !menu->visible ) {
		menu->visible = qtrue;
		menu->showTime = cls.realtime;
		URC_SnapshotCvars( menu );
	}
}

static void URC_HideMenu( urcMenu_t *menu ) {
	if ( menu ) {
		menu->visible = qfalse;
	}
}

/*
=================
URC_UpdateCatcher

After the stack changes. Retail's (0x40ab60) also sets ui_newvidmode, what
the video menu's resolution label shows, to r_mode when a menu comes up
and it is unset, and unsets it (-1) when the last one goes.
=================
*/
static void URC_UpdateCatcher( void ) {
	if ( urc_stackDepth ) {
		Key_SetCatcher( Key_GetCatcher() | KEYCATCH_UI );
		if ( Cvar_VariableIntegerValue( "ui_newvidmode" ) == -1 ) {
			Cvar_Set( "ui_newvidmode", va( "%d", Cvar_VariableIntegerValue( "r_mode" ) ) );
		}
	} else {
		Key_SetCatcher( Key_GetCatcher() & ~KEYCATCH_UI );
		Cvar_Set( "ui_newvidmode", "-1" );
	}
}

/*
=================
URC_UpdateOptionCvars

What the menus show for settings spread over several latched cvars, from
the values they take at the next restart: the window size for the video
menu's resolution (r_mode, r_customwidth and r_customheight at -1, the
desktop's at -2), and OpenFAKK2's options menu's anisotropic filter.
=================
*/
static const char *URC_PendingString( const char *name ) {
	const char	*latched = Cvar_LatchedString( name );

	return latched ? latched : Cvar_VariableString( name );
}

static void URC_SetIfChanged( const char *name, const char *value ) {
	if ( strcmp( Cvar_VariableString( name ), value ) ) {
		Cvar_Set( name, value );
	}
}

static void URC_UpdateOptionCvars( void ) {
	// the renderer's r_vidModes (tr_init.c)
	static const int	modes[][2] = {
		{ 320, 240 }, { 400, 300 }, { 512, 384 }, { 640, 480 }, { 800, 600 }, { 960, 720 },
		{ 1024, 768 }, { 1152, 864 }, { 1280, 1024 }, { 1600, 1200 }, { 2048, 1536 }, { 856, 480 }
	};
	int		mode = atoi( URC_PendingString( "r_mode" ) );

	if ( mode == -1 ) {
		URC_SetIfChanged( "ui_resolution", va( "%d x %d", atoi( URC_PendingString( "r_customwidth" ) ),
			atoi( URC_PendingString( "r_customheight" ) ) ) );
	} else if ( mode >= 0 && mode < (int)ARRAY_LEN( modes ) ) {
		URC_SetIfChanged( "ui_resolution", va( "%d x %d", modes[mode][0], modes[mode][1] ) );
	} else {
		URC_SetIfChanged( "ui_resolution", "Desktop" );
	}
	if ( atoi( URC_PendingString( "r_ext_texture_filter_anisotropic" ) ) ) {
		URC_SetIfChanged( "ui_anisotropy", va( "%dx", atoi( URC_PendingString( "r_ext_max_anisotropy" ) ) ) );
	} else {
		URC_SetIfChanged( "ui_anisotropy", "Off" );
	}
}

qboolean URC_HasMenus( void ) {
	return urc_initialised && urc_stackDepth > 0;
}

// a bind list cell waits for the key to bind (the controller's sticks and
// buttons are then keys rather than the cursor, sdl_input.c)
qboolean URC_BindWaiting( void ) {
	return URC_HasMenus() && urc_bindList.waiting;
}

qboolean URC_IntroActive( void ) {
	return urc_initialised && urc_introStep != 0;
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
	URC_SnapshotCvars( menu );		// also when it was shown already
	URC_UpdateCatcher();
	INV_Close();		// not retail's: its weapon bar stays over the menu (cl_inv.c)

	for ( i = 0 ; i < menu->numWidgets ; i++ ) {
		urcWidget_t	*w = &urc_widgets[menu->firstWidget + i];

		// the load and save menu reads the saved games as it comes up
		if ( w->cls == WC_LOADGAME ) {
			urc_saveWidget = menu->firstWidget + i;
			URC_LoadSaves();
		}
		// the main menu's "backtogame" button only shows while a game is
		// active; retail's pushmenu (0x40c044) sends it "disable" then and
		// "enable" otherwise, but retail's captures show it without a map
		if ( !strcmp( menu->name, "main" ) && !strcmp( w->name, "backtogame" ) ) {
			w->disabled = clc.state < CA_ACTIVE;
		}
	}
}

void URC_PopMenu( void ) {
	if ( !urc_stackDepth ) {
		return;
	}
	URC_HideMenu( &urc_menus[urc_stack[--urc_stackDepth]] );
	urc_popupWidget = -1;
	urc_dragSlider = -1;
	urc_sliderPress = -1;
	urc_pressWidget = -1;
	urc_scrollWidget = -1;
	urc_scrollHeld = SCROLL_NONE;
	urc_bindList.waiting = qfalse;
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
=======================================================================

INTRO

=======================================================================
*/

/*
=================
URC_IntroStep

The retail "_intro" command (0x40e0b0) runs the intro one step at a time,
each step ending in "wait <ms>; _intro" (retail's wait counts milliseconds,
0x42c590): the title theme and 3 seconds of black, then
textures/menu/ritual and textures/menu/gathering (256x256, drawn at 1.5
times that) each fading in over a second, held for 3 and fading out over
one, and then the main menu (0x40b7c0). Step -1, which the update sets when
a menu comes up or a map loads, ends it. OpenFAKK2's wait counts frames,
so the steps are timed here instead of in the command buffer.
=================
*/
static void URC_IntroShow( qhandle_t shader, float alphaFrom, float alphaTo, int fadeTime ) {
	urc_introShader = shader;
	urc_introAlphaFrom = alphaFrom;
	urc_introAlphaTo = alphaTo;
	urc_introFadeTime = fadeTime;
	urc_introFadeStart = cls.realtime;
}

static void URC_IntroStep( void ) {
	int		delay;

	switch ( urc_introStep ) {
	case 0:
		MUSIC_NewSoundtrack( "music/intro" );
		MUSIC_UpdateMood( 1, 1 );		// mood_normal, as the menu's music
		delay = 3000;
		break;
	case 1:
		URC_IntroShow( urc_ritualShader, 0, 1, 1000 );
		delay = 1000;
		break;
	case 2:
		URC_IntroShow( urc_ritualShader, 1, 1, 0 );
		delay = 3000;
		break;
	case 3:
		URC_IntroShow( urc_ritualShader, 1, 0, 1000 );
		delay = 1000;
		break;
	case 4:
		URC_IntroShow( urc_gatheringShader, 0, 1, 1000 );
		delay = 1000;
		break;
	case 5:
		URC_IntroShow( urc_gatheringShader, 1, 1, 0 );
		delay = 3000;
		break;
	case 6:
		URC_IntroShow( urc_gatheringShader, 1, 0, 1000 );
		delay = 1000;
		break;
	case 7:
		URC_PushMenu( "main" );
		// fall through
	default:
		urc_introStep = 0;
		urc_introShader = 0;
		return;
	}
	urc_introStep++;
	urc_introNextTime = cls.realtime + delay;
}

static void URC_Intro_f( void ) {
	if ( urc_initialised ) {
		URC_IntroStep();
	}
}

/*
=================
URC_DoIntro_f

"dointro" (0x40e360), which UI_Init (0x40e9d0) puts in the command buffer
when developer is 0 (it opens the console otherwise): the intro from its
first step.
=================
*/
static void URC_DoIntro_f( void ) {
	if ( !urc_initialised ) {
		return;		// a map load restarting the client; it would end the intro anyway
	}
	urc_introStep = 0;
	urc_introShader = 0;
	URC_IntroStep();
}

/*
=================
URC_DrawIntro

The retail update (0x40af50), with no menu up, no map loading and the
client not active: the screen filled black (0x40acd0) and the intro's
picture (0x40ad90) centred on it, 1.5 times its pixel size, its alpha
moving from one value to the other over the step's fade time. A menu
coming up, a map loading or the client becoming active ends the intro.
=================
*/
static void URC_DrawIntro( void ) {
	float	w, h, frac;
	vec4_t	color;

	if ( !urc_introStep ) {
		return;
	}
	if ( urc_stackDepth || urc_loading || clc.state >= CA_ACTIVE || CL_Viewer_Active() ) {
		urc_introStep = 0;
		urc_introShader = 0;
		return;
	}
	if ( cls.realtime >= urc_introNextTime ) {
		URC_IntroStep();
		if ( !urc_introStep ) {
			return;		// the main menu is up
		}
	}

	re.SetColor( colorBlack );
	re.DrawBox( 0, 0, cls.glconfig.vidWidth, cls.glconfig.vidHeight );
	re.SetColor( NULL );
	if ( !urc_introShader ) {
		return;
	}
	color[0] = color[1] = color[2] = 1;
	color[3] = urc_introAlphaTo;
	if ( urc_introFadeTime ) {
		frac = ( cls.realtime - urc_introFadeStart ) / (float)urc_introFadeTime;
		frac = Com_Clamp( 0, 1, frac );
		color[3] = urc_introAlphaFrom + ( urc_introAlphaTo - urc_introAlphaFrom ) * frac;
	}
	w = re.GetShaderWidth( urc_introShader ) * URC_INTRO_SCALE;
	h = re.GetShaderHeight( urc_introShader ) * URC_INTRO_SCALE;
	re.SetColor( color );
	re.DrawStretchPic( ( cls.glconfig.vidWidth - w ) * 0.5f, ( cls.glconfig.vidHeight - h ) * 0.5f,
		w, h, 0, 0, 1, 1, urc_introShader );
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

	on = urc_hudOn && clc.state == CA_ACTIVE && cl.snap.valid && !urc_stackDepth &&
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

/*
=================
URC_CenterPrint / URC_LocationPrint

Retail keeps one print, and each of these replaces it, whichever kind was
up, starting its 4000 ms again. The centre print (0x418cc0) takes a text
beginning with '@' as the name of a picture; the location print (0x418ec0)
keeps its text as it is and only draws text (0x419770).
=================
*/
static void URC_SetPrint( qboolean located, int x, int y, const char *text, float alpha ) {
	urcPrint_t	*pr = &urc_print;

	Com_Memset( pr, 0, sizeof( *pr ) );
	pr->active = qtrue;
	pr->located = located;
	pr->x = x;
	pr->y = y;
	pr->alpha = alpha;
	pr->startTime = cls.realtime;
	if ( !located && text[0] == '@' ) {
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
	URC_SetPrint( qfalse, 320, 160, text, alpha );
}

void URC_LocationPrint( int x, int y, const char *text, float alpha ) {
	if ( !urc_initialised ) {
		return;
	}
	URC_SetPrint( qtrue, x, y, text, alpha );
}

static void URC_DrawPrint( urcPrint_t *pr ) {
	int			elapsed;
	float		alpha;
	vec4_t		color, shadow;
	urcFont_t	*font;
	float		r[4];
	int			horz, vert;

	if ( !pr->active ) {
		return;
	}
	elapsed = cls.realtime - pr->startTime;
	if ( elapsed >= URC_PRINT_TIME ) {
		pr->active = qfalse;
		return;
	}
	// retail (0x419480, 0x419770): the print's alpha scales the fades only,
	// and the time between them is drawn opaque
	if ( elapsed < URC_PRINT_FADE ) {
		alpha = 1.0f - ( URC_PRINT_FADE - elapsed ) / (float)URC_PRINT_FADE * pr->alpha;
	} else if ( elapsed > URC_PRINT_TIME - URC_PRINT_FADE ) {
		alpha = ( URC_PRINT_TIME - elapsed ) / (float)URC_PRINT_FADE * pr->alpha;
	} else {
		alpha = 1.0f;
	}
	if ( alpha < 0 ) {
		alpha = 0;
	} else if ( alpha > 1 ) {
		alpha = 1;
	}
	Vector4Set( color, 1, 1, 1, alpha );
	if ( pr->shader ) {
		// the picture at its own size, centred (0x419480: the renderer's
		// width and height of the image); "GAME SAVED" is 256 x 64
		float	w = re.GetShaderWidth( pr->shader ), h = re.GetShaderHeight( pr->shader );

		SCR_BeginHudScale( 320, 240 );
		URC_DrawPicST( 320 - w * 0.5f, 240 - h * 0.5f, w, h, 0, 0, 1, 1, pr->shader, color );
		SCR_EndHudScale();
		return;
	}
	font = URC_LoadFont( URC_PRINT_FONT );
	if ( !font ) {
		return;
	}
	// at the HUD's size, towards the point the print is placed by: the
	// middle, or a location print's x and the foot of its first line
	SCR_BeginHudScale( pr->located && pr->x != -1 ? pr->x : 320,
		pr->located && pr->y != -1 ? 480 - pr->y : 240 );
	// the text is placed in the screen's rectangle by PrintJustified
	// (0x4b03c0), as one block: centred by its widest line, every line from
	// the block's left edge. A centre print (0x419480) is centred both ways.
	// A location print (0x419770) puts the block's top left at x and at y
	// measured up from the bottom of the screen to the bottom of the first
	// line (480 - y - the font's height), the rectangle keeping the screen's
	// size; an x of -1 centres it across the screen instead, a y of -1 down it.
	// The text is drawn twice, black one unit down and right, then white
	// (retail's unit there is a screen pixel)
	r[0] = r[1] = 0;
	r[2] = 640;
	r[3] = 480;
	horz = URC_JUSTIFY_CENTER;
	vert = URC_JUSTIFY_MIDDLE;
	if ( pr->located ) {
		horz = URC_JUSTIFY_LEFT;
		vert = URC_JUSTIFY_TOP;
		if ( pr->x == -1 ) {
			horz = URC_JUSTIFY_CENTER;
		} else {
			r[0] = pr->x;
		}
		if ( pr->y == -1 ) {
			vert = URC_JUSTIFY_MIDDLE;
		} else {
			r[1] = 480 - pr->y - URC_FontHeight( font );
		}
	}
	Vector4Set( shadow, 0, 0, 0, alpha );
	r[0] += 1;
	r[1] += 1;
	URC_PrintJustified( font, r, horz, vert, pr->text, shadow );
	r[0] -= 1;
	r[1] -= 1;
	URC_PrintJustified( font, r, horz, vert, pr->text, color );
	SCR_EndHudScale();
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
	float				frac, left, width;

	if ( clc.state != CA_ACTIVE || !cl.snap.valid ) {
		return;
	}
	// both cover the whole window, wide or not
	left = SCR_ScreenLeft640();
	width = SCR_ScreenRight640() - left;
	if ( ps->blend[3] > 0 ) {
		Vector4Copy( ps->blend, color );
		if ( color[3] > 1 ) {
			color[3] = 1;
		}
		if ( ps->stats[URC_STAT_ADDFADE] && urc_addFadeShader ) {
			URC_DrawPicST( left, 0, width, 480, 0, 0, 1, 1, urc_addFadeShader, color );
		} else {
			URC_FillRect( left, 0, width, 480, color );
		}
	}
	frac = ps->stats[URC_STAT_LETTERBOX] / (float)URC_MAX_LETTERBOX_SIZE;
	if ( frac > 0 ) {
		float	h = frac * 480;

		if ( h > 240 ) {
			h = 240;
		}
		URC_FillRect( left, 0, width, h, colorBlack );
		URC_FillRect( left, 480 - h, width, h, colorBlack );
	}
}

/*
=================
URC_DrawFps

FAKK2's fps counter, drawn by the interface's root window over everything
else (fakk2.exe 0x419160) while "fps" is on: three lines in facfont-20, one font
height in from the left and ending at the bottom, the frame rate white above
23.94, yellow above 18 and red below, then the 3D view's triangles and vertexes
with the map lights in light cubes, then the whole frame's with the texels in
use in millions. With "fps 1" and a game running, a frame rate under 18 plays
sound/misc/lowframerate.wav at once and every four seconds until it is back
above 19.8 (0x4241f3).
=================
*/
static void URC_DrawFps( void ) {
	static const vec4_t	white = { 1, 1, 1, 1 };
	static const vec4_t	yellow = { 1, 1, 0, 1 };
	static const vec4_t	red = { 1, 0, 0, 1 };
	static qboolean		warned;
	static int			nextWarning;
	const urcFont_t		*font;
	int					stats[6];
	float				h, x;

	if ( !com_fps->integer ) {
		return;
	}

	if ( com_fps->integer == 1 && clc.state == CA_ACTIVE ) {
		if ( com_fpsValue > 19.8f ) {
			warned = qfalse;
		} else {
			if ( !warned && com_fpsValue < 18.0f ) {
				nextWarning = 10000;
				warned = qtrue;
			}
			if ( warned && cls.realtime > nextWarning ) {
				nextWarning = cls.realtime + 4000;
				S_StartLocalSound( S_RegisterSound( "sound/misc/lowframerate.wav", qfalse ), CHAN_MENU );
			}
		}
	}

	Com_Memset( stats, 0, sizeof( stats ) );
	if ( re.GetFrameStats ) {
		re.GetFrameStats( stats );
	}
	font = URC_LoadFont( "facfont-20" );		// the root window's (a retail capture's lines are 20 apart)
	h = URC_FontHeight( font );
	x = SCR_ScreenLeft640() + h;		// in from the window's corner, wide or not
	URC_DrawString( font, x, 480 - 3 * h, va( "FPS %4.1f", com_fpsValue ),
		com_fpsValue > 23.94f ? white : com_fpsValue > 18.0f ? yellow : red );
	URC_DrawString( font, x, 480 - 2 * h, va( "wt%5d wv%5d cl%d", stats[3], stats[4], stats[5] ), white );
	URC_DrawString( font, x, 480 - h, va( "t%5d v%5d Mtex%5.2f", stats[0], stats[1], stats[2] * 1e-6f ), white );
}

/*
=======================================================================

FLOATING WINDOWS

uilib's UIFloatingWindow (Create 0x4a1600, FrameInitialized 0x4a1700,
Draw 0x4a19a0), which the "dialog" command makes (UIDialog, 0x4ab690).
The window is filled with a widget's default background, 0.75 grey. Its
title bar is the top 20 pixels: while the window is active, filled with
its colour inside a raised edge (0x49a260) in that colour's light shade
throughout, one pixel along the top and left and two along the bottom
and right, its title white; inactive, the default background inside two
thirds of it, the title black. The title is verdana-14 at 3, 3; the
close box, 16x16 at w-18, 2, shows marlett's "r" and the minimise box
at w-36, 2 its "0", both in the title's colour. The children are laid
out in the frame 2, 20, w-4, h-22.

They do not bring the cursor up: with it up (+togglemouse, ui_showmouse
or a menu) a click raises and activates the window under it, the title
bar drags it, the close box closes it and the minimise box folds it to
its title bar and back.

=======================================================================
*/

#define URC_MAX_WINDOWS		8
#define URC_TITLE_HEIGHT	20
#define URC_TITLE_FONT		"verdana-14"	// UIFloatingWindow's Create (0x4a1600)
#define URC_BOX_FONT		"marlett"
#define URC_BOX_SIZE		16
#define URC_DIALOG_BUTTON_W	64				// UIDialog's FrameInitialized (0x4ab890)
#define URC_DIALOG_BUTTON_H	32
#define URC_DIALOG_MARGIN	6				// 0x4d633c

typedef enum {
	WIN_DIALOG,
	WIN_TESTLIST,
	WIN_NOTEPAD,
	WIN_FILEPICKER
} urcWindowKind_t;

typedef enum {
	WINPART_NONE, WINPART_TITLE, WINPART_CLOSE, WINPART_MINIMIZE, WINPART_OK, WINPART_CANCEL, WINPART_CLIENT
} urcWindowPart_t;

typedef struct {
	urcWindowKind_t	kind;
	char			title[64];
	float			rect[4];
	vec4_t			color;				// the title bar's while active
	qboolean		minimized;
	// a dialog: the label's cvar, the Ok button's command, and the label's,
	// Ok's and Cancel's materials, which take the place of their text
	char			cvar[64];
	char			command[256];
	qhandle_t		material[3];
	// a list: the first row shown, the selected row (1.., 0 for none), what
	// the left button holds on its scroll bar and where on the thumb
	int				top, selected;
	urcScrollPart_t	held;
	float			grab;
	void			*data;				// a notepad's or a file picker's (Z_Malloc), freed with the window
} urcWindow_t;

static urcWindow_t		urc_windows[URC_MAX_WINDOWS];	// bottom to top
static int				urc_numWindows;
static int				urc_activeWindow = -1;
static int				urc_windowPress = -1;		// the window whose button or box the left button holds
static urcWindowPart_t	urc_windowPressPart;
static int				urc_windowDrag = -1;		// the window whose title bar it holds
static float			urc_windowGrab[2];

static const vec4_t		urc_windowBack = { 0.75f, 0.75f, 0.75f, 1 };	// UIWidget's default background

qboolean URC_HasWindows( void ) {
	return urc_initialised && urc_numWindows > 0;
}

// the frame the children are laid out in
static void URC_WindowClient( const urcWindow_t *win, float r[4] ) {
	r[0] = win->rect[0] + 2;
	r[1] = win->rect[1] + URC_TITLE_HEIGHT;
	r[2] = win->rect[2] - 4;
	r[3] = win->rect[3] - URC_TITLE_HEIGHT - 2;
}

static void URC_WindowBox( const urcWindow_t *win, urcWindowPart_t part, float r[4] ) {
	r[0] = win->rect[0] + win->rect[2] - ( part == WINPART_CLOSE ? 18 : 36 );
	r[1] = win->rect[1] + 2;
	r[2] = URC_BOX_SIZE;
	r[3] = URC_BOX_SIZE;
}

// a dialog's Ok and Cancel: 64x32, 6 in from the client frame's bottom
// left and bottom right
static void URC_DialogButton( const urcWindow_t *win, urcWindowPart_t part, float r[4] ) {
	float	c[4];

	URC_WindowClient( win, c );
	r[0] = c[0] + ( part == WINPART_CANCEL ? c[2] - URC_DIALOG_BUTTON_W - URC_DIALOG_MARGIN : URC_DIALOG_MARGIN );
	r[1] = c[1] + c[3] - URC_DIALOG_BUTTON_H - URC_DIALOG_MARGIN;
	r[2] = URC_DIALOG_BUTTON_W;
	r[3] = URC_DIALOG_BUTTON_H;
}

static qboolean URC_PointIn( const float r[4], float x, float y ) {
	return x >= r[0] && x < r[0] + r[2] && y >= r[1] && y < r[1] + r[3];
}

static float URC_WindowHeight( const urcWindow_t *win ) {
	return win->minimized ? URC_TITLE_HEIGHT : win->rect[3];
}

// the part of a window a point is on
static urcWindowPart_t URC_WindowPartAt( const urcWindow_t *win, float x, float y ) {
	float	r[4];

	Vector4Set( r, win->rect[0], win->rect[1], win->rect[2], URC_WindowHeight( win ) );
	if ( !URC_PointIn( r, x, y ) ) {
		return WINPART_NONE;
	}
	URC_WindowBox( win, WINPART_CLOSE, r );
	if ( URC_PointIn( r, x, y ) ) {
		return WINPART_CLOSE;
	}
	if ( win->kind != WIN_DIALOG ) {
		URC_WindowBox( win, WINPART_MINIMIZE, r );
		if ( URC_PointIn( r, x, y ) ) {
			return WINPART_MINIMIZE;
		}
	}
	if ( y < win->rect[1] + URC_TITLE_HEIGHT ) {
		return WINPART_TITLE;
	}
	if ( win->kind == WIN_DIALOG ) {
		URC_DialogButton( win, WINPART_OK, r );
		if ( URC_PointIn( r, x, y ) ) {
			return WINPART_OK;
		}
		URC_DialogButton( win, WINPART_CANCEL, r );
		if ( URC_PointIn( r, x, y ) ) {
			return WINPART_CANCEL;
		}
	}
	return WINPART_CLIENT;
}

static int URC_WindowAt( float x, float y ) {
	int		i;

	for ( i = urc_numWindows - 1 ; i >= 0 ; i-- ) {
		if ( URC_WindowPartAt( &urc_windows[i], x, y ) != WINPART_NONE ) {
			return i;
		}
	}
	return -1;
}

static urcWindow_t *URC_NewWindow( urcWindowKind_t kind, const char *title, float x, float y, float w, float h, const vec4_t color ) {
	urcWindow_t	*win;

	if ( urc_numWindows == URC_MAX_WINDOWS ) {
		Com_Printf( "Too many windows\n" );
		return NULL;
	}
	win = &urc_windows[urc_numWindows++];
	Com_Memset( win, 0, sizeof( *win ) );
	win->kind = kind;
	Q_strncpyz( win->title, title, sizeof( win->title ) );
	Vector4Set( win->rect, x, y, w, h );
	Vector4Copy( color, win->color );
	return win;
}

static void URC_FreeWindowData( urcWindow_t *win );

static void URC_CloseWindow( int index ) {
	if ( index < 0 || index >= urc_numWindows ) {
		return;
	}
	URC_FreeWindowData( &urc_windows[index] );
	memmove( &urc_windows[index], &urc_windows[index + 1], ( urc_numWindows - index - 1 ) * sizeof( urc_windows[0] ) );
	urc_numWindows--;
	if ( urc_activeWindow == index ) {
		urc_activeWindow = -1;
	} else if ( urc_activeWindow > index ) {
		urc_activeWindow--;
	}
	urc_windowPress = urc_windowDrag = -1;
}

static void URC_CloseAllWindows( void ) {
	while ( urc_numWindows ) {
		URC_FreeWindowData( &urc_windows[--urc_numWindows] );
	}
	urc_activeWindow = urc_windowPress = urc_windowDrag = -1;
}

// to the top, and active (0x498c10); returns its new index
static int URC_ActivateWindow( int index ) {
	urcWindow_t	win = urc_windows[index];

	memmove( &urc_windows[index], &urc_windows[index + 1], ( urc_numWindows - index - 1 ) * sizeof( urc_windows[0] ) );
	urc_windows[urc_numWindows - 1] = win;
	urc_activeWindow = urc_numWindows - 1;
	return urc_activeWindow;
}

// a dialog's Ok or Cancel: raised in the default background, its text
// (a held button's a pixel down and right) or its material
static void URC_DrawDialogButton( const urcWindow_t *win, int index, urcWindowPart_t part, const char *text, qhandle_t material ) {
	static const vec4_t	white = { 1, 1, 1, 1 };
	vec4_t				colors[4];
	float				r[4];

	URC_DialogButton( win, part, r );
	URC_BorderColors( urc_windowBack, colors );
	URC_Draw3DBox( r, qfalse, colors, 1 );
	if ( material ) {
		URC_DrawPicST( r[0], r[1], r[2], r[3], 0, 0, 1, 1, material, white );
		return;
	}
	if ( urc_windowPress == index && urc_windowPressPart == part && URC_WindowPartAt( win, urc_cursorX, urc_cursorY ) == part ) {
		r[0] += 1;
		r[1] += 1;
	}
	URC_PrintJustified( URC_LoadFont( URC_DEFAULT_FONT ), r, URC_JUSTIFY_CENTER, URC_JUSTIFY_MIDDLE, text, white );
}

// a dialog's label: its cvar's value centred over the client frame (its
// frame, 0x4ab8f4). Retail's line of text lands 6 pixels above that frame's
// centre in a capture, as if it were 12 shorter; where that comes from was
// not found, so the text is centred over the frame less 12 at the bottom.
static void URC_DrawDialog( const urcWindow_t *win, int index ) {
	static const vec4_t	white = { 1, 1, 1, 1 };
	float				c[4];

	URC_WindowClient( win, c );
	if ( win->material[0] ) {
		URC_DrawPicST( c[0], c[1], c[2], c[3], 0, 0, 1, 1, win->material[0], white );
	} else {
		c[3] -= 12;
		URC_PrintJustified( URC_LoadFont( URC_DEFAULT_FONT ), c, URC_JUSTIFY_CENTER, URC_JUSTIFY_MIDDLE, Cvar_VariableString( win->cvar ), white );
	}
	URC_DrawDialogButton( win, index, WINPART_OK, "Ok", win->material[1] );
	URC_DrawDialogButton( win, index, WINPART_CANCEL, "Cancel", win->material[2] );
}

/*
=================
The test list

ui_testlist (0x40d950): a UIListCtrl (0x4ac830) over the client frame in
verdana-12, its colour white, with the columns "Name" 100, "Ping" 64 and
"IP" 64 wide and a hundred times the rows ("Item 1", "123", "1"),
("Item 2", "60", "2"), ("Item 3", "-5", "3"). It is drawn as the load
list is (0x4ace40): the headers 16 tall, 0.3 grey inside white lines, their
titles white at 2, 2; a row per font height in the list's colours, white
on white here, so retail's rows cannot be seen; the selected row black on
white; the scroll bar down the right.
=================
*/
#define URC_TESTLIST_ROWS	300

static const char	*urc_testColumns[3] = { "Name", "Ping", "IP" };
static const int	urc_testWidths[3] = { 100, 64, 64 };
static const char	*urc_testCells[3][3] = { { "Item 1", "123", "1" }, { "Item 2", "60", "2" }, { "Item 3", "-5", "3" } };

static int URC_TestListPage( const float c[4] ) {
	int		rows = (int)( ( c[3] - URC_SAVE_HEADER ) / URC_FontHeight( URC_LoadFont( URC_DEFAULT_FONT ) ) );

	return rows < 1 ? 1 : rows;
}

static void URC_TestListClamp( urcWindow_t *win ) {
	float	c[4];
	int		page;

	URC_WindowClient( win, c );
	page = URC_TestListPage( c );
	if ( win->top > URC_TESTLIST_ROWS - page ) {
		win->top = URC_TESTLIST_ROWS - page;
	}
	if ( win->top < 0 ) {
		win->top = 0;
	}
}

static void URC_DrawTestList( const urcWindow_t *win, int index ) {
	static const vec4_t	headerFill = { 0.3f, 0.3f, 0.3f, 1 };
	static const vec4_t	white = { 1, 1, 1, 1 };
	static const vec4_t	black = { 0, 0, 0, 1 };
	urcFont_t			*font = URC_LoadFont( URC_DEFAULT_FONT );
	float				c[4], r[4], saved[4], x, y, rowH = URC_FontHeight( font );
	int					col, i;

	URC_WindowClient( win, c );
	URC_PushClip( c, saved );
	URC_Box( c[0], c[1], c[2], c[3], white, 1 );

	x = 0;
	for ( col = 0 ; col < 3 ; col++ ) {
		Vector4Set( r, c[0] + x, c[1], urc_testWidths[col] + 1, URC_SAVE_HEADER );
		URC_DrawBorder( r, headerFill, white, 1, 3, 1 );
		URC_DrawString( font, r[0] + 2, r[1] + 2, urc_testColumns[col], white );
		x += urc_testWidths[col];
	}
	Vector4Set( r, c[0] + x, c[1], c[2] - x + 1, URC_SAVE_HEADER );
	URC_DrawBorder( r, headerFill, white, 1, 3, 1 );

	for ( i = win->top, y = URC_SAVE_HEADER ; i < URC_TESTLIST_ROWS && y <= c[3] ; i++, y += rowH ) {
		const float	*text = i + 1 == win->selected ? black : white;

		x = 0;
		for ( col = 0 ; col < 3 ; col++ ) {
			URC_DrawString( font, c[0] + x, c[1] + y, urc_testCells[i % 3][col], text );
			x += urc_testWidths[col];
		}
	}

	URC_DrawScrollBar( c, URC_TESTLIST_ROWS, URC_TestListPage( c ), win->top,
		urc_windowPress == index ? win->held : SCROLL_NONE, 1 );
	URC_PopClip( saved );
}

// a click in the test list: its scroll bar, or a row to select
static void URC_TestListClick( urcWindow_t *win ) {
	float	c[4];
	int		page, row;

	URC_WindowClient( win, c );
	page = URC_TestListPage( c );
	win->held = URC_ScrollPart( c, URC_TESTLIST_ROWS, page, win->top, urc_cursorX, urc_cursorY );
	switch ( win->held ) {
	case SCROLL_UP: win->top--; break;
	case SCROLL_DOWN: win->top++; break;
	case SCROLL_PAGEUP: win->top -= page; break;
	case SCROLL_PAGEDOWN: win->top += page; break;
	case SCROLL_THUMB:
		{
			float	sr[4], ty, th;

			URC_ScrollRect( c, sr );
			URC_ScrollThumb( sr, URC_TESTLIST_ROWS, page, win->top, &ty, &th );
			win->grab = urc_cursorY - ( sr[1] + ty );
		}
		break;
	default:
		if ( urc_cursorX < c[0] + c[2] - URC_SCROLL_WIDTH && urc_cursorY >= c[1] + URC_SAVE_HEADER ) {
			row = win->top + (int)( ( urc_cursorY - c[1] - URC_SAVE_HEADER ) / URC_FontHeight( URC_LoadFont( URC_DEFAULT_FONT ) ) ) + 1;
			if ( row >= 1 && row <= URC_TESTLIST_ROWS ) {
				win->selected = row;
			}
		}
		break;
	}
	URC_TestListClamp( win );
}

/*
=================
The notepad

notepad [file] (fakk2.exe 0x40c550, 0x4a7d20): a UINotepad, a floating
window at 100, 100, 450 by 300 in 0.1 grey, titled "<file> - Notepad"
(0x4a9dd0), that opens the file through the game's file system
(0x4a8180; nothing opens, and nothing is said, when it cannot be read).
Its client frame (Create, 0x4a8200; laid out at 0x4a8ac0) holds a menu
bar at the top, a UIStatusBar 20 tall at the bottom and between them a
UINotepadEdit, a UIMultiLineEdit in verdana-14, black on white:

  File: Open (Ctrl-O), Save (Ctrl-S), Save As...(Ctrl-A), a separator,
        Close (Ctrl-W)
  Edit: Copy (Ctrl-C), Cut (Ctrl-X), Paste (Ctrl-V), a separator,
        Find (Ctrl-F), Go to line... (Ctrl-G)

The shortcuts are the control characters (0x4a8fd0, the table 0x4fcdb0).
The status bar (drawn at 0x4a8b70) shows "Line %d, column %d", black on
the window's grey; a prompt white on half-transparent dark red (red at
half, 0x4d53dc), what is typed after it: "Goto line (tab cancels): ",
"Find text (tab cancels): " (the last text found already there),
"Save As (tab cancels): " and "Save <file>? (yes/no/cancel) " ("Y"
there); and for 3 seconds a message in red. While a prompt is up the
characters are its (0x4a9090): backspace, enter, escape or tab cancels,
printable ones are added (the yes/no/cancel prompt keeps one). Enter on
a line number selects that line or says "Line '<n>' doesn't exist."; on a
text, finds it from after the caret, ignoring case, wrapping to the top
once (0x4a96c0), and selects it, the prompt staying for the next, or says
"Text: '<text>' could not be found."; on a name, saves to it. Save
(0x4a9bf0) without a name asks for one; it writes through the file system
and says "Saved <file>". Close (0x4a9f30) asks the yes/no/cancel question
when the text changed since it was opened or saved: n closes, y saves
and closes. Open lists files in a File Picker (below).

The edit (UIMultiLineEdit, drawn at 0x4b7530): lines from the scroll
bar's first, at the left edge; the selection white on dark blue (0, 0,
0.5); the caret, while the window is active, a 2 pixel bar the font's
height, shown for the second half of every 750 ms. Printable characters
and tab are typed over the selection, backspace and enter work on it
(0x4b8770). A click in it places the caret and cancels a prompt
(0x4a9aa0), a drag selects.

The edit's keys (UIMultiLineEdit, 0x4b80e0) work on the caret's line and
column: up and down keep the column; left and right go on over a line's
end; home and end go to the line's ends, with control to the text's;
page up and down go first to the top or bottom line shown, then a page;
the wheel scrolls 2 lines. Shift held keeps the selection's other end,
or the caret's move takes it along. Backspace with nothing selected
selects the character before and deletes it; delete with nothing
selected only selects the character after (a second delete deletes it);
shift with delete copies first. With something selected, control and
insert copy, shift and insert paste. Copy and paste are the system's
clipboard (0x458560, 0x4584e0); a paste is typed, character by
character, through the notepad (0x4b8e10). A click (0x4b7ec0) puts the
caret and the selection's other end at the character it is on, a drag
moves the caret, scrolling every quarter second while it is beyond the
text shown (0x4b7f80, 0x4b7fe0). The window's close box is Close
(floatingwindow_close_pressed, 0x4a12cb), and the status bar's corner is
a UIWindowSizer (0x4bce40) that resizes the window, to at least 32 by 16.
=================
*/
#define URC_NP_X				100
#define URC_NP_Y				100
#define URC_NP_W				450
#define URC_NP_H				300
#define URC_NP_BAR_H			18			// the menu bar: verdana-14 and 2 above and below
#define URC_NP_STATUS_H			20			// 0x4d5fa8
#define URC_NP_FONT				"verdana-14"
#define URC_NP_STATUS_FONT		"verdana-12"
#define URC_NP_MESSAGE_TIME		3000		// 0xbb8
#define URC_NP_GRIP				16			// UIWindowSizer, the status bar's bottom right 16 by 16 (0x4bce40)
#define URC_NP_MIN_W			32			// 0x4bcbff
#define URC_NP_MIN_H			16
#define URC_NP_SEPARATOR_H		8			// UIPopupMenu's item height for a separator (0x4bfab0)
#define URC_NP_SCROLL_TIME		250			// a drag past the text scrolls again after (0x4b7fe0)
#define URC_NP_POPUP_PAD		4			// UIPopupMenu's, as the pulldowns' lists (URC_DrawPopup)

typedef enum {
	NP_EDIT, NP_GOTO, NP_FIND, NP_SAVEAS, NP_MESSAGE, NP_ASKSAVE		// 0x1b8: 0 to 5
} urcNotepadMode_t;

typedef enum {
	NPC_NONE, NPC_OPEN, NPC_SAVE, NPC_SAVEAS, NPC_CLOSE, NPC_COPY, NPC_CUT, NPC_PASTE, NPC_FIND, NPC_GOTO
} urcNotepadCmd_t;

typedef struct {
	char				*text;				// lines joined by \n
	int					len, size;
	int					cursor, anchor;		// the caret and the selection's other end
	int					top;				// the first line shown
	qboolean			modified;			// the edit's 0x196
	char				file[MAX_QPATH];
	urcNotepadMode_t	mode;
	char				input[256];
	char				message[320];
	int					messageEnd;
	char				lastFind[256];
	int					menu;				// the menu bar's open menu: -1, 0 File, 1 Edit
	qboolean			selecting, resizing;
	int					scrollTime;			// a drag's next scroll, 0 none
	urcScrollPart_t		held;
	float				grab[2];
} urcNotepad_t;

typedef struct {
	const char		*text;			// "" a separator
	urcNotepadCmd_t	cmd;
} urcNotepadItem_t;

static const char				*urc_npMenuTitles[2] = { "File", "Edit" };
static const urcNotepadItem_t	urc_npMenus[2][6] = {
	{ { "Open (Ctrl-O)", NPC_OPEN }, { "Save (Ctrl-S)", NPC_SAVE }, { "Save As...(Ctrl-A)", NPC_SAVEAS },
	  { "", NPC_NONE }, { "Close (Ctrl-W)", NPC_CLOSE }, { NULL, NPC_NONE } },
	{ { "Copy (Ctrl-C)", NPC_COPY }, { "Cut (Ctrl-X)", NPC_CUT }, { "Paste (Ctrl-V)", NPC_PASTE },
	  { "", NPC_NONE }, { "Find (Ctrl-F)", NPC_FIND }, { "Go to line... (Ctrl-G)", NPC_GOTO } }
};
static const struct {
	char			letter;
	urcNotepadCmd_t	cmd;
} urc_npShortcuts[] = {		// 0x4fcdb0
	{ 'o', NPC_OPEN }, { 's', NPC_SAVE }, { 'a', NPC_SAVEAS }, { 'w', NPC_CLOSE }, { 'f', NPC_FIND },
	{ 'g', NPC_GOTO }, { 'c', NPC_COPY }, { 'x', NPC_CUT }, { 'v', NPC_PASTE }
};


// the file picker (below)
#define URC_PICKER_X	100
#define URC_PICKER_Y	50
#define URC_PICKER_W	400
#define URC_PICKER_H	300

typedef struct {
	char		name[MAX_QPATH];
	qboolean	dir;
	char		date[24];			// "" when the file is not on the disk (in a pk3)
	char		size[16];
} urcPickEntry_t;

typedef struct {
	char			dir[MAX_QPATH];		// "" or ending in '/'
	void			*owner;				// the notepad's data
	urcPickEntry_t	*entries;
	int				num;
	int				clickTime, clickRow;
	float			clickX, clickY;
} urcPicker_t;

static int URC_PickerRows( const float c[4] ) {
	int		rows = (int)( ( c[3] - URC_SAVE_HEADER ) / URC_FontHeight( URC_LoadFont( URC_DEFAULT_FONT ) ) );

	return rows < 1 ? 1 : rows;
}


static void URC_OpenPicker( const char *dir, void *owner );

// the text

static int URC_NpLineStart( const urcNotepad_t *np, int pos ) {
	while ( pos > 0 && np->text[pos - 1] != '\n' ) {
		pos--;
	}
	return pos;
}

static int URC_NpLineEnd( const urcNotepad_t *np, int pos ) {
	while ( pos < np->len && np->text[pos] != '\n' ) {
		pos++;
	}
	return pos;
}

static int URC_NpLineOf( const urcNotepad_t *np, int pos ) {
	int		i, line = 0;

	for ( i = 0 ; i < pos && i < np->len ; i++ ) {
		line += ( np->text[i] == '\n' );
	}
	return line;
}

static int URC_NpNumLines( const urcNotepad_t *np ) {
	return URC_NpLineOf( np, np->len ) + 1;
}

// where a line begins, or -1 past the last
static int URC_NpLinePos( const urcNotepad_t *np, int line ) {
	int		pos = 0;

	while ( line > 0 ) {
		while ( pos < np->len && np->text[pos] != '\n' ) {
			pos++;
		}
		if ( pos >= np->len ) {
			return -1;
		}
		pos++;
		line--;
	}
	return pos;
}

static void URC_NpReserve( urcNotepad_t *np, int extra ) {
	char	*text;
	int		size;

	if ( np->len + extra + 1 <= np->size ) {
		return;
	}
	size = ( np->len + extra + 1 ) * 2;
	if ( size < 1024 ) {
		size = 1024;
	}
	text = Z_Malloc( size );
	if ( np->text ) {
		Com_Memcpy( text, np->text, np->len );
		Z_Free( np->text );
	}
	np->text = text;
	np->size = size;
	np->text[np->len] = 0;
}

static qboolean URC_NpDeleteSelection( urcNotepad_t *np ) {
	int		a = np->cursor < np->anchor ? np->cursor : np->anchor;
	int		b = np->cursor < np->anchor ? np->anchor : np->cursor;

	if ( a == b ) {
		return qfalse;
	}
	memmove( np->text + a, np->text + b, np->len - b + 1 );
	np->len -= b - a;
	np->cursor = np->anchor = a;
	np->modified = qtrue;
	return qtrue;
}

// over the selection, at the caret; carriage returns are dropped
static void URC_NpInsert( urcNotepad_t *np, const char *s, int n ) {
	int		i, j;

	URC_NpDeleteSelection( np );
	URC_NpReserve( np, n );
	memmove( np->text + np->cursor + n, np->text + np->cursor, np->len - np->cursor + 1 );
	for ( i = j = 0 ; i < n ; i++ ) {
		if ( s[i] != '\r' ) {
			np->text[np->cursor + j++] = s[i];
		}
	}
	if ( j < n ) {
		memmove( np->text + np->cursor + j, np->text + np->cursor + n, np->len - np->cursor + 1 );
	}
	np->len += j;
	np->cursor += j;
	np->anchor = np->cursor;
	np->modified = qtrue;
}

static void URC_NpSetText( urcNotepad_t *np, const char *s ) {
	np->len = 0;
	np->cursor = np->anchor = np->top = 0;
	URC_NpInsert( np, s, strlen( s ) );
	np->cursor = np->anchor = 0;
	np->modified = qfalse;
}

// the selection as a new string (Z_Malloc), or NULL
static char *URC_NpSelection( const urcNotepad_t *np ) {
	int		a = np->cursor < np->anchor ? np->cursor : np->anchor;
	int		b = np->cursor < np->anchor ? np->anchor : np->cursor;
	char	*s;

	if ( a == b ) {
		return NULL;
	}
	s = Z_Malloc( b - a + 1 );
	Com_Memcpy( s, np->text + a, b - a );
	s[b - a] = 0;
	return s;
}

// the window's parts

static void URC_NpRects( const urcWindow_t *win, float bar[4], float edit[4], float status[4] ) {
	float	c[4];

	URC_WindowClient( win, c );
	Vector4Set( bar, c[0], c[1], c[2], URC_NP_BAR_H );
	Vector4Set( status, c[0], c[1] + c[3] - URC_NP_STATUS_H, c[2], URC_NP_STATUS_H );
	Vector4Set( edit, c[0], c[1] + URC_NP_BAR_H, c[2], c[3] - URC_NP_BAR_H - URC_NP_STATUS_H );
}

static int URC_NpRows( const float edit[4] ) {
	int		rows = (int)( edit[3] / URC_FontHeight( URC_LoadFont( URC_NP_FONT ) ) );

	return rows < 1 ? 1 : rows;
}

// a menu bar title, 4 either side of its text
static void URC_NpTitleRect( const float bar[4], int menu, float r[4] ) {
	urcFont_t	*font = URC_LoadFont( URC_NP_FONT );
	float		x = bar[0];
	int			i;

	for ( i = 0 ; i < menu ; i++ ) {
		x += URC_StringWidth( font, urc_npMenuTitles[i] ) + 8;
	}
	Vector4Set( r, x, bar[1], URC_StringWidth( font, urc_npMenuTitles[menu] ) + 8, bar[3] );
}

static int URC_NpNumItems( int menu ) {
	int		n = 0;

	while ( n < 6 && urc_npMenus[menu][n].text ) {
		n++;
	}
	return n;
}

// an open menu's list: as UIPopupMenu's (URC_PopupRect), below its title
static void URC_NpMenuRect( const float bar[4], int menu, float r[4] ) {
	urcFont_t	*font = URC_LoadFont( URC_POPUP_FONT );
	float		title[4], width = 0;
	int			i;

	URC_NpTitleRect( bar, menu, title );
	for ( i = 0 ; i < URC_NpNumItems( menu ) ; i++ ) {
		float	w = URC_StringWidth( font, urc_npMenus[menu][i].text );

		if ( w > width ) {
			width = w;
		}
	}
	Vector4Set( r, title[0], bar[1] + bar[3], width + 24, 2 * URC_NP_POPUP_PAD );
	for ( i = 0 ; i < URC_NpNumItems( menu ) ; i++ ) {
		r[3] += urc_npMenus[menu][i].text[0] ? URC_FontHeight( font ) : URC_NP_SEPARATOR_H;
	}
}

// the item of an open menu at a height, or -1
static int URC_NpMenuItemAt( const float r[4], int menu, float y ) {
	float	h, top = r[1] + URC_NP_POPUP_PAD;
	int		i;

	for ( i = 0 ; i < URC_NpNumItems( menu ) ; i++, top += h ) {
		h = urc_npMenus[menu][i].text[0] ? URC_FontHeight( URC_LoadFont( URC_POPUP_FONT ) ) : URC_NP_SEPARATOR_H;
		if ( y >= top && y < top + h ) {
			return i;
		}
	}
	return -1;
}

// the size grip: the status bar's bottom right corner
static void URC_NpGripRect( const float status[4], float r[4] ) {
	Vector4Set( r, status[0] + status[2] - URC_NP_GRIP, status[1] + status[3] - URC_NP_GRIP, URC_NP_GRIP, URC_NP_GRIP );
}

static void URC_NpClampTop( urcNotepad_t *np, const float edit[4] ) {
	int		rows = URC_NpRows( edit ), lines = URC_NpNumLines( np );

	if ( np->top > lines - rows ) {
		np->top = lines - rows;
	}
	if ( np->top < 0 ) {
		np->top = 0;
	}
}

static void URC_NpShowCaret( urcNotepad_t *np, const urcWindow_t *win ) {
	float	bar[4], edit[4], status[4];
	int		line = URC_NpLineOf( np, np->cursor ), rows;

	URC_NpRects( win, bar, edit, status );
	rows = URC_NpRows( edit );
	if ( line < np->top ) {
		np->top = line;
	} else if ( line >= np->top + rows ) {
		np->top = line - rows + 1;
	}
	URC_NpClampTop( np, edit );
}

// a position as the edit's line and column, and back, the line kept in
// the text and the column in the line (0x4b8080)
static void URC_NpLineCol( const urcNotepad_t *np, int pos, int *line, int *col ) {
	*line = URC_NpLineOf( np, pos );
	*col = pos - URC_NpLineStart( np, pos );
}

static int URC_NpPos( const urcNotepad_t *np, int line, int col ) {
	int		pos, end;

	if ( line >= URC_NpNumLines( np ) ) {
		line = URC_NpNumLines( np ) - 1;
	}
	if ( line < 0 ) {
		line = 0;
	}
	pos = URC_NpLinePos( np, line );
	end = URC_NpLineEnd( np, pos );
	if ( col > end - pos ) {
		col = end - pos;
	}
	if ( col < 0 ) {
		col = 0;
	}
	return pos + col;
}

/*
=================
URC_NpPosAt

The position at a point in the edit (0x4b7dc0): the line the point's
height falls in, counted from the first shown and cut toward zero; along
it, the characters up to the one the point is on, less that one when the
point is on its left half, unless the line ran out first.
=================
*/
static int URC_NpPosAt( const urcNotepad_t *np, const float edit[4], float x, float y ) {
	urcFont_t	*font = URC_LoadFont( URC_NP_FONT );
	int			line = (int)( ( y - edit[1] ) / URC_FontHeight( font ) + np->top );
	int			pos, end, i = 0;
	float		w = 0, cw = 0;

	if ( line >= URC_NpNumLines( np ) ) {
		line = URC_NpNumLines( np ) - 1;
	}
	if ( line < 0 ) {
		return 0;
	}
	pos = URC_NpLinePos( np, line );
	end = URC_NpLineEnd( np, pos );
	x -= edit[0];
	while ( pos + i < end && w < x ) {
		cw = URC_CharWidth( font, np->text[pos + i] );
		w += cw;
		i++;
	}
	if ( pos + i < end && i && w - cw * 0.5f >= x ) {
		i--;
	}
	return pos + i;
}

// the x of a position within its line
static float URC_NpPosX( const urcNotepad_t *np, int pos ) {
	urcFont_t	*font = URC_LoadFont( URC_NP_FONT );
	int			i = URC_NpLineStart( np, pos );
	float		w = 0;

	for ( ; i < pos ; i++ ) {
		w += URC_CharWidth( font, np->text[i] );
	}
	return w;
}

static void URC_NpSetTitle( urcWindow_t *win, const urcNotepad_t *np ) {
	Com_sprintf( win->title, sizeof( win->title ), "%s - Notepad", np->file );		// 0x4a9dd0, " - Notepad"
}

static void URC_NpMessage( urcNotepad_t *np, const char *text ) {		// 0x4a8ee0
	np->mode = NP_MESSAGE;
	np->messageEnd = cls.realtime + URC_NP_MESSAGE_TIME;
	Q_strncpyz( np->message, text, sizeof( np->message ) );
}

static void URC_NpPrompt( urcNotepad_t *np, urcNotepadMode_t mode, const char *input ) {
	np->mode = mode;
	Q_strncpyz( np->input, input, sizeof( np->input ) );
}

static qboolean URC_NpOpen( urcWindow_t *win, const char *path ) {		// 0x4a8180
	urcNotepad_t	*np = win->data;
	char			*buf;

	if ( !path || !path[0] || FS_ReadFile( path, (void **)&buf ) < 0 || !buf ) {
		return qfalse;
	}
	Q_strncpyz( np->file, path, sizeof( np->file ) );
	URC_NpSetTitle( win, np );
	URC_NpSetText( np, buf );
	FS_FreeFile( buf );
	return qtrue;
}

static void URC_NpSave( urcNotepad_t *np ) {		// 0x4a9bf0
	if ( !np->file[0] ) {
		URC_NpPrompt( np, NP_SAVEAS, "" );
		return;
	}
	np->modified = qfalse;
	FS_WriteFile( np->file, np->text ? np->text : "", np->len );
	URC_NpMessage( np, va( "Saved %s", np->file ) );
}

// selects a line by its number, from 1 (0x4a9650)
static qboolean URC_NpGotoLine( urcWindow_t *win, int line ) {
	urcNotepad_t	*np = win->data;
	int				pos;

	if ( line < 1 || ( pos = URC_NpLinePos( np, line - 1 ) ) < 0 ) {
		return qfalse;
	}
	np->cursor = pos;
	np->anchor = URC_NpLineEnd( np, pos );
	URC_NpShowCaret( np, win );
	return qtrue;
}

// from skip after the caret to the end, then from the top, ignoring case (0x4a96c0)
static qboolean URC_NpFind( urcWindow_t *win, const char *text, int skip ) {
	urcNotepad_t	*np = win->data;
	int				n = strlen( text ), start = np->cursor + skip, pass, i;

	if ( !n ) {
		return qfalse;
	}
	for ( pass = 0 ; pass < 2 ; pass++ ) {
		for ( i = pass ? 0 : start ; i + n <= np->len ; i++ ) {
			if ( !Q_stricmpn( np->text + i, text, n ) ) {
				np->cursor = i;
				np->anchor = i + n;
				URC_NpShowCaret( np, win );
				return qtrue;
			}
		}
	}
	return qfalse;
}

// the selection to the system's clipboard (0x4b8b60)
static void URC_NpCopy( const urcNotepad_t *np ) {
	char	*s = URC_NpSelection( np );

	if ( s ) {
		Sys_SetClipboardText( s );
		Z_Free( s );
	}
}

static qboolean URC_NotepadChar( int index, int c );

// the system's clipboard typed into the notepad, a newline as enter, over
// the selection (0x4b8e10)
static void URC_NpPaste( int index ) {
	char	*text = Sys_GetClipboardText(), *s;

	URC_NpDeleteSelection( urc_windows[index].data );
	if ( !text ) {
		return;
	}
	for ( s = text ; *s && index < urc_numWindows && urc_windows[index].kind == WIN_NOTEPAD ; s++ ) {
		URC_NotepadChar( index, *s == '\n' ? 13 : (unsigned char)*s );
	}
	Z_Free( text );
}

static void URC_NpCommand( int index, urcNotepadCmd_t cmd ) {
	urcWindow_t		*win = &urc_windows[index];
	urcNotepad_t	*np = win->data;
	char			dir[MAX_QPATH];

	switch ( cmd ) {
	case NPC_OPEN:		// 0x4a9d40: a file picker where the file is
		Q_strncpyz( dir, np->file, sizeof( dir ) );
		*( strrchr( dir, '/' ) ? strrchr( dir, '/' ) + 1 : dir ) = 0;
		URC_OpenPicker( dir, np );
		break;
	case NPC_SAVE:
		URC_NpSave( np );
		break;
	case NPC_SAVEAS:	// 0x4a9ad0
		URC_NpPrompt( np, NP_SAVEAS, "" );
		break;
	case NPC_CLOSE:		// 0x4a9f30
		if ( np->modified ) {
			URC_NpPrompt( np, NP_ASKSAVE, "Y" );
		} else {
			URC_CloseWindow( index );
		}
		break;
	case NPC_COPY:		// 0x4aa220
		URC_NpCopy( np );
		break;
	case NPC_CUT:		// 0x4aa240
		URC_NpCopy( np );
		URC_NpDeleteSelection( np );
		break;
	case NPC_PASTE:		// 0x4aa230
		URC_NpPaste( index );
		break;
	case NPC_FIND:		// 0x4aa080
		URC_NpPrompt( np, NP_FIND, np->lastFind );
		break;
	case NPC_GOTO:		// 0x4aa100
		URC_NpPrompt( np, NP_GOTO, "" );
		break;
	default:
		break;
	}
}

// enter in a prompt (0x4a927e)
static void URC_NpEnter( int index ) {
	urcWindow_t		*win = &urc_windows[index];
	urcNotepad_t	*np = win->data;
	char			c;

	switch ( np->mode ) {
	case NP_GOTO:
		if ( !np->input[0] ) {
			np->mode = NP_EDIT;
		} else if ( URC_NpGotoLine( win, atoi( np->input ) ) ) {
			np->mode = NP_EDIT;
		} else {
			URC_NpMessage( np, va( "Line '%s' doesn't exist.", np->input ) );
		}
		break;
	case NP_FIND:
		if ( !np->input[0] ) {
			np->mode = NP_EDIT;
			break;
		}
		if ( !URC_NpFind( win, np->input, 1 ) ) {
			URC_NpMessage( np, va( "Text: '%s' could not be found.", np->input ) );
		}
		Q_strncpyz( np->lastFind, np->input, sizeof( np->lastFind ) );
		break;
	case NP_SAVEAS:
		np->mode = NP_EDIT;
		if ( np->input[0] ) {
			Q_strncpyz( np->file, np->input, sizeof( np->file ) );
			URC_NpSetTitle( win, np );
			URC_NpSave( np );
		}
		break;
	case NP_ASKSAVE:
		np->mode = NP_EDIT;
		c = tolower( np->input[0] ? np->input[0] : 'q' );
		if ( c == 'y' ) {
			URC_NpSave( np );
			URC_CloseWindow( index );
		} else if ( c == 'n' ) {
			URC_CloseWindow( index );
		}
		break;
	default:
		break;
	}
}

/*
=================
URC_NotepadChar

A character for the active notepad (0x4a9620, 0x4a9090, 0x4b8770);
returns whether it took it.
=================
*/
static qboolean URC_NotepadChar( int index, int c ) {
	urcWindow_t		*win = &urc_windows[index];
	urcNotepad_t	*np = win->data;
	int				i, n;
	char			ch;

	if ( c >= 1 && c <= 26 ) {
		for ( i = 0 ; i < (int)ARRAY_LEN( urc_npShortcuts ) ; i++ ) {
			if ( urc_npShortcuts[i].letter == c + 0x60 ) {
				np->menu = -1;
				URC_NpCommand( index, urc_npShortcuts[i].cmd );
				return qtrue;
			}
		}
	}
	if ( np->mode != NP_EDIT && np->mode != NP_MESSAGE ) {
		n = strlen( np->input );
		if ( c == 8 ) {
			if ( n ) {
				np->input[n - 1] = 0;
			}
		} else if ( c == 13 ) {
			URC_NpEnter( index );
		} else if ( c == 27 || c == 9 ) {
			np->mode = NP_EDIT;
		} else if ( c >= 0x20 && c < 0x7f ) {
			if ( np->mode == NP_ASKSAVE ) {
				n = 0;
			}
			if ( n < (int)sizeof( np->input ) - 1 ) {
				np->input[n] = c;
				np->input[n + 1] = 0;
			}
		}
		return qtrue;
	}

	// the edit (0x4b8770)
	if ( c == 8 ) {
		if ( np->cursor == np->anchor && np->cursor > 0 ) {
			np->cursor--;		// shift and left
		}
		URC_NpDeleteSelection( np );
	} else if ( c == 13 ) {
		URC_NpInsert( np, "\n", 1 );
	} else if ( c == 9 || ( c >= 0x20 && c < 0x7f ) ) {
		ch = c;
		URC_NpInsert( np, &ch, 1 );
	} else {
		return qfalse;
	}
	URC_NpShowCaret( np, win );
	return qtrue;
}

/*
=================
URC_NotepadKey

The keys that are not characters, for the active notepad; returns
whether it took the key.
=================
*/
static qboolean URC_NotepadKey( int index, int key ) {
	urcWindow_t		*win = &urc_windows[index];
	urcNotepad_t	*np = win->data;
	float			bar[4], edit[4], status[4];
	int				line, col, page, numLines = URC_NpNumLines( np );
	qboolean		shift = keys[K_SHIFT].down, ctrl = keys[K_CTRL].down, empty = np->cursor == np->anchor;

	if ( key >= K_MOUSE1 && key <= K_MOUSE5 ) {
		return qfalse;
	}
	switch ( key ) {
	case K_ENTER:
	case K_KP_ENTER:
		return URC_NotepadChar( index, 13 );
	case K_TAB:
		return URC_NotepadChar( index, 9 );
	case K_ESCAPE:
		if ( np->menu >= 0 ) {
			np->menu = -1;
			return qtrue;
		}
		return np->mode != NP_EDIT && np->mode != NP_MESSAGE && URC_NotepadChar( index, 27 );
	case K_MWHEELUP:
	case K_MWHEELDOWN:
		return qfalse;		// the window's wheel (URC_NotepadWheel)
	default:
		break;
	}

	URC_NpRects( win, bar, edit, status );
	page = URC_NpRows( edit );
	URC_NpLineCol( np, np->cursor, &line, &col );
	switch ( key ) {
	case K_UPARROW:
	case K_KP_UPARROW:
		line--;
		break;
	case K_DOWNARROW:
	case K_KP_DOWNARROW:
		line++;
		break;
	case K_LEFTARROW:
	case K_KP_LEFTARROW:
		if ( --col < 0 ) {
			line--;
			col = 999999;		// 0xf423f
			if ( line < 0 ) {
				col = 0;
			}
		}
		break;
	case K_RIGHTARROW:
	case K_KP_RIGHTARROW:
		if ( URC_NpPos( np, line, col + 1 ) == np->cursor ) {
			col = 0;
			if ( ++line >= numLines ) {
				line = numLines - 1;
				col = 999999;
			}
		} else {
			col++;
		}
		break;
	case K_HOME:
	case K_KP_HOME:
		col = 0;
		if ( ctrl ) {
			line = 0;
		}
		break;
	case K_END:
	case K_KP_END:
		col = 999999;
		if ( ctrl ) {
			line = numLines;
		}
		break;
	case K_PGUP:
	case K_KP_PGUP:
		line = line == np->top ? line - page : np->top;
		break;
	case K_PGDN:
	case K_KP_PGDN:
		line = line == np->top + page - 1 ? line + page : np->top + page - 1;
		break;
	case K_INS:
	case K_KP_INS:
		if ( !empty ) {
			if ( ctrl ) {
				URC_NpCopy( np );
			} else if ( shift ) {
				URC_NpPaste( index );
			}
		}
		return qtrue;
	case K_DEL:
	case K_KP_DEL:
		if ( empty ) {
			// selects the character after, as shift and right would
			if ( URC_NpPos( np, line, col + 1 ) == np->cursor ) {
				col = 0;
				if ( ++line >= numLines ) {
					line = numLines - 1;
					col = 999999;
				}
			} else {
				col++;
			}
			np->cursor = URC_NpPos( np, line, col );
			URC_NpShowCaret( np, win );
			return qtrue;
		}
		if ( shift ) {
			URC_NpCopy( np );
		}
		URC_NpDeleteSelection( np );
		URC_NpShowCaret( np, win );
		return qtrue;
	default:
		// the characters come as character events
		return qtrue;
	}
	np->cursor = URC_NpPos( np, line, col );
	if ( !shift ) {
		np->anchor = np->cursor;
	}
	URC_NpShowCaret( np, win );
	return qtrue;
}

static void URC_NotepadWheel( urcWindow_t *win, int delta ) {
	float	bar[4], edit[4], status[4];

	if ( win->kind == WIN_NOTEPAD ) {
		urcNotepad_t	*np = win->data;

		URC_NpRects( win, bar, edit, status );
		np->top += 2 * delta;		// 0x4b820c, 0x4b822f
		URC_NpClampTop( np, edit );
	} else if ( win->kind == WIN_FILEPICKER ) {
		win->top += delta;
	}
}

static void URC_PickerMouseDown( int index );
static void URC_PickerMouseUp( urcWindow_t *win );

static void URC_NotepadMouseDown( int index ) {
	urcWindow_t		*win = &urc_windows[index];
	urcNotepad_t	*np = win->data;
	float			bar[4], edit[4], status[4], r[4];
	urcNotepadCmd_t	cmd;
	int				menu, item;

	URC_NpRects( win, bar, edit, status );

	// an open menu takes the click: an item, or it closes
	if ( np->menu >= 0 ) {
		menu = np->menu;
		np->menu = -1;
		URC_NpMenuRect( bar, menu, r );
		if ( URC_PointIn( r, urc_cursorX, urc_cursorY ) ) {
			item = URC_NpMenuItemAt( r, menu, urc_cursorY );
			if ( item >= 0 && ( cmd = urc_npMenus[menu][item].cmd ) != NPC_NONE ) {
				URC_NpCommand( index, cmd );
			}
			return;
		}
		URC_NpTitleRect( bar, menu, r );
		if ( URC_PointIn( r, urc_cursorX, urc_cursorY ) ) {
			return;		// its title shuts it
		}
	}
	for ( menu = 0 ; menu < 2 ; menu++ ) {
		URC_NpTitleRect( bar, menu, r );
		if ( URC_PointIn( r, urc_cursorX, urc_cursorY ) ) {
			np->menu = menu;
			return;
		}
	}
	URC_NpGripRect( status, r );
	if ( URC_PointIn( r, urc_cursorX, urc_cursorY ) ) {
		np->resizing = qtrue;
		np->grab[0] = win->rect[0] + win->rect[2] - urc_cursorX;
		np->grab[1] = win->rect[1] + win->rect[3] - urc_cursorY;
		return;
	}
	if ( !URC_PointIn( edit, urc_cursorX, urc_cursorY ) ) {
		return;
	}

	// the edit: its scroll bar, or the caret (a prompt goes, 0x4a9aa0)
	np->held = URC_ScrollPart( edit, URC_NpNumLines( np ), URC_NpRows( edit ), np->top, urc_cursorX, urc_cursorY );
	switch ( np->held ) {
	case SCROLL_UP: np->top--; break;
	case SCROLL_DOWN: np->top++; break;
	case SCROLL_PAGEUP: np->top -= URC_NpRows( edit ); break;
	case SCROLL_PAGEDOWN: np->top += URC_NpRows( edit ); break;
	case SCROLL_THUMB:
		{
			float	sr[4], ty, th;

			URC_ScrollRect( edit, sr );
			URC_ScrollThumb( sr, URC_NpNumLines( np ), URC_NpRows( edit ), np->top, &ty, &th );
			np->grab[1] = urc_cursorY - ( sr[1] + ty );
		}
		break;
	default:
		if ( np->mode != NP_MESSAGE ) {
			np->mode = NP_EDIT;
		}
		np->cursor = np->anchor = URC_NpPosAt( np, edit, urc_cursorX, urc_cursorY );
		URC_NpShowCaret( np, win );
		np->selecting = qtrue;
		np->scrollTime = 0;
		break;
	}
	URC_NpClampTop( np, edit );
}

static void URC_NotepadMouseMove( urcWindow_t *win ) {
	urcNotepad_t	*np = win->data;
	float			bar[4], edit[4], status[4];

	if ( win->kind == WIN_FILEPICKER ) {
		if ( win->held == SCROLL_THUMB ) {
			urcPicker_t	*pk = win->data;
			float		c[4];

			URC_WindowClient( win, c );
			win->top = URC_ScrollTopAt( c, pk->num, URC_PickerRows( c ), urc_cursorY - win->grab );
		}
		return;
	}
	if ( win->kind != WIN_NOTEPAD ) {
		return;
	}
	if ( np->resizing ) {
		win->rect[2] = urc_cursorX + np->grab[0] - win->rect[0];
		win->rect[3] = urc_cursorY + np->grab[1] - win->rect[1];
		if ( win->rect[2] < URC_NP_MIN_W ) win->rect[2] = URC_NP_MIN_W;
		if ( win->rect[3] < URC_NP_MIN_H ) win->rect[3] = URC_NP_MIN_H;
		return;
	}
	URC_NpRects( win, bar, edit, status );
	if ( np->selecting ) {
		// the caret follows; past the text shown, the next scroll is now
		np->cursor = URC_NpPosAt( np, edit, urc_cursorX, urc_cursorY );
		if ( !np->scrollTime ) {
			np->scrollTime = cls.realtime;
		}
	} else if ( np->held == SCROLL_THUMB ) {
		np->top = URC_ScrollTopAt( edit, URC_NpNumLines( np ), URC_NpRows( edit ), urc_cursorY - np->grab[1] );
		URC_NpClampTop( np, edit );
	}
}

static void URC_NotepadMouseUp( urcWindow_t *win ) {
	if ( win->kind == WIN_FILEPICKER ) {
		URC_PickerMouseUp( win );
	} else if ( win->kind == WIN_NOTEPAD ) {
		urcNotepad_t	*np = win->data;

		if ( np->selecting ) {
			URC_NpShowCaret( np, win );		// 0x4b7f30
		}
		np->selecting = np->resizing = qfalse;
		np->scrollTime = 0;
		np->held = SCROLL_NONE;
	}
}

// the width of the text from one position to another on a line
static float URC_NpRangeWidth( const urcNotepad_t *np, urcFont_t *font, int from, int to ) {
	float	w = 0;

	for ( ; from < to ; from++ ) {
		w += URC_CharWidth( font, np->text[from] );
	}
	return w;
}

// a line of the text, its selected part white on dark blue (0x4b7530)
static void URC_NpDrawLine( const urcNotepad_t *np, urcFont_t *font, float x, float y, int start, int end, const vec4_t color ) {
	static const vec4_t	selFill = { 0, 0, 0.5f, 1 };
	static const vec4_t	white = { 1, 1, 1, 1 };
	char				buf[1024];
	int					a = np->cursor < np->anchor ? np->cursor : np->anchor;
	int					b = np->cursor < np->anchor ? np->anchor : np->cursor;
	int					from, to, n;
	float				x0;

	if ( end - start > (int)sizeof( buf ) - 1 ) {
		end = start + sizeof( buf ) - 1;
	}
	n = end - start;
	Com_Memcpy( buf, np->text + start, n );
	buf[n] = 0;
	URC_DrawString( font, x, y, buf, color );

	from = a > start ? a : start;
	to = b < end ? b : end;
	if ( from >= to ) {
		return;
	}
	x0 = x + URC_NpRangeWidth( np, font, start, from );
	URC_FillRect( x0, y, URC_NpRangeWidth( np, font, from, to ), URC_FontHeight( font ), selFill );
	buf[to - start] = 0;
	URC_DrawString( font, x0, y, buf + ( from - start ), white );
}

static void URC_DrawNotepad( const urcWindow_t *win, int index ) {
	static const vec4_t	white = { 1, 1, 1, 1 };
	static const vec4_t	black = { 0, 0, 0, 1 };
	static const vec4_t	red = { 1, 0, 0, 1 };
	static const vec4_t	promptFill = { 0.5f, 0, 0, 0.5f };		// red at half, 0x4d53dc
	static const vec4_t	statusEdge = { 0.5f, 0.5f, 0.5f, 1 };
	urcNotepad_t		*np = win->data;
	urcFont_t			*font = URC_LoadFont( URC_NP_FONT );
	urcFont_t			*small = URC_LoadFont( URC_NP_STATUS_FONT );
	float				bar[4], edit[4], status[4], r[4], saved[4], text[4], fh = URC_FontHeight( font ), y;
	int					rows, line, pos, end, i, menu;
	const char			*s;
	vec4_t				colors[4];

	URC_NpRects( win, bar, edit, status );

	// a drag past the text shown scrolls toward the cursor every quarter
	// second while the text moves (0x4b7fe0)
	if ( np->selecting && np->scrollTime && cls.realtime >= np->scrollTime ) {
		int		top = np->top;

		np->cursor = URC_NpPosAt( np, edit, urc_cursorX, urc_cursorY );
		URC_NpShowCaret( np, win );
		np->scrollTime = np->top != top ? cls.realtime + URC_NP_SCROLL_TIME : 0;
	}
	URC_NpClampTop( np, edit );
	rows = URC_NpRows( edit );

	// the menu bar (UIPulldownMenu, 0x4be190): its titles black on the
	// window's grey at 4, 2; the open one's white on black
	for ( menu = 0 ; menu < 2 ; menu++ ) {
		URC_NpTitleRect( bar, menu, r );
		if ( menu == np->menu ) {
			URC_Box( r[0], r[1], r[2], r[3], black, 1 );
		}
		URC_DrawString( font, r[0] + 4, r[1] + 2, urc_npMenuTitles[menu], menu == np->menu ? white : black );
	}

	// the edit
	Vector4Copy( edit, text );
	text[2] -= URC_SCROLL_WIDTH;
	URC_PushClip( text, saved );
	URC_Box( edit[0], edit[1], edit[2], edit[3], white, 1 );
	pos = URC_NpLinePos( np, np->top );
	for ( line = np->top, y = edit[1] ; pos >= 0 && line < np->top + rows + 1 ; line++, y += fh ) {
		end = URC_NpLineEnd( np, pos );
		URC_NpDrawLine( np, font, edit[0], y, pos, end, black );
		if ( index == urc_activeWindow && np->cursor >= pos && np->cursor <= end && cls.realtime % 750 >= 375 ) {
			URC_FillRect( edit[0] + URC_NpPosX( np, np->cursor ), y, 2, fh, black );
		}
		if ( end >= np->len ) {
			break;
		}
		pos = end + 1;
	}
	URC_PopClip( saved );
	URC_DrawScrollBar( edit, URC_NpNumLines( np ), rows, np->top, np->held, 1 );

	// the status bar
	if ( np->mode == NP_MESSAGE && cls.realtime > np->messageEnd ) {
		np->mode = NP_EDIT;
	}
	URC_Box( status[0], status[1], status[2], 1, statusEdge, 1 );
	Vector4Set( r, status[0], status[1] + 1, status[2], status[3] - 1 );
	switch ( np->mode ) {
	case NP_EDIT:
		line = URC_NpLineOf( np, np->cursor );
		s = va( "Line %d, column %d", line + 1, np->cursor - URC_NpLineStart( np, np->cursor ) + 1 );
		URC_PrintJustified( small, r, URC_JUSTIFY_LEFT, URC_JUSTIFY_MIDDLE, s, black );
		break;
	case NP_MESSAGE:
		URC_PrintJustified( small, r, URC_JUSTIFY_LEFT, URC_JUSTIFY_MIDDLE, np->message, red );
		break;
	default:
		URC_Box( r[0], r[1], r[2], r[3], promptFill, 1 );
		s = np->mode == NP_GOTO ? "Goto line (tab cancels): " :
			np->mode == NP_FIND ? "Find text (tab cancels): " :
			np->mode == NP_SAVEAS ? "Save As (tab cancels): " : va( "Save %s? (yes/no/cancel) ", np->file );
		URC_PrintJustified( small, r, URC_JUSTIFY_LEFT, URC_JUSTIFY_MIDDLE, va( "%s%s", s, np->input ), white );
		break;
	}
	// the size grip (marlett's two halves of it)
	// (UIWindowSizer, 0x4bc940: marlett's "o" white, then "p" black; placed
	// as a capture has them, 4 in from the corner)
	URC_NpGripRect( status, r );
	r[0] -= 4;
	r[1] -= 4;
	URC_PrintJustified( URC_LoadFont( URC_BOX_FONT ), r, URC_JUSTIFY_RIGHT, URC_JUSTIFY_BOTTOM, "o", white );
	URC_PrintJustified( URC_LoadFont( URC_BOX_FONT ), r, URC_JUSTIFY_RIGHT, URC_JUSTIFY_BOTTOM, "p", black );

	// an open menu, as UIPopupMenu draws its list (URC_DrawPopup)
	if ( np->menu >= 0 ) {
		urcFont_t	*pf = URC_LoadFont( URC_POPUP_FONT );
		float		ph = URC_FontHeight( pf );

		URC_NpMenuRect( bar, np->menu, r );
		URC_Box( r[0], r[1], r[2], r[3], white, 1 );
		URC_BorderColors( white, colors );
		URC_Draw3DBox( r, qfalse, colors, 1 );
		for ( i = 0, y = r[1] + URC_NP_POPUP_PAD ; i < URC_NpNumItems( np->menu ) ; i++ ) {
			if ( !urc_npMenus[np->menu][i].text[0] ) {
				// a separator (0x4bf687): a line in the text's colour, 8 in
				// from each side, 4 down its 8
				URC_Box( r[0] + 8, y + 4, r[2] - 16, 1, black, 1 );
				y += URC_NP_SEPARATOR_H;
				continue;
			}
			URC_DrawString( pf, r[0] + URC_NP_POPUP_PAD, y, urc_npMenus[np->menu][i].text, black );
			y += ph;
		}
	}
}

static void URC_Notepad_f( void ) {
	static const vec4_t	color = { 0.1f, 0.1f, 0.1f, 1 };		// 0x4a8239
	urcWindow_t			*win;
	urcNotepad_t		*np;

	win = URC_NewWindow( WIN_NOTEPAD, "", URC_NP_X, URC_NP_Y, URC_NP_W, URC_NP_H, color );
	if ( !win ) {
		return;
	}
	np = win->data = Z_Malloc( sizeof( *np ) );
	np->menu = -1;
	URC_NpSetText( np, "" );
	URC_NpSetTitle( win, np );
	if ( Cmd_Argc() > 1 ) {
		URC_NpOpen( win, Cmd_Argv( 1 ) );
	}
	URC_ActivateWindow( win - urc_windows );		// it comes up active (0x4a7f24)
}

/*
=================
The file picker

FilePickerClass (0x412b40), which the notepad's Open brings up with every
file (UIPickFileClass, ".*"): a floating window at 100, 50, 400 by 300 in
0.1 grey titled with its directory, a UIListCtrl over its client frame in
verdana-12, black on white, with the columns "File Name" 175, "Date" 125
and "Size" 100 wide: ".." when the directory is not the one it began in,
the directories as "[name]" (not those whose name begins with '.'), then
the files (0x4133c0). A file's date and size, and a directory's date, are
the disk's (0x432bc0, _findfirst on the game directory's path): month,
day, year and the hour of a 12 hour clock with "a" or "p"; a file in a
pk3 has neither. A row is opened by a second click on it within half a
second and 2 pixels (listbase_item_doubleclicked, 0x4ad4a0; a single click
does nothing here, 0x413db0): ".." lists the directory above, "[name]"
that directory, and a file goes to the notepad that asked and the picker
goes (0x413fa0, 0x415bf0). The title is the directory.

Not retail's: the disk is looked at in the home directory first, then
the install, as the file system reads.
=================
*/
static int URC_ComparePickNames( const void *a, const void *b ) {
	return Q_stricmp( *(const char **)a, *(const char **)b );
}

// a file's or directory's date, and a file's size, from the disk (0x432bc0)
static void URC_PickerStat( urcPickEntry_t *e, const char *path ) {
	static const char	*bases[2] = { "fs_homepath", "fs_basepath" };
	struct stat			st;
	struct tm			*t;
	int					i;

	e->date[0] = e->size[0] = 0;
	for ( i = 0 ; i < 2 ; i++ ) {
		const char	*os = FS_BuildOSPath( Cvar_VariableString( bases[i] ), FS_GetCurrentGameDir(), path );

		if ( stat( os, &st ) == 0 && ( t = localtime( &st.st_mtime ) ) != NULL ) {
			Com_sprintf( e->date, sizeof( e->date ), "%2d/%02d/%04d %2d:%02d%c", t->tm_mon + 1, t->tm_mday,
				t->tm_year + 1900, t->tm_hour % 12, t->tm_min, t->tm_hour < 12 ? 'a' : 'p' );
			if ( !e->dir ) {
				Com_sprintf( e->size, sizeof( e->size ), "%d", (int)st.st_size );
			}
			return;
		}
	}
}

static void URC_PickerList( urcWindow_t *win ) {
	urcPicker_t	*pk = win->data;
	char		**dirs, **files, path[MAX_QPATH];
	int			numDirs, numFiles, i, n = 0;

	if ( pk->entries ) {
		Z_Free( pk->entries );
	}
	dirs = FS_ListFiles( pk->dir, "/", &numDirs );
	files = FS_ListFiles( pk->dir, "", &numFiles );
	qsort( dirs, numDirs, sizeof( char * ), URC_ComparePickNames );
	qsort( files, numFiles, sizeof( char * ), URC_ComparePickNames );
	pk->entries = Z_Malloc( ( numDirs + numFiles + 1 ) * sizeof( urcPickEntry_t ) );
	if ( pk->dir[0] ) {
		Q_strncpyz( pk->entries[n].name, "..", sizeof( pk->entries[n].name ) );
		pk->entries[n++].dir = qtrue;
	}
	// the file system lists the pk3s' directories at every depth, ending
	// in '/': the directory's own, once each
	for ( i = 0 ; i < numDirs ; i++ ) {
		char	name[MAX_QPATH];
		int		len;

		Q_strncpyz( name, dirs[i], sizeof( name ) );
		len = strlen( name );
		if ( len && name[len - 1] == '/' ) {
			name[--len] = 0;
		}
		if ( !len || strchr( name, '/' ) || name[0] == '.' ||
			( n && pk->entries[n - 1].dir && !Q_stricmp( pk->entries[n - 1].name, name ) ) ) {
			continue;
		}
		Q_strncpyz( pk->entries[n].name, name, sizeof( pk->entries[n].name ) );
		pk->entries[n].dir = qtrue;
		Com_sprintf( path, sizeof( path ), "%s%s", pk->dir, name );
		URC_PickerStat( &pk->entries[n++], path );
	}
	for ( i = 0 ; i < numFiles ; i++ ) {
		if ( !files[i][0] || strchr( files[i], '/' ) ) {
			continue;		// a pk3's entry for the directory itself has no name
		}
		Q_strncpyz( pk->entries[n].name, files[i], sizeof( pk->entries[n].name ) );
		pk->entries[n].dir = qfalse;
		Com_sprintf( path, sizeof( path ), "%s%s", pk->dir, files[i] );
		URC_PickerStat( &pk->entries[n++], path );
	}
	pk->num = n;
	FS_FreeFileList( dirs );
	FS_FreeFileList( files );
	win->top = win->selected = 0;
	Q_strncpyz( win->title, pk->dir, sizeof( win->title ) );
}

static void URC_OpenPicker( const char *dir, void *owner ) {
	static const vec4_t	color = { 0.1f, 0.1f, 0.1f, 1 };
	urcWindow_t			*win;
	urcPicker_t			*pk;

	win = URC_NewWindow( WIN_FILEPICKER, "File Picker", URC_PICKER_X, URC_PICKER_Y, URC_PICKER_W, URC_PICKER_H, color );
	if ( !win ) {
		return;
	}
	pk = win->data = Z_Malloc( sizeof( *pk ) );
	Q_strncpyz( pk->dir, dir, sizeof( pk->dir ) );
	pk->owner = owner;
	pk->clickRow = -1;
	URC_PickerList( win );
	URC_ActivateWindow( win - urc_windows );
}

static void URC_PickerClamp( urcWindow_t *win ) {
	urcPicker_t	*pk = win->data;
	float		c[4];
	int			rows;

	URC_WindowClient( win, c );
	rows = URC_PickerRows( c );
	if ( win->top > pk->num - rows ) {
		win->top = pk->num - rows;
	}
	if ( win->top < 0 ) {
		win->top = 0;
	}
}

// a row opened: a directory is listed, a file goes to its notepad
static void URC_PickerOpen( int index, int row ) {
	urcWindow_t		*win = &urc_windows[index];
	urcPicker_t		*pk = win->data;
	urcPickEntry_t	*e = &pk->entries[row];
	char			path[MAX_QPATH], *slash;
	void			*owner = pk->owner;
	int				i;

	if ( e->dir ) {
		if ( !strcmp( e->name, ".." ) ) {
			pk->dir[strlen( pk->dir ) - 1] = 0;
			slash = strrchr( pk->dir, '/' );
			*( slash ? slash + 1 : pk->dir ) = 0;
		} else {
			Q_strcat( pk->dir, sizeof( pk->dir ), va( "%s/", e->name ) );
		}
		URC_PickerList( win );
		return;
	}
	Com_sprintf( path, sizeof( path ), "%s%s", pk->dir, e->name );
	for ( i = 0 ; i < urc_numWindows ; i++ ) {
		if ( urc_windows[i].kind == WIN_NOTEPAD && urc_windows[i].data == owner ) {
			URC_NpOpen( &urc_windows[i], path );		// the "openfile" event (0x4a9d60)
			break;
		}
	}
	URC_CloseWindow( index );
	for ( i = 0 ; i < urc_numWindows ; i++ ) {
		if ( urc_windows[i].data == owner ) {
			URC_ActivateWindow( i );
			break;
		}
	}
}

static void URC_PickerMouseDown( int index ) {
	urcWindow_t	*win = &urc_windows[index];
	urcPicker_t	*pk = win->data;
	float		c[4];
	int			rows, row;

	URC_WindowClient( win, c );
	rows = URC_PickerRows( c );
	win->held = URC_ScrollPart( c, pk->num, rows, win->top, urc_cursorX, urc_cursorY );
	switch ( win->held ) {
	case SCROLL_UP: win->top--; break;
	case SCROLL_DOWN: win->top++; break;
	case SCROLL_PAGEUP: win->top -= rows; break;
	case SCROLL_PAGEDOWN: win->top += rows; break;
	case SCROLL_THUMB:
		{
			float	sr[4], ty, th;

			URC_ScrollRect( c, sr );
			URC_ScrollThumb( sr, pk->num, rows, win->top, &ty, &th );
			win->grab = urc_cursorY - ( sr[1] + ty );
		}
		break;
	default:
		if ( urc_cursorY >= c[1] + URC_SAVE_HEADER ) {
			row = win->top + (int)( ( urc_cursorY - c[1] - URC_SAVE_HEADER ) / URC_FontHeight( URC_LoadFont( URC_DEFAULT_FONT ) ) );
			if ( row >= 0 && row < pk->num ) {
				if ( row == pk->clickRow && cls.realtime - pk->clickTime < 500 &&
					fabs( urc_cursorX - pk->clickX ) <= 2 && fabs( urc_cursorY - pk->clickY ) <= 2 ) {
					pk->clickRow = -1;
					URC_PickerOpen( index, row );
					return;
				}
				win->selected = row + 1;
				pk->clickRow = row;
				pk->clickTime = cls.realtime;
				pk->clickX = urc_cursorX;
				pk->clickY = urc_cursorY;
			}
		}
		break;
	}
	URC_PickerClamp( win );
}

static void URC_PickerMouseUp( urcWindow_t *win ) {
	win->held = SCROLL_NONE;
}

static void URC_DrawPicker( const urcWindow_t *win, int index ) {
	static const char	*titles[3] = { "File Name", "Date", "Size" };
	static const int	widths[3] = { 175, 125, 100 };		// 0x412c64
	static const vec4_t	headerFill = { 0.3f, 0.3f, 0.3f, 1 };
	static const vec4_t	white = { 1, 1, 1, 1 };
	static const vec4_t	black = { 0, 0, 0, 1 };
	urcPicker_t			*pk = win->data;
	urcFont_t			*font = URC_LoadFont( URC_DEFAULT_FONT );
	float				c[4], r[4], saved[4], x, y, rowH = URC_FontHeight( font );
	int					col, i;

	URC_WindowClient( win, c );
	URC_PickerClamp( (urcWindow_t *)win );
	URC_PushClip( c, saved );
	URC_Box( c[0], c[1], c[2], c[3], white, 1 );
	x = 0;
	for ( col = 0 ; col < 3 ; col++ ) {
		Vector4Set( r, c[0] + x, c[1], widths[col] + 1, URC_SAVE_HEADER );
		URC_DrawBorder( r, headerFill, white, 1, 3, 1 );
		URC_DrawString( font, r[0] + 2, r[1] + 2, titles[col], white );
		x += widths[col];
	}
	if ( x < c[2] ) {
		Vector4Set( r, c[0] + x, c[1], c[2] - x + 1, URC_SAVE_HEADER );
		URC_DrawBorder( r, headerFill, white, 1, 3, 1 );
	}
	for ( i = win->top, y = URC_SAVE_HEADER ; i < pk->num && y <= c[3] ; i++, y += rowH ) {
		const urcPickEntry_t	*e = &pk->entries[i];

		// the list's selected row is drawn white on black's inverse, as the
		// load list's (UIListCtrl), which on this black on white list is no change
		URC_Box( c[0], c[1] + y, c[2], rowH, white, 1 );
		URC_DrawString( font, c[0], c[1] + y, e->dir && strcmp( e->name, ".." ) ? va( "[%s]", e->name ) : e->name, black );
		URC_DrawString( font, c[0] + widths[0], c[1] + y, e->date, black );
		URC_DrawString( font, c[0] + widths[0] + widths[1], c[1] + y, e->size, black );
	}
	URC_DrawScrollBar( c, pk->num, URC_PickerRows( c ), win->top, urc_windowPress == index ? win->held : SCROLL_NONE, 1 );
	URC_PopClip( saved );
}

static void URC_FreeWindowData( urcWindow_t *win ) {
	if ( !win->data ) {
		return;
	}
	if ( win->kind == WIN_NOTEPAD ) {
		urcNotepad_t	*np = win->data;

		if ( np->text ) {
			Z_Free( np->text );
		}
	} else if ( win->kind == WIN_FILEPICKER ) {
		urcPicker_t		*pk = win->data;

		if ( pk->entries ) {
			Z_Free( pk->entries );
		}
	}
	Z_Free( win->data );
	win->data = NULL;
}

// the keys and characters for the active notepad
static qboolean URC_NotepadTakesKey( int key ) {
	if ( urc_activeWindow < 0 || urc_activeWindow >= urc_numWindows ||
		urc_windows[urc_activeWindow].kind != WIN_NOTEPAD || urc_windows[urc_activeWindow].minimized ) {
		return qfalse;
	}
	return URC_NotepadKey( urc_activeWindow, key );
}

void URC_CharEvent( int c ) {
	if ( !urc_initialised || urc_activeWindow < 0 || urc_activeWindow >= urc_numWindows ||
		urc_windows[urc_activeWindow].kind != WIN_NOTEPAD || urc_windows[urc_activeWindow].minimized ) {
		return;
	}
	URC_NotepadChar( urc_activeWindow, c );
}

static void URC_DrawWindow( int index ) {
	static const vec4_t	white = { 1, 1, 1, 1 };
	static const vec4_t	black = { 0, 0, 0, 1 };
	const urcWindow_t	*win = &urc_windows[index];
	urcFont_t			*boxFont = URC_LoadFont( URC_BOX_FONT );
	vec4_t				colors[4], fill, edge, text;
	float				x = win->rect[0], y = win->rect[1], w = win->rect[2], h = URC_WindowHeight( win );
	float				th = h < URC_TITLE_HEIGHT ? h : URC_TITLE_HEIGHT;
	float				r[4], saved[4];

	Vector4Set( r, x, y, w, h );
	URC_PushClip( r, saved );
	URC_Box( x, y, w, h, urc_windowBack, 1 );

	if ( index == urc_activeWindow ) {
		URC_BorderColors( win->color, colors );
		Vector4Copy( win->color, fill );
		Vector4Copy( colors[2], edge );
		Vector4Copy( white, text );
	} else {
		URC_BorderColors( urc_windowBack, colors );
		Vector4Copy( urc_windowBack, fill );
		Vector4Copy( colors[0], edge );
		Vector4Copy( black, text );
	}
	URC_Box( x, y, w, th, fill, 1 );
	URC_Box( x, y, w, 1, edge, 1 );
	URC_Box( x, y, 1, th, edge, 1 );
	URC_Box( x + w - 2, y, 2, th, edge, 1 );
	URC_Box( x, y + th - 2, w, 2, edge, 1 );
	URC_DrawString( URC_LoadFont( URC_TITLE_FONT ), x + 3, y + 3, win->title, text );
	URC_WindowBox( win, WINPART_CLOSE, r );
	URC_PrintJustified( boxFont, r, URC_JUSTIFY_CENTER, URC_JUSTIFY_MIDDLE, "r", text );
	if ( win->kind != WIN_DIALOG ) {
		URC_WindowBox( win, WINPART_MINIMIZE, r );
		URC_PrintJustified( boxFont, r, URC_JUSTIFY_CENTER, URC_JUSTIFY_MIDDLE, "0", text );
	}

	if ( !win->minimized ) {
		switch ( win->kind ) {
		case WIN_DIALOG:
			URC_DrawDialog( win, index );
			break;
		case WIN_TESTLIST:
			URC_DrawTestList( win, index );
			break;
		case WIN_NOTEPAD:
			URC_DrawNotepad( win, index );
			break;
		case WIN_FILEPICKER:
			URC_DrawPicker( win, index );
			break;
		}
	}
	URC_PopClip( saved );
}

static void URC_DrawWindows( void ) {
	int		i;

	for ( i = 0 ; i < urc_numWindows ; i++ ) {
		URC_DrawWindow( i );
	}
}

/*
=================
URC_WindowKey

The mouse buttons over the windows, with the cursor up; returns whether
the windows took the key.
=================
*/
static qboolean URC_WindowKey( int key, qboolean down ) {
	urcWindowPart_t	part;
	int				i;

	// the wheel scrolls a list under the cursor
	if ( ( key == K_MWHEELUP || key == K_MWHEELDOWN ) && down ) {
		i = URC_WindowAt( urc_cursorX, urc_cursorY );
		if ( i < 0 ) {
			return qfalse;
		}
		if ( urc_windows[i].kind == WIN_TESTLIST && !urc_windows[i].minimized ) {
			urc_windows[i].top += key == K_MWHEELUP ? -1 : 1;
			URC_TestListClamp( &urc_windows[i] );
		} else if ( !urc_windows[i].minimized ) {
			URC_NotepadWheel( &urc_windows[i], key == K_MWHEELUP ? -1 : 1 );
		}
		return qtrue;
	}
	if ( key != K_MOUSE1 ) {
		return qfalse;
	}
	if ( !down ) {
		i = urc_windowPress;
		urc_windowDrag = urc_windowPress = -1;
		if ( i >= 0 && i < urc_numWindows && urc_windowPressPart == WINPART_CLIENT &&
			( urc_windows[i].kind == WIN_NOTEPAD || urc_windows[i].kind == WIN_FILEPICKER ) ) {
			URC_NotepadMouseUp( &urc_windows[i] );
			return qtrue;
		}
		if ( i >= 0 && i < urc_numWindows && urc_windows[i].held != SCROLL_NONE ) {
			urc_windows[i].held = SCROLL_NONE;
			return qtrue;
		}
		if ( i < 0 || i >= urc_numWindows || URC_WindowPartAt( &urc_windows[i], urc_cursorX, urc_cursorY ) != urc_windowPressPart ) {
			return qfalse;
		}
		switch ( urc_windowPressPart ) {
		case WINPART_OK:
			// UIDialog's Ok (0x4ac120 sets the command): the command, then
			// the dialog goes, as with Cancel and the close box
			Cbuf_AddText( va( "%s\n", urc_windows[i].command ) );
			URC_CloseWindow( i );
			break;
		case WINPART_CANCEL:
			URC_CloseWindow( i );
			break;
		case WINPART_CLOSE:
			// a notepad's asks to save first (floatingwindow_close_pressed)
			if ( urc_windows[i].kind == WIN_NOTEPAD ) {
				URC_NpCommand( i, NPC_CLOSE );
			} else {
				URC_CloseWindow( i );
			}
			break;
		case WINPART_MINIMIZE:
			urc_windows[i].minimized = !urc_windows[i].minimized;
			break;
		default:
			break;
		}
		return qtrue;
	}

	i = URC_WindowAt( urc_cursorX, urc_cursorY );
	if ( i < 0 ) {
		urc_activeWindow = -1;		// a click elsewhere: the keys go back to the menus
		return qfalse;
	}
	i = URC_ActivateWindow( i );
	part = URC_WindowPartAt( &urc_windows[i], urc_cursorX, urc_cursorY );
	switch ( part ) {
	case WINPART_TITLE:
		urc_windowDrag = i;
		urc_windowGrab[0] = urc_cursorX - urc_windows[i].rect[0];
		urc_windowGrab[1] = urc_cursorY - urc_windows[i].rect[1];
		break;
	case WINPART_CLOSE:
	case WINPART_MINIMIZE:
	case WINPART_OK:
	case WINPART_CANCEL:
		urc_windowPress = i;
		urc_windowPressPart = part;
		break;
	case WINPART_CLIENT:
		if ( urc_windows[i].kind == WIN_TESTLIST ) {
			URC_TestListClick( &urc_windows[i] );
			if ( urc_windows[i].held != SCROLL_NONE ) {
				urc_windowPress = i;
				urc_windowPressPart = part;
			}
		} else if ( urc_windows[i].kind == WIN_NOTEPAD ) {
			urc_windowPress = i;
			urc_windowPressPart = part;
			URC_NotepadMouseDown( i );
		} else if ( urc_windows[i].kind == WIN_FILEPICKER ) {
			urc_windowPress = i;
			urc_windowPressPart = part;
			URC_PickerMouseDown( i );
		}
		break;
	default:
		break;
	}
	return qtrue;
}

// a window dragged by its title bar follows the cursor, a list's thumb
// the cursor's height
static void URC_WindowMouseMove( void ) {
	if ( urc_windowDrag >= 0 && urc_windowDrag < urc_numWindows ) {
		urc_windows[urc_windowDrag].rect[0] = urc_cursorX - urc_windowGrab[0];
		urc_windows[urc_windowDrag].rect[1] = urc_cursorY - urc_windowGrab[1];
	}
	if ( urc_windowPress >= 0 && urc_windowPress < urc_numWindows && urc_windows[urc_windowPress].kind == WIN_TESTLIST &&
		urc_windows[urc_windowPress].held == SCROLL_THUMB ) {
		urcWindow_t	*win = &urc_windows[urc_windowPress];
		float		c[4];

		URC_WindowClient( win, c );
		win->top = URC_ScrollTopAt( c, URC_TESTLIST_ROWS, URC_TestListPage( c ), urc_cursorY - win->grab );
	}
	if ( urc_windowPress >= 0 && urc_windowPress < urc_numWindows && urc_windowPressPart == WINPART_CLIENT ) {
		URC_NotepadMouseMove( &urc_windows[urc_windowPress] );
	}
}

/*
=================
URC_Dialog_f

dialog <title> <cvar> <command> [width] [height] [label material] [ok
material] [cancel material] (fakk2.exe 0x423250, the window 0x40cf10): a
dialog of 300x200 by default, centred on the screen, its label showing
the cvar, Ok running the command; the materials take the place of the
label's and the buttons' text. It comes up active.
=================
*/
static void URC_Dialog_f( void ) {
	static const vec4_t	color = { 0.1f, 0.1f, 0.1f, 1 };
	urcWindow_t			*win;
	int					w, h, i;

	if ( Cmd_Argc() < 4 ) {
		Com_Printf( "usage: dialog title cvar_name command [width] [height]\n" );
		return;
	}
	w = Cmd_Argc() > 4 ? atoi( Cmd_Argv( 4 ) ) : 300;
	h = Cmd_Argc() > 5 ? atoi( Cmd_Argv( 5 ) ) : 200;
	win = URC_NewWindow( WIN_DIALOG, Cmd_Argv( 1 ), ( 640 - w ) >> 1, ( 480 - h ) >> 1, w, h, color );
	if ( !win ) {
		return;
	}
	Q_strncpyz( win->cvar, Cmd_Argv( 2 ), sizeof( win->cvar ) );
	Q_strncpyz( win->command, Cmd_Argv( 3 ), sizeof( win->command ) );
	for ( i = 0 ; i < 3 ; i++ ) {
		if ( Cmd_Argc() > 6 + i && Cmd_Argv( 6 + i )[0] ) {
			win->material[i] = re.RegisterShaderNoMip( Cmd_Argv( 6 + i ) );
		}
	}
	URC_ActivateWindow( win - urc_windows );
}

/*
=================
URC_TestList_f

ui_testlist (fakk2.exe 0x40d950): "Test list control", 400x300 at 20, 20,
coloured 0.1 grey, holding the test list; it is not made active.
=================
*/
static void URC_TestList_f( void ) {
	static const vec4_t	color = { 0.1f, 0.1f, 0.1f, 1 };

	URC_NewWindow( WIN_TESTLIST, "Test list control", 20, 20, 400, 300, color );
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
	Vector4Set( urc_clip, SCR_ScreenLeft640(), 0, SCR_ScreenRight640(), 480 );

	// leaving the menu for a map drops the menus, as the retail client's
	// map load does; disconnecting brings the main menu back
	if ( clc.state != urc_lastState ) {
		if ( urc_lastState == CA_DISCONNECTED && clc.state != CA_DISCONNECTED ) {
			URC_ClearMenus();
		}
		urc_lastState = clc.state;
	}
	URC_DrawIntro();
	if ( clc.state == CA_DISCONNECTED && !urc_stackDepth && !urc_introStep && !CL_Viewer_Active() &&
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
		URC_UpdateOptionCvars();
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

	// a saved game's picture shows the game and its HUD, not the menus
	// (saveshot; the retail UI's draw, 0x4193f0, checks the same flag)
	if ( urc_saveShot ) {
		for ( i = 0 ; i < urc_numMenus ; i++ ) {
			urcMenu_t	*menu = &urc_menus[i];
			int			k;

			for ( k = 0 ; k < urc_stackDepth && urc_stack[k] != i ; k++ ) {
			}
			if ( menu->visible && k == urc_stackDepth ) {
				URC_DrawMenu( menu, qfalse, qtrue );
			}
		}
		return;
	}

	// the print, centre or located ("GAME SAVED" is one), is drawn by the
	// retail desktop's own Draw (0x419ba0, then 0x4193f0, the print at
	// 0x419480 or 0x419770), and UIWidget::Display (0x49ef30) draws a widget
	// before its children, every menu and the HUD among them: so the prints are
	// under the menus, and the main menu, which covers the screen, hides them
	if ( clc.state == CA_ACTIVE ) {
		URC_DrawPrint( &urc_print );
	}

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
			URC_DrawMenu( menu, qfalse, qtrue );
		}
	}
	for ( i = 0 ; i < urc_stackDepth ; i++ ) {
		URC_DrawMenu( &urc_menus[urc_stack[i]], i == urc_stackDepth - 1, qfalse );
	}
	URC_DrawWindows();
	URC_DrawPopup();
	URC_DrawPressKey();
	INV_Draw();

	// ui_drawcoords (fakk2.exe 0x4982a3): with the cursor up, its place as
	// "x:y" ten units right of it and ten above, in the interface's font
	if ( ui_drawcoords->integer && ( urc_stackDepth || cl_mouseShown ) ) {
		static const vec4_t	white = { 1, 1, 1, 1 };

		URC_DrawString( URC_LoadFont( URC_DEFAULT_FONT ), urc_cursorX + 10, urc_cursorY - 10,
			va( "%d:%d", (int)urc_cursorX, (int)urc_cursorY ), white );
	}
	URC_DrawFps();
}

/*
=================
URC_SetLinkCvar

What a check box or slider writes to its cvar, set as the console's "set"
sets it: a latched cvar waits for the restart Apply's ui_checkrestart
finds it needs. Set outright (Cvar_Set), it changed at once and
ui_checkrestart found nothing to restart for it.
=================
*/
static void URC_SetLinkCvar( const urcWidget_t *w, const char *value ) {
	Cvar_SetLatched( w->linkcvar, value );
}

/*
=================
URC_SliderSet, URC_SliderStep

A slider's cvar from where the cursor is on the track, or a step from an
arrow; written as UISlider's UpdateData (0x4b61c0) writes it: "%d" for an
integral slider, "%.3f" otherwise.
=================
*/
static void URC_SliderWrite( urcWidget_t *w, float value ) {
	if ( value < w->rangeMin ) value = w->rangeMin;
	if ( value > w->rangeMax ) value = w->rangeMax;
	if ( w->sliderInt ) {
		URC_SetLinkCvar( w, va( "%d", (int)value ) );
	} else {
		URC_SetLinkCvar( w, va( "%.3f", value ) );
	}
}

static void URC_SliderSet( urcWidget_t *w, const float rect[4] ) {
	float	thumbW = URC_SliderThumbWidth(), span, frac, value;

	if ( !w->linkcvar[0] || w->rangeMax <= w->rangeMin ) {
		return;
	}
	span = rect[2] - 2 * URC_SLIDER_ARROW - thumbW;
	frac = span > 0 ? ( urc_cursorX - rect[0] - URC_SLIDER_ARROW - thumbW * 0.5f ) / span : 0;
	if ( frac < 0 ) frac = 0;
	if ( frac > 1 ) frac = 1;
	value = w->rangeMin + frac * ( w->rangeMax - w->rangeMin );
	if ( w->stepSize > 0 ) {
		value = w->rangeMin + floor( ( value - w->rangeMin ) / w->stepSize + 0.5f ) * w->stepSize;
	}
	URC_SliderWrite( w, value );
}

static void URC_SliderStep( urcWidget_t *w, int dir ) {
	float	step = w->stepSize > 0 ? w->stepSize : ( w->rangeMax - w->rangeMin ) * 0.1f;

	if ( !w->linkcvar[0] || w->rangeMax <= w->rangeMin ) {
		return;
	}
	URC_SliderWrite( w, atof( URC_LinkValue( w ) ) + dir * step );
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

/*
=================
URC_PopupRect, URC_DrawPopup

UIPopupMenu (Create, 0x4bf1f0; Draw, 0x4bf4a0), opened below its pulldown:
as wide as its widest item plus 24, an item per verdana-14 line plus 4
above and below, kept on the screen; a raised border on its background.
The pulldown's menu gives it black on white, and white under black for the
item under the cursor (0x4bda90), so that one looks the same.
=================
*/
#define URC_POPUP_PAD		4

/*
=================
URC_BuildModeList

What a "popupsource videomodes" pulldown lists (OpenFAKK2's: retail's
video menu lists r_mode 3 to 9, 4:3 and 5:4 only): the desktop's size,
then every size the display reports (r_availableModes) and the common
wide ones, at least 640x480, smallest first, each with its shape. As many
as fit on the screen in verdana-14 are kept, the largest.
=================
*/
#define URC_MAX_MODES		33		// ( 480 - 2 * URC_POPUP_PAD ) / 14

typedef struct {
	char	text[32];
	char	command[96];
} urcMode_t;

static urcMode_t	urc_modes[URC_MAX_MODES];
static int			urc_numModes;

static int URC_CompareSizes( const void *a, const void *b ) {
	const int	*x = a, *y = b;

	return x[0] != y[0] ? x[0] - y[0] : x[1] - y[1];
}

static const char *URC_ShapeName( int w, int h ) {
	static const struct {
		float		ratio;
		const char	*name;
	} shapes[] = {
		{ 4.0f / 3, "4:3" }, { 5.0f / 4, "5:4" }, { 16.0f / 9, "16:9" }, { 16.0f / 10, "16:10" }, { 21.0f / 9, "21:9" }, { 64.0f / 27, "21:9" }
	};
	float	r = (float)w / h;
	int		i;

	for ( i = 0 ; i < ARRAY_LEN( shapes ) ; i++ ) {
		if ( fabs( r - shapes[i].ratio ) < 0.02f ) {
			return shapes[i].name;
		}
	}
	return NULL;
}

static void URC_BuildModeList( void ) {
	// the wide sizes OpenFAKK2's options offered, and retail's list's
	static const int	common[][2] = {
		{ 1280, 720 }, { 1366, 768 }, { 1600, 900 }, { 1920, 1080 }, { 2560, 1440 }, { 3840, 2160 },
		{ 1680, 1050 }, { 1920, 1200 }, { 2560, 1600 }, { 2560, 1080 }, { 3440, 1440 },
		{ 640, 480 }, { 800, 600 }, { 1024, 768 }, { 1152, 864 }, { 1280, 1024 }, { 1600, 1200 }
	};
	int			sizes[128][2], numSizes = 0, first, i, j;
	char		list[MAX_CVAR_VALUE_STRING], *p, *tok;

	Q_strncpyz( list, Cvar_VariableString( "r_availableModes" ), sizeof( list ) );
	for ( p = list ; ( tok = COM_ParseExt( &p, qfalse ) )[0] && numSizes < ARRAY_LEN( sizes ) ; ) {
		if ( sscanf( tok, "%dx%d", &sizes[numSizes][0], &sizes[numSizes][1] ) == 2 ) {
			numSizes++;
		}
	}
	for ( i = 0 ; i < ARRAY_LEN( common ) && numSizes < ARRAY_LEN( sizes ) ; i++ ) {
		sizes[numSizes][0] = common[i][0];
		sizes[numSizes][1] = common[i][1];
		numSizes++;
	}
	qsort( sizes, numSizes, sizeof( sizes[0] ), URC_CompareSizes );
	for ( i = j = 0 ; i < numSizes ; i++ ) {
		if ( sizes[i][0] < 640 || sizes[i][1] < 480 || ( j && sizes[i][0] == sizes[j - 1][0] && sizes[i][1] == sizes[j - 1][1] ) ) {
			continue;
		}
		sizes[j][0] = sizes[i][0];
		sizes[j][1] = sizes[i][1];
		j++;
	}
	numSizes = j;

	Q_strncpyz( urc_modes[0].text, "Desktop", sizeof( urc_modes[0].text ) );
	Q_strncpyz( urc_modes[0].command, "set r_mode -2", sizeof( urc_modes[0].command ) );
	urc_numModes = 1;
	first = numSizes > URC_MAX_MODES - 1 ? numSizes - ( URC_MAX_MODES - 1 ) : 0;
	for ( i = first ; i < numSizes ; i++ ) {
		urcMode_t	*m = &urc_modes[urc_numModes++];
		const char	*shape = URC_ShapeName( sizes[i][0], sizes[i][1] );

		Com_sprintf( m->text, sizeof( m->text ), shape ? "%d x %d (%s)" : "%d x %d", sizes[i][0], sizes[i][1], shape );
		Com_sprintf( m->command, sizeof( m->command ), "set r_mode -1; set r_customwidth %d; set r_customheight %d",
			sizes[i][0], sizes[i][1] );
	}
}

static int URC_PopupCount( const urcWidget_t *w ) {
	return w->videoModes ? urc_numModes : w->numPopups;
}

static const char *URC_PopupText( const urcWidget_t *w, int i ) {
	return w->videoModes ? urc_modes[i].text : w->popups[i].text;
}

static const char *URC_PopupCommand( const urcWidget_t *w, int i ) {
	return w->videoModes ? urc_modes[i].command : w->popups[i].command;
}

static void URC_PopupRect( int index, float rect[4] ) {
	const urcWidget_t	*w = &urc_widgets[index];
	urcFont_t			*font = URC_LoadFont( URC_POPUP_FONT );
	float				head[4], width = 0;
	int					i;

	URC_TopWidgetRect( index, head );
	for ( i = 0 ; i < URC_PopupCount( w ) ; i++ ) {
		float	tw = URC_TextBlockWidth( font, URC_PopupText( w, i ) );

		if ( tw > width ) {
			width = tw;
		}
	}
	rect[2] = width + 24;
	rect[3] = URC_PopupCount( w ) * URC_FontHeight( font ) + 2 * URC_POPUP_PAD;
	rect[0] = head[0];
	rect[1] = head[1] + head[3];
	if ( rect[0] + rect[2] > SCR_ScreenRight640() ) {
		rect[0] = SCR_ScreenRight640() - rect[2];
	}
	if ( rect[1] + rect[3] > 480 ) {
		rect[1] = head[1] - rect[3];
		if ( rect[1] < 0 ) {
			rect[1] = 0;
		}
	}
}

static int URC_PopupItemAt( const float rect[4] ) {
	urcFont_t	*font = URC_LoadFont( URC_POPUP_FONT );
	int			i;

	if ( !URC_CursorIn( rect ) ) {
		return -1;
	}
	i = (int)( ( urc_cursorY - rect[1] - URC_POPUP_PAD ) / URC_FontHeight( font ) );
	return ( i >= 0 && i < URC_PopupCount( &urc_widgets[urc_popupWidget] ) ) ? i : -1;
}

static void URC_DrawPopup( void ) {
	static const vec4_t	white = { 1, 1, 1, 1 };
	static const vec4_t	black = { 0, 0, 0, 1 };
	urcWidget_t			*w;
	urcFont_t			*font;
	float				rect[4], saved[4], y, h;
	vec4_t				colors[4];
	int					i, hover;

	if ( urc_popupWidget < 0 || !urc_stackDepth ) {
		return;
	}
	w = &urc_widgets[urc_popupWidget];
	font = URC_LoadFont( URC_POPUP_FONT );
	URC_PopupRect( urc_popupWidget, rect );
	hover = URC_PopupItemAt( rect );
	URC_PushClip( rect, saved );
	URC_Box( rect[0], rect[1], rect[2], rect[3], white, 1 );
	URC_BorderColors( white, colors );
	URC_Draw3DBox( rect, qfalse, colors, 1 );
	h = URC_FontHeight( font );
	y = rect[1] + URC_POPUP_PAD;
	for ( i = 0 ; i < URC_PopupCount( w ) ; i++, y += h ) {
		if ( i == hover ) {
			URC_Box( rect[0] + 2, y, rect[2] - 4, h, white, 1 );
		}
		URC_DrawString( font, rect[0] + URC_POPUP_PAD, y, URC_PopupText( w, i ), black );
	}
	URC_PopClip( saved );
}

/*
=================
URC_ListScroll

The wheel over a bind or save list, and dragging its thumb.
=================
*/
static void URC_ListScroll( int index, int delta ) {
	urcWidget_t	*w = &urc_widgets[index];
	float		rect[4];

	URC_TopWidgetRect( index, rect );
	if ( w->cls == WC_BINDLIST ) {
		urc_bindList.scroll += delta;
		URC_BindClampScroll( rect );
	} else if ( w->cls == WC_LOADGAME ) {
		urc_saveTop += delta;
		URC_SaveClampTop( w );
	} else if ( w->cls == WC_MULTILINE ) {
		w->lineTop += delta;
		URC_MultiLineClamp( w );
	}
}

static void URC_ListDragThumb( void ) {
	urcWidget_t	*w;
	float		rect[4];

	if ( urc_scrollWidget < 0 || urc_scrollHeld != SCROLL_THUMB || !URC_TopWidgetRect( urc_scrollWidget, rect ) ) {
		return;
	}
	w = &urc_widgets[urc_scrollWidget];
	if ( w->cls == WC_BINDLIST ) {
		urc_bindList.scroll = URC_ScrollTopAt( rect, urc_bindList.numItems, URC_BindVisibleRows( rect ), urc_cursorY - urc_scrollGrab );
	} else if ( w->cls == WC_LOADGAME ) {
		urc_saveTop = URC_ScrollTopAt( rect, urc_numSaves, URC_SaveVisibleRows( w ), urc_cursorY - urc_scrollGrab );
	} else if ( w->cls == WC_MULTILINE ) {
		w->lineTop = URC_ScrollTopAt( rect, URC_MultiLineCount( w ), URC_MultiLinePage( w ), urc_cursorY - urc_scrollGrab );
	}
}

// the save list of the menu on top, if it has one
static urcWidget_t *URC_TopSaveList( void ) {
	urcMenu_t	*menu;
	int			i;

	if ( !urc_stackDepth ) {
		return NULL;
	}
	menu = &urc_menus[urc_stack[urc_stackDepth - 1]];
	for ( i = 0 ; i < menu->numWidgets ; i++ ) {
		if ( urc_widgets[menu->firstWidget + i].cls == WC_LOADGAME ) {
			return &urc_widgets[menu->firstWidget + i];
		}
	}
	return NULL;
}

/*
=================
URC_KeyEvent

Only while a menu is up: escape pops it, the left button works the
button, check box, slider, pulldown or list under the cursor, the wheel
scrolls the list under it, the arrows move the saved game selection.
=================
*/
void URC_KeyEvent( int key, qboolean down ) {
	float	rect[4];
	int		i;

	// the floating windows over everything, with the cursor up
	if ( urc_numWindows && ( urc_stackDepth || cl_mouseShown ) && URC_WindowKey( key, down ) ) {
		return;
	}
	// the keys of the active notepad's text and prompts
	if ( down && URC_NotepadTakesKey( key ) ) {
		return;
	}
	if ( !urc_stackDepth ) {
		return;
	}
	if ( !down ) {
		if ( key == K_MOUSE1 ) {
			urc_dragSlider = -1;
			urc_sliderPress = -1;
			urc_pressWidget = -1;
			urc_scrollHeld = SCROLL_NONE;
			urc_scrollWidget = -1;
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
			i = URC_PopupItemAt( rect );
			if ( i >= 0 ) {
				Cbuf_AddText( va( "%s\n", URC_PopupCommand( w, i ) ) );
				URC_PlaySound( w->clickSound );
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
		i = URC_WidgetUnderCursor();
		if ( i >= 0 ) {
			URC_ListScroll( i, key == K_MWHEELUP ? -1 : 1 );
		}
		return;
	}
	if ( key == K_UPARROW || key == K_DOWNARROW || key == K_KP_UPARROW || key == K_KP_DOWNARROW ) {
		urcWidget_t	*list = URC_TopSaveList();

		if ( list ) {
			URC_SaveSelect( list, urc_saveSelected + ( ( key == K_UPARROW || key == K_KP_UPARROW ) ? -1 : 1 ) );
		}
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
					qboolean	on = atof( URC_LinkValue( w ) ) == 0;

					URC_SetLinkCvar( w, on ? "1" : "0" );
					if ( on && w->checkedCommand[0] ) {
						Cbuf_AddText( va( "%s\n", w->checkedCommand ) );
					} else if ( !on && w->uncheckedCommand[0] ) {
						Cbuf_AddText( va( "%s\n", w->uncheckedCommand ) );
					}
				}
				break;
			case WC_SLIDER:
				if ( urc_cursorX < rect[0] + URC_SLIDER_ARROW ) {
					urc_sliderPress = i * 2;
					URC_SliderStep( w, -1 );
				} else if ( urc_cursorX >= rect[0] + rect[2] - URC_SLIDER_ARROW ) {
					urc_sliderPress = i * 2 + 1;
					URC_SliderStep( w, 1 );
				} else {
					urc_dragSlider = i;
					URC_SliderSet( w, rect );
				}
				break;
			case WC_PULLDOWN:
				URC_PlaySound( w->clickSound );
				if ( w->videoModes ) {
					URC_BuildModeList();
				}
				urc_popupWidget = URC_PopupCount( w ) ? i : -1;
				break;
			case WC_BINDLIST:
				urc_scrollWidget = i;
				URC_BindListClick( w, rect );
				break;
			case WC_LOADGAME:
				urc_scrollWidget = i;
				URC_SaveListClick( w, rect );
				break;
			case WC_MULTILINE:
				{
					int		lines = URC_MultiLineCount( w ), page = URC_MultiLinePage( w );

					urc_scrollWidget = i;
					urc_scrollHeld = URC_ScrollPart( rect, lines, page, w->lineTop, urc_cursorX, urc_cursorY );
					switch ( urc_scrollHeld ) {
					case SCROLL_UP: w->lineTop--; break;
					case SCROLL_DOWN: w->lineTop++; break;
					case SCROLL_PAGEUP: w->lineTop -= page; break;
					case SCROLL_PAGEDOWN: w->lineTop += page; break;
					case SCROLL_THUMB:
						{
							float	sr[4], ty, th;

							URC_ScrollRect( rect, sr );
							URC_ScrollThumb( sr, lines, page, w->lineTop, &ty, &th );
							urc_scrollGrab = urc_cursorY - ( sr[1] + ty );
						}
						break;
					default: break;
					}
					URC_MultiLineClamp( w );
				}
				break;
			default:
				urc_pressWidget = i;
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
		urc_cursorX = x;
		urc_cursorY = y;
		SCR_AdjustTo640( &urc_cursorX, &urc_cursorY );		// as the menus are drawn
	}
	if ( urc_dragSlider >= 0 && URC_TopWidgetRect( urc_dragSlider, rect ) ) {
		URC_SliderSet( &urc_widgets[urc_dragSlider], rect );
	}
	URC_ListDragThumb();
	URC_WindowMouseMove();
}

/*
=================
URC_WidgetCommand_f

"widgetcommand <widget> <command>": the load and save menu's buttons
talk to its list. loadgame loads the selected game, savegame saves one,
removegame deletes the selected one's files from the home directory.
=================
*/
static void URC_WidgetCommand_f( void ) {
	const char	*cmd;
	urcSave_t	*s;

	if ( Cmd_Argc() < 3 ) {
		Com_Printf( "Usage: widgetcommand <name> <command>\n" );
		return;
	}
	if ( Q_stricmp( Cmd_Argv( 1 ), "LoadSaveList" ) ) {
		return;
	}
	cmd = Cmd_Argv( 2 );
	if ( !Q_stricmp( cmd, "savegame" ) ) {
		Cbuf_AddText( "savegame\n" );
		return;
	}
	if ( urc_saveSelected < 1 || urc_saveSelected > urc_numSaves ) {
		return;
	}
	s = &urc_saves[urc_saveSelected - 1];
	if ( !Q_stricmp( cmd, "loadgame" ) ) {
		Cbuf_AddText( va( "loadgame %s\n", s->name ) );
	} else if ( !Q_stricmp( cmd, "removegame" ) ) {
		Com_WipeSaveGame( s->name );
		URC_LoadSaves();
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

// popmenu <restore_cvars> (0x40bbd0, 0x40b790): 1 puts back the cvars the
// page changed (the Back buttons), 0 keeps them (Apply)
static void URC_PopMenu_f( void ) {
	if ( Cmd_Argc() > 1 && atoi( Cmd_Argv( 1 ) ) ) {
		URC_RestoreCvars();
	}
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
	urc_hoverWidget = urc_dragSlider = urc_popupWidget = urc_sliderPress = urc_pressWidget = -1;
	urc_saveWidget = urc_scrollWidget = -1;
	urc_scrollHeld = SCROLL_NONE;
	Com_Memset( &urc_bindList, 0, sizeof( urc_bindList ) );
	urc_numSaves = urc_saveSelected = 0;
	urc_sliderThumb = re.RegisterShaderNoMip( "textures/menu/slider_thumb" );
	urc_sliderThumbSel = re.RegisterShaderNoMip( "textures/menu/slider_thumb_sel" );
	urc_arrowLeft = re.RegisterShaderNoMip( "textures/menu/arrow_left" );
	urc_arrowLeftSel = re.RegisterShaderNoMip( "textures/menu/arrow_left_sel" );
	urc_arrowRight = re.RegisterShaderNoMip( "textures/menu/arrow_right" );
	urc_arrowRightSel = re.RegisterShaderNoMip( "textures/menu/arrow_right_sel" );
	files = FS_ListFiles( "ui", ".urc", &numFiles );
	for ( i = 0 ; i < numFiles ; i++ ) {
		URC_LoadMenuFile( va( "ui/%s", files[i] ) );
	}
	FS_FreeFileList( files );
	Com_Printf( "%d menus from %d ui/*.urc files\n", urc_numMenus, numFiles );
	URC_LoadBuiltinMenus();

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

/*
=================
URC_PlayAliasSound

The inventory's sounds (retail 0x408860) go through the engine's local
sound (0x43ed80), as the client game's do: the name looked up among the
global aliases (global0.scr's "alias snd_inv_select ..."), or a file,
started on the listener's CHAN_MENU. The menus' aliascache lines do not
name them.
=================
*/
void URC_PlayAliasSound( const char *name ) {
	const char	*path;

	if ( !name[0] ) {
		return;
	}
	path = Alias_FindRandom( name );
	if ( !path ) {
		path = name;
	}
	S_StartLocalSound( S_RegisterSound( path, qfalse ), CHAN_MENU );
}

/*
=================
URC_Hud_f

ui_hud [1|0] (fakk2.exe 0x40e010): the HUD menus on or off.
=================
*/
static void URC_Hud_f( void ) {
	if ( Cmd_Argc() != 2 ) {
		Com_Printf( "Usage: ui_hud [1|0]\n" );
		return;
	}
	urc_hudOn = atoi( Cmd_Argv( 1 ) ) != 0;
}

/*
=================
URC_ResetCvars_f

ui_resetcvars (fakk2.exe 0x40d8f0, 0x4a44c0, 0x4a3da0): every cvar a
widget of the menu on top links to goes back to its default.
=================
*/
static void URC_ResetCvars_f( void ) {
	urcMenu_t	*menu;
	int			i;

	if ( !urc_stackDepth ) {
		return;
	}
	menu = &urc_menus[urc_stack[urc_stackDepth - 1]];
	for ( i = 0 ; i < menu->numWidgets ; i++ ) {
		const char	*name = urc_widgets[menu->firstWidget + i].linkcvar;

		if ( name[0] ) {
			Cvar_Reset( name );
		}
	}
}

/*
=================
URC_CheckRestart_f

ui_checkrestart (fakk2.exe 0x40d6a0, 0x4a44f0, 0x4a3cb0): when a cvar a
widget of the menu on top links to has a latched value other than the
one it had as the menu came up, restart what applies it: retail runs
vid_restart for a CVAR_LATCH cvar, else snd_restart for a sound latch,
the video taking precedence. Here snd_restart restarts the video as
well, so a sound latch asks for it and the video alone for vid_restart.
Retail first brings its console window ("FAKK2 Console") and mini
console up or down by ui_console and ui_minicon; those windows are not
made here.
=================
*/
static void URC_CheckRestart_f( void ) {
	urcMenu_t	*menu;
	qboolean	vid = qfalse, snd = qfalse;
	int			i;

	if ( !urc_stackDepth ) {
		return;
	}
	menu = &urc_menus[urc_stack[urc_stackDepth - 1]];
	for ( i = 0 ; i < menu->numWidgets ; i++ ) {
		urcWidget_t	*w = &urc_widgets[menu->firstWidget + i];
		const char	*latched;
		int			flags;

		if ( !w->linkcvar[0] ) {
			continue;
		}
		latched = Cvar_LatchedString( w->linkcvar );
		if ( !latched || !Q_stricmp( latched, w->linkSnapshot ) ) {
			continue;
		}
		flags = Cvar_Flags( w->linkcvar );
		if ( flags & CVAR_SOUND_LATCH ) {
			snd = qtrue;
		} else if ( flags & CVAR_LATCH ) {
			vid = qtrue;
		}
	}
	if ( snd ) {
		Cbuf_AddText( "snd_restart\n" );
	} else if ( vid ) {
		Cbuf_AddText( "vid_restart\n" );
	}
}

void URC_SetSaveShot( qboolean on ) {
	urc_saveShot = on;
}

// a game was saved: the load and save menu lists it at once
void URC_SavesChanged( void ) {
	if ( urc_saveWidget >= 0 ) {
		URC_LoadSaves();
	}
}

void URC_Init( void ) {
	ui_drawcoords = Cvar_Get( "ui_drawcoords", "0", 0 );
	ui_urcdebug = Cvar_Get( "ui_urcdebug", "0", CVAR_TEMP );
	Cvar_Get( "shortversion", "1.02", CVAR_ROM );	// the main menu's version label; the game data's version
	Cvar_Get( "ui_newvidmode", "-1", 0 );			// UI_Init (0x40ea46); see URC_UpdateCatcher
	Cvar_Get( "ui_console", "0", CVAR_ARCHIVE );		// UI_Init (0x40ea31, 0x40e9f0): the console and mini console
	Cvar_Get( "ui_minicon", "0", CVAR_ARCHIVE );		// windows, which are not made here (ui_checkrestart)
	Cvar_Get( "ui_resolution", "", 0 );				// OpenFAKK2's (URC_UpdateOptionCvars)
	Cvar_Get( "ui_anisotropy", "", 0 );

	urc_numFonts = 0;
	urc_stackDepth = 0;
	Com_Memset( &urc_print, 0, sizeof( urc_print ) );
	urc_addFadeShader = re.RegisterShaderNoMip( URC_ADDFADE_SHADER );
	urc_loadingShader = re.RegisterShaderNoMip( URC_LOADING_SHADER );
	urc_ritualShader = re.RegisterShaderNoMip( URC_RITUAL_SHADER );
	urc_gatheringShader = re.RegisterShaderNoMip( URC_GATHERING_SHADER );
	urc_introStep = 0;
	urc_introShader = 0;
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
	Cmd_AddCommand( "widgetcommand", URC_WidgetCommand_f );
	Cmd_AddCommand( "ui_hud", URC_Hud_f );
	Cmd_AddCommand( "dialog", URC_Dialog_f );
	Cmd_AddCommand( "ui_testlist", URC_TestList_f );
	Cmd_AddCommand( "notepad", URC_Notepad_f );		// UI_Init (0x40eb35)
	Cmd_AddCommand( "ui_resetcvars", URC_ResetCvars_f );
	Cmd_AddCommand( "ui_checkrestart", URC_CheckRestart_f );
	if ( !urc_introCommands ) {
		// the "dointro" UI_Init queues runs after the startup commands, by
		// when a startup map's load has shut this down
		urc_introCommands = qtrue;
		Cmd_AddCommand( "dointro", URC_DoIntro_f );
		Cmd_AddCommand( "_intro", URC_Intro_f );
	}
	urc_initialised = qtrue;
	urc_lastState = -1;

	// retail's UI_Init plays the intro when developer is 0; it runs again
	// after a vid_restart, which here would take the video menu away
	if ( !urc_introQueued && !Cvar_VariableIntegerValue( "developer" ) ) {
		urc_introQueued = qtrue;
		Cbuf_AddText( "dointro\n" );
	}
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
	Cmd_RemoveCommand( "ui_hud" );
	Cmd_RemoveCommand( "locationprint" );
	Cmd_RemoveCommand( "widgetcommand" );
	Cmd_RemoveCommand( "dialog" );
	Cmd_RemoveCommand( "ui_testlist" );
	Cmd_RemoveCommand( "notepad" );
	Cmd_RemoveCommand( "ui_resetcvars" );
	Cmd_RemoveCommand( "ui_checkrestart" );
	URC_CloseAllWindows();		// their materials die with the renderer
	urc_initialised = qfalse;
	urc_numFonts = 0;		// shader handles die with the renderer
	urc_loadingShader = 0;
	urc_ritualShader = urc_gatheringShader = 0;
	urc_introStep = 0;
	urc_introShader = 0;
}
