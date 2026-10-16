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

#ifdef USE_INTERNAL_SDL_HEADERS
#	include "SDL.h"
#else
#	include <SDL.h>
#endif

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "../client/client.h"
#include "../sys/sys_local.h"

#if !SDL_VERSION_ATLEAST(2, 0, 17)
#define KMOD_SCROLL KMOD_RESERVED
#endif

static cvar_t *in_keyboardDebug     = NULL;

static SDL_GameController *gamepad = NULL;
static SDL_Joystick *stick = NULL;

static qboolean mouseAvailable = qfalse;
static qboolean mouseActive = qfalse;

static cvar_t *in_mouse             = NULL;
static cvar_t *in_nograb;

static cvar_t *in_joystick          = NULL;
static cvar_t *in_joystickThreshold = NULL;
static cvar_t *in_joystickNo        = NULL;
static cvar_t *in_joystickUseAnalog = NULL;
static cvar_t *in_gamepad;

static int vidRestartTime = 0;

static int in_eventTime = 0;

static SDL_Window *SDL_window = NULL;

// Sys_Milliseconds when IN_Frame started reading events, -1 outside it
static int in_frameStart = -1;

#define CTRL(a) ((a)-'a'+1)

/*
===============
IN_PrintKey
===============
*/
static void IN_PrintKey( const SDL_Keysym *keysym, keyNum_t key, qboolean down )
{
	if( down )
		Com_Printf( "+ " );
	else
		Com_Printf( "  " );

	Com_Printf( "Scancode: 0x%02x(%s) Sym: 0x%02x(%s)",
			keysym->scancode, SDL_GetScancodeName( keysym->scancode ),
			keysym->sym, SDL_GetKeyName( keysym->sym ) );

	if( keysym->mod & KMOD_LSHIFT )   Com_Printf( " KMOD_LSHIFT" );
	if( keysym->mod & KMOD_RSHIFT )   Com_Printf( " KMOD_RSHIFT" );
	if( keysym->mod & KMOD_LCTRL )    Com_Printf( " KMOD_LCTRL" );
	if( keysym->mod & KMOD_RCTRL )    Com_Printf( " KMOD_RCTRL" );
	if( keysym->mod & KMOD_LALT )     Com_Printf( " KMOD_LALT" );
	if( keysym->mod & KMOD_RALT )     Com_Printf( " KMOD_RALT" );
	if( keysym->mod & KMOD_LGUI )     Com_Printf( " KMOD_LGUI" );
	if( keysym->mod & KMOD_RGUI )     Com_Printf( " KMOD_RGUI" );
	if( keysym->mod & KMOD_NUM )      Com_Printf( " KMOD_NUM" );
	if( keysym->mod & KMOD_CAPS )     Com_Printf( " KMOD_CAPS" );
	if( keysym->mod & KMOD_MODE )     Com_Printf( " KMOD_MODE" );
	if( keysym->mod & KMOD_SCROLL )   Com_Printf( " KMOD_SCROLL" );

	Com_Printf( " Q:0x%02x(%s)\n", key, Key_KeynumToString( key ) );
}

#define MAX_CONSOLE_KEYS 16

/*
===============
IN_IsConsoleKey

TODO: If the SDL_Scancode situation improves, use it instead of
      both of these methods
===============
*/
static qboolean IN_IsConsoleKey( keyNum_t key, int character )
{
	typedef struct consoleKey_s
	{
		enum
		{
			QUAKE_KEY,
			CHARACTER
		} type;

		union
		{
			keyNum_t key;
			int character;
		} u;
	} consoleKey_t;

	static consoleKey_t consoleKeys[ MAX_CONSOLE_KEYS ];
	static int numConsoleKeys = 0;
	int i;

	// Only parse the variable when it changes
	if( cl_consoleKeys->modified )
	{
		char *text_p, *token;

		cl_consoleKeys->modified = qfalse;
		text_p = cl_consoleKeys->string;
		numConsoleKeys = 0;

		while( numConsoleKeys < MAX_CONSOLE_KEYS )
		{
			consoleKey_t *c = &consoleKeys[ numConsoleKeys ];
			int charCode = 0;

			token = COM_Parse( &text_p );
			if( !token[ 0 ] )
				break;

			charCode = Com_HexStrToInt( token );

			if( charCode > 0 )
			{
				c->type = CHARACTER;
				c->u.character = charCode;
			}
			else
			{
				c->type = QUAKE_KEY;
				c->u.key = Key_StringToKeynum( token );

				// 0 isn't a key
				if( c->u.key <= 0 )
					continue;
			}

			numConsoleKeys++;
		}
	}

	// If the character is the same as the key, prefer the character
	if( key == character )
		key = 0;

	for( i = 0; i < numConsoleKeys; i++ )
	{
		consoleKey_t *c = &consoleKeys[ i ];

		switch( c->type )
		{
			case QUAKE_KEY:
				if( key && c->u.key == key )
					return qtrue;
				break;

			case CHARACTER:
				if( c->u.character == character )
					return qtrue;
				break;
		}
	}

	return qfalse;
}

/*
===============
IN_TranslateSDLToQ3Key
===============
*/
static keyNum_t IN_TranslateSDLToQ3Key( SDL_Keysym *keysym, qboolean down )
{
	keyNum_t key = 0;

	if( keysym->scancode >= SDL_SCANCODE_1 && keysym->scancode <= SDL_SCANCODE_0 )
	{
		// Always map the number keys as such even if they actually map
		// to other characters (eg, "1" is "&" on an AZERTY keyboard).
		// This is required for SDL before 2.0.6, except on Windows
		// which already had this behavior.
		if( keysym->scancode == SDL_SCANCODE_0 )
			key = '0';
		else
			key = '1' + keysym->scancode - SDL_SCANCODE_1;
	}
	else if( keysym->sym >= SDLK_SPACE && keysym->sym < SDLK_DELETE )
	{
		// These happen to match the ASCII chars
		key = (int)keysym->sym;
	}
	else
	{
		switch( keysym->sym )
		{
			case SDLK_PAGEUP:       key = K_PGUP;          break;
			case SDLK_KP_9:         key = K_KP_PGUP;       break;
			case SDLK_PAGEDOWN:     key = K_PGDN;          break;
			case SDLK_KP_3:         key = K_KP_PGDN;       break;
			case SDLK_KP_7:         key = K_KP_HOME;       break;
			case SDLK_HOME:         key = K_HOME;          break;
			case SDLK_KP_1:         key = K_KP_END;        break;
			case SDLK_END:          key = K_END;           break;
			case SDLK_KP_4:         key = K_KP_LEFTARROW;  break;
			case SDLK_LEFT:         key = K_LEFTARROW;     break;
			case SDLK_KP_6:         key = K_KP_RIGHTARROW; break;
			case SDLK_RIGHT:        key = K_RIGHTARROW;    break;
			case SDLK_KP_2:         key = K_KP_DOWNARROW;  break;
			case SDLK_DOWN:         key = K_DOWNARROW;     break;
			case SDLK_KP_8:         key = K_KP_UPARROW;    break;
			case SDLK_UP:           key = K_UPARROW;       break;
			case SDLK_ESCAPE:       key = K_ESCAPE;        break;
			case SDLK_KP_ENTER:     key = K_KP_ENTER;      break;
			case SDLK_RETURN:       key = K_ENTER;         break;
			case SDLK_TAB:          key = K_TAB;           break;
			case SDLK_F1:           key = K_F1;            break;
			case SDLK_F2:           key = K_F2;            break;
			case SDLK_F3:           key = K_F3;            break;
			case SDLK_F4:           key = K_F4;            break;
			case SDLK_F5:           key = K_F5;            break;
			case SDLK_F6:           key = K_F6;            break;
			case SDLK_F7:           key = K_F7;            break;
			case SDLK_F8:           key = K_F8;            break;
			case SDLK_F9:           key = K_F9;            break;
			case SDLK_F10:          key = K_F10;           break;
			case SDLK_F11:          key = K_F11;           break;
			case SDLK_F12:          key = K_F12;           break;
			case SDLK_F13:          key = K_F13;           break;
			case SDLK_F14:          key = K_F14;           break;
			case SDLK_F15:          key = K_F15;           break;

			case SDLK_BACKSPACE:    key = K_BACKSPACE;     break;
			case SDLK_KP_PERIOD:    key = K_KP_DEL;        break;
			case SDLK_DELETE:       key = K_DEL;           break;
			case SDLK_PAUSE:        key = K_PAUSE;         break;

			case SDLK_LSHIFT:
			case SDLK_RSHIFT:       key = K_SHIFT;         break;

			case SDLK_LCTRL:
			case SDLK_RCTRL:        key = K_CTRL;          break;

#ifdef __APPLE__
			case SDLK_RGUI:
			case SDLK_LGUI:         key = K_COMMAND;       break;
#else
			case SDLK_RGUI:
			case SDLK_LGUI:         key = K_SUPER;         break;
#endif

			case SDLK_RALT:
			case SDLK_LALT:         key = K_ALT;           break;

			case SDLK_KP_5:         key = K_KP_5;          break;
			case SDLK_INSERT:       key = K_INS;           break;
			case SDLK_KP_0:         key = K_KP_INS;        break;
			case SDLK_KP_MULTIPLY:  key = K_KP_STAR;       break;
			case SDLK_KP_PLUS:      key = K_KP_PLUS;       break;
			case SDLK_KP_MINUS:     key = K_KP_MINUS;      break;
			case SDLK_KP_DIVIDE:    key = K_KP_SLASH;      break;

			case SDLK_MODE:         key = K_MODE;          break;
			case SDLK_HELP:         key = K_HELP;          break;
			case SDLK_PRINTSCREEN:  key = K_PRINT;         break;
			case SDLK_SYSREQ:       key = K_SYSREQ;        break;
			case SDLK_MENU:         key = K_MENU;          break;
			case SDLK_APPLICATION:	key = K_MENU;          break;
			case SDLK_POWER:        key = K_POWER;         break;
			case SDLK_UNDO:         key = K_UNDO;          break;
			case SDLK_SCROLLLOCK:   key = K_SCROLLOCK;     break;
			case SDLK_NUMLOCKCLEAR: key = K_KP_NUMLOCK;    break;
			case SDLK_CAPSLOCK:     key = K_CAPSLOCK;      break;

			default:
				if( !( keysym->sym & SDLK_SCANCODE_MASK ) && keysym->scancode <= 95 )
				{
					// Map Unicode characters to 95 world keys using the key's scan code.
					// FIXME: There aren't enough world keys to cover all the scancodes.
					// Maybe create a map of scancode to quake key at start up and on
					// key map change; allocate world key numbers as needed similar
					// to SDL 1.2.
					key = K_WORLD_0 + (int)keysym->scancode;
				}
				break;
		}
	}

	if( in_keyboardDebug->integer )
		IN_PrintKey( keysym, key, down );

	if( IN_IsConsoleKey( key, 0 ) )
	{
		// Console keys can't be bound or generate characters
		key = K_CONSOLE;
	}

	return key;
}

/*
===============
IN_GobbleMotionEvents
===============
*/
static void IN_GobbleMotionEvents( void )
{
	SDL_Event dummy[ 1 ];
	int val = 0;

	// Gobble any mouse motion events
	SDL_PumpEvents( );
	while( ( val = SDL_PeepEvents( dummy, 1, SDL_GETEVENT,
		SDL_MOUSEMOTION, SDL_MOUSEMOTION ) ) > 0 ) { }

	if ( val < 0 )
		Com_Printf( "IN_GobbleMotionEvents failed: %s\n", SDL_GetError( ) );
}

/*
===============
IN_ActivateMouse
===============
*/
static void IN_ActivateMouse( qboolean isFullscreen )
{
	if (!mouseAvailable || !SDL_WasInit( SDL_INIT_VIDEO ) )
		return;

	if( !mouseActive )
	{
		SDL_SetRelativeMouseMode( SDL_TRUE );
		SDL_SetWindowGrab( SDL_window, SDL_TRUE );

		IN_GobbleMotionEvents( );
	}

	// in_nograb makes no sense in fullscreen mode
	if( !isFullscreen )
	{
		if( in_nograb->modified || !mouseActive )
		{
			if( in_nograb->integer ) {
				SDL_SetRelativeMouseMode( SDL_FALSE );
				SDL_SetWindowGrab( SDL_window, SDL_FALSE );
			} else {
				SDL_SetRelativeMouseMode( SDL_TRUE );
				SDL_SetWindowGrab( SDL_window, SDL_TRUE );
			}

			in_nograb->modified = qfalse;
		}
	}

	mouseActive = qtrue;
}

/*
===============
IN_DeactivateMouse
===============
*/
static void IN_DeactivateMouse( qboolean isFullscreen )
{
	if( !SDL_WasInit( SDL_INIT_VIDEO ) )
		return;

	// Always show the cursor when the mouse is disabled,
	// but not when fullscreen
	if( !isFullscreen )
		SDL_ShowCursor( SDL_TRUE );

	if( !mouseAvailable )
		return;

	if( mouseActive )
	{
		IN_GobbleMotionEvents( );

		SDL_SetWindowGrab( SDL_window, SDL_FALSE );
		SDL_SetRelativeMouseMode( SDL_FALSE );

		// Don't warp the mouse unless the cursor is within the window
		if( SDL_GetWindowFlags( SDL_window ) & SDL_WINDOW_MOUSE_FOCUS )
			SDL_WarpMouseInWindow( SDL_window, cls.glconfig.vidWidth / 2, cls.glconfig.vidHeight / 2 );

		mouseActive = qfalse;
	}
}

// We translate axes movement into keypresses
static int joy_keys[16] = {
	K_LEFTARROW, K_RIGHTARROW,
	K_UPARROW, K_DOWNARROW,
	K_JOY17, K_JOY18,
	K_JOY19, K_JOY20,
	K_JOY21, K_JOY22,
	K_JOY23, K_JOY24,
	K_JOY25, K_JOY26,
	K_JOY27, K_JOY28
};

// translate hat events into keypresses
// the 4 highest buttons are used for the first hat ...
static int hat_keys[16] = {
	K_JOY29, K_JOY30,
	K_JOY31, K_JOY32,
	K_JOY25, K_JOY26,
	K_JOY27, K_JOY28,
	K_JOY21, K_JOY22,
	K_JOY23, K_JOY24,
	K_JOY17, K_JOY18,
	K_JOY19, K_JOY20
};


struct
{
	qboolean buttons[SDL_CONTROLLER_BUTTON_MAX + 1]; // +1 because old max was 16, current SDL_CONTROLLER_BUTTON_MAX is 15
	unsigned int oldaxes;
	int oldaaxes[MAX_JOYSTICK_AXIS];
	unsigned int oldhats;
} stick_state;


/*
===============
IN_InitJoystick
===============
*/
static void IN_PadRelease( void );

static void IN_InitJoystick( void )
{
	int i = 0;
	int total = 0;
	int index;
	char buf[16384] = "";

	if (gamepad)
	{
		IN_PadRelease( );
		SDL_GameControllerClose(gamepad);
	}
	in_padConnected = qfalse;

	if (stick != NULL)
		SDL_JoystickClose(stick);

	stick = NULL;
	gamepad = NULL;
	memset(&stick_state, '\0', sizeof (stick_state));

	// SDL 2.0.4 requires SDL_INIT_JOYSTICK to be initialized separately from
	// SDL_INIT_GAMECONTROLLER for SDL_JoystickOpen() to work correctly,
	// despite https://wiki.libsdl.org/SDL_Init (retrieved 2016-08-16)
	// indicating SDL_INIT_JOYSTICK should be initialized automatically.
	if (!SDL_WasInit(SDL_INIT_JOYSTICK))
	{
		Com_DPrintf("Calling SDL_Init(SDL_INIT_JOYSTICK)...\n");
		if (SDL_Init(SDL_INIT_JOYSTICK) != 0)
		{
			Com_DPrintf("SDL_Init(SDL_INIT_JOYSTICK) failed: %s\n", SDL_GetError());
			return;
		}
		Com_DPrintf("SDL_Init(SDL_INIT_JOYSTICK) passed.\n");
	}

	if (!SDL_WasInit(SDL_INIT_GAMECONTROLLER))
	{
		Com_DPrintf("Calling SDL_Init(SDL_INIT_GAMECONTROLLER)...\n");
		if (SDL_Init(SDL_INIT_GAMECONTROLLER) != 0)
		{
			Com_DPrintf("SDL_Init(SDL_INIT_GAMECONTROLLER) failed: %s\n", SDL_GetError());
			return;
		}
		Com_DPrintf("SDL_Init(SDL_INIT_GAMECONTROLLER) passed.\n");
	}

	total = SDL_NumJoysticks();
	Com_DPrintf("%d possible joysticks\n", total);

	// Print list and build cvar to allow ui to select joystick.
	for (i = 0; i < total; i++)
	{
		Q_strcat(buf, sizeof(buf), SDL_JoystickNameForIndex(i));
		Q_strcat(buf, sizeof(buf), "\n");
	}

	Cvar_Get( "in_availableJoysticks", "", CVAR_ROM );

	// Update cvar on in_restart or controller add/remove.
	Cvar_Set( "in_availableJoysticks", buf );

	if( !in_joystick->integer && !in_gamepad->integer ) {
		Com_DPrintf( "Joystick is not active.\n" );
		SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
		return;
	}

	in_joystickNo = Cvar_Get( "in_joystickNo", "0", CVAR_ARCHIVE );
	if( in_joystickNo->integer < 0 || in_joystickNo->integer >= total )
		Cvar_Set( "in_joystickNo", "0" );

	in_joystickUseAnalog = Cvar_Get( "in_joystickUseAnalog", "0", CVAR_ARCHIVE );

	// OpenFAKK2: with in_gamepad alone, only a game controller: the one
	// in_joystickNo names, or else the first
	index = in_joystickNo->integer;
	if( !in_joystick->integer && ( index >= total || !SDL_IsGameController( index ) ) ) {
		for( index = 0; index < total && !SDL_IsGameController( index ); index++ )
			;
		if( index >= total ) {
			Com_DPrintf( "No game controller connected.\n" );
			return;
		}
	}

	stick = SDL_JoystickOpen( index );

	if (stick == NULL) {
		Com_DPrintf( "No joystick opened: %s\n", SDL_GetError() );
		return;
	}

	if (SDL_IsGameController(index))
		gamepad = SDL_GameControllerOpen(index);
	in_padConnected = gamepad != NULL;
	if( gamepad )
		Com_Printf( "Controller: %s\n", SDL_GameControllerName( gamepad ) );

	Com_DPrintf( "Joystick %d opened\n", index );
	Com_DPrintf( "Name:       %s\n", SDL_JoystickNameForIndex(index) );
	Com_DPrintf( "Axes:       %d\n", SDL_JoystickNumAxes(stick) );
	Com_DPrintf( "Hats:       %d\n", SDL_JoystickNumHats(stick) );
	Com_DPrintf( "Buttons:    %d\n", SDL_JoystickNumButtons(stick) );
	Com_DPrintf( "Balls:      %d\n", SDL_JoystickNumBalls(stick) );
	Com_DPrintf( "Use Analog: %s\n", in_joystickUseAnalog->integer ? "Yes" : "No" );
	Com_DPrintf( "Is gamepad: %s\n", gamepad ? "Yes" : "No" );

	SDL_JoystickEventState(SDL_QUERY);
	SDL_GameControllerEventState(SDL_QUERY);
}

/*
===============
IN_ShutdownJoystick
===============
*/
static void IN_DetachVirtualPad( void );

static void IN_ShutdownJoystick( void )
{
	if ( !SDL_WasInit( SDL_INIT_GAMECONTROLLER ) )
		return;

	if ( !SDL_WasInit( SDL_INIT_JOYSTICK ) )
		return;

	if (gamepad)
	{
		IN_PadRelease( );
		SDL_GameControllerClose(gamepad);
		gamepad = NULL;
	}
	in_padConnected = qfalse;

	if (stick)
	{
		SDL_JoystickClose(stick);
		stick = NULL;
	}

	IN_DetachVirtualPad( );

	SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
	SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
}


/*
===============================================================================

GAME CONTROLLERS

OpenFAKK2: a controller SDL knows the layout of (an Xbox or PlayStation
pad and most others) is opened by default (in_gamepad), apart from the
Quake III joystick code that in_joystick turns on. Its buttons, triggers
and stick directions are the PAD0_ keys. A stick direction bound to a
movement or look command moves or turns by how far the stick goes
(SE_GAMEPAD_AXIS, cl_input.c); one bound to anything else presses its key
halfway over. While a menu is up the left stick moves the cursor and the
right one scrolls; cl_keys.c turns the buttons into the menu's keys.

===============================================================================
*/

#define PAD_STICK_PRESS		0.5f	// how far over a stick direction presses its key
#define PAD_STICK_RELEASE	0.4f	// and back in lets it go

typedef enum {
	PADMODE_GAME,	// the sticks move and turn
	PADMODE_MENU,	// the left stick moves the cursor, the right one scrolls
	PADMODE_BIND,	// a bind list cell waits: every stick direction is a key
	PADMODE_NONE	// the console is down
} padMode_t;

static const int padStickKeys[2][4] = {
	{ K_PAD0_LEFTSTICK_LEFT, K_PAD0_LEFTSTICK_RIGHT, K_PAD0_LEFTSTICK_UP, K_PAD0_LEFTSTICK_DOWN },
	{ K_PAD0_RIGHTSTICK_LEFT, K_PAD0_RIGHTSTICK_RIGHT, K_PAD0_RIGHTSTICK_UP, K_PAD0_RIGHTSTICK_DOWN }
};

static struct {
	qboolean	buttons[SDL_CONTROLLER_BUTTON_MAX];
	qboolean	stickKeys[2][4];		// stick directions held down as keys
	qboolean	triggers[2];
	int			analog[PAD_ANALOG_MAX];	// as last sent
	qboolean	active;					// something was pressed or pushed last frame
	int			lastTime;
	qboolean	cursorMoving;
	float		cursorX, cursorY;		// the menu cursor while the stick moves it
	int			scrollTime;				// when the right stick scrolls again
} pad;

static cvar_t *pad_deadzoneLeft;
static cvar_t *pad_deadzoneRight;
static cvar_t *pad_triggerThreshold;
static cvar_t *pad_cursorSpeed;

/*
===============
IN_KeyFor
===============
*/
static keyNum_t IN_KeyFor( SDL_GameControllerButton sdlButton )
{
	switch(sdlButton)
	{
		case SDL_CONTROLLER_BUTTON_A:               return K_PAD0_A;
		case SDL_CONTROLLER_BUTTON_B:               return K_PAD0_B;
		case SDL_CONTROLLER_BUTTON_X:               return K_PAD0_X;
		case SDL_CONTROLLER_BUTTON_Y:               return K_PAD0_Y;
		case SDL_CONTROLLER_BUTTON_BACK:            return K_PAD0_BACK;
		case SDL_CONTROLLER_BUTTON_GUIDE:           return K_PAD0_GUIDE;
		case SDL_CONTROLLER_BUTTON_START:           return K_PAD0_START;
		case SDL_CONTROLLER_BUTTON_LEFTSTICK:       return K_PAD0_LEFTSTICK_CLICK;
		case SDL_CONTROLLER_BUTTON_RIGHTSTICK:      return K_PAD0_RIGHTSTICK_CLICK;
		case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:    return K_PAD0_LEFTSHOULDER;
		case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:   return K_PAD0_RIGHTSHOULDER;
		case SDL_CONTROLLER_BUTTON_DPAD_UP:         return K_PAD0_DPAD_UP;
		case SDL_CONTROLLER_BUTTON_DPAD_DOWN:       return K_PAD0_DPAD_DOWN;
		case SDL_CONTROLLER_BUTTON_DPAD_LEFT:       return K_PAD0_DPAD_LEFT;
		case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:      return K_PAD0_DPAD_RIGHT;
#if SDL_VERSION_ATLEAST( 2, 0, 14 )
		case SDL_CONTROLLER_BUTTON_MISC1:           return K_PAD0_MISC1;
		case SDL_CONTROLLER_BUTTON_PADDLE1:         return K_PAD0_PADDLE1;
		case SDL_CONTROLLER_BUTTON_PADDLE2:         return K_PAD0_PADDLE2;
		case SDL_CONTROLLER_BUTTON_PADDLE3:         return K_PAD0_PADDLE3;
		case SDL_CONTROLLER_BUTTON_PADDLE4:         return K_PAD0_PADDLE4;
		case SDL_CONTROLLER_BUTTON_TOUCHPAD:        return K_PAD0_TOUCHPAD;
#endif

		default:
			Com_Error(ERR_DROP, "IN_KeyFor: unhandled SDL button: %d", sdlButton);
			break;
	}

	return 0;
}

/*
===============
IN_PadAnalogBinding

Whether a stick direction's key is bound to a command the stick works by
degree, and which way along which channel.
===============
*/
static qboolean IN_PadAnalogBinding( int key, int *channel, int *sign )
{
	static const struct {
		const char	*command;
		int			channel, sign;
	} analog[] = {
		{ "+forward", PAD_FORWARD, 1 },
		{ "+back", PAD_FORWARD, -1 },
		{ "+moveright", PAD_SIDE, 1 },
		{ "+moveleft", PAD_SIDE, -1 },
		{ "+right", PAD_YAW, 1 },
		{ "+left", PAD_YAW, -1 },
		{ "+lookdown", PAD_PITCH, 1 },
		{ "+lookup", PAD_PITCH, -1 }
	};
	const char *bind = Key_GetBinding( key );
	int i;

	if ( !bind || bind[0] != '+' )
		return qfalse;

	for ( i = 0; i < ARRAY_LEN( analog ); i++ )
	{
		if ( !Q_stricmp( bind, analog[i].command ) )
		{
			*channel = analog[i].channel;
			*sign = analog[i].sign;
			return qtrue;
		}
	}
	return qfalse;
}

/*
===============
IN_PadStick

A stick's position past its dead zone, as -1 to 1 on each axis: the dead
zone is a circle, and the stick's travel beyond it is stretched to the
full range so it starts moving from nothing.
===============
*/
static void IN_PadStick( int s, float *x, float *y )
{
	float dz = s ? pad_deadzoneRight->value : pad_deadzoneLeft->value;
	float mag, scale;

	*x = SDL_GameControllerGetAxis( gamepad, s ? SDL_CONTROLLER_AXIS_RIGHTX : SDL_CONTROLLER_AXIS_LEFTX ) / 32767.0f;
	*y = SDL_GameControllerGetAxis( gamepad, s ? SDL_CONTROLLER_AXIS_RIGHTY : SDL_CONTROLLER_AXIS_LEFTY ) / 32767.0f;
	*x = Com_Clamp( -1, 1, *x );
	*y = Com_Clamp( -1, 1, *y );
	dz = Com_Clamp( 0, 0.95f, dz );

	mag = sqrt( *x * *x + *y * *y );
	if ( mag <= dz )
	{
		*x = *y = 0;
		return;
	}
	scale = MIN( 1, ( mag - dz ) / ( 1 - dz ) ) / mag;
	*x *= scale;
	*y *= scale;
}

static void IN_PadKey( qboolean *held, int key, qboolean down )
{
	if ( *held == down )
		return;
	Com_QueueEvent( in_eventTime, SE_KEY, key, down, 0, NULL );
	*held = down;
}

static void IN_PadAnalog( int channel, float value )
{
	int v = (int)( 32767 * Com_Clamp( -1, 1, value ) );

	if ( v == pad.analog[channel] )
		return;
	Com_QueueEvent( in_eventTime, SE_GAMEPAD_AXIS, channel, v, 0, NULL );
	pad.analog[channel] = v;
}

/*
===============
IN_PadRelease

Everything held let go, before the controller is closed or another opened.
===============
*/
static void IN_PadRelease( void )
{
	int i, d;

	for ( i = 0; i < SDL_CONTROLLER_BUTTON_MAX; i++ )
		IN_PadKey( &pad.buttons[i], IN_KeyFor( SDL_CONTROLLER_BUTTON_A + i ), qfalse );
	for ( i = 0; i < 2; i++ )
	{
		for ( d = 0; d < 4; d++ )
			IN_PadKey( &pad.stickKeys[i][d], padStickKeys[i][d], qfalse );
	}
	IN_PadKey( &pad.triggers[0], K_PAD0_LEFTTRIGGER, qfalse );
	IN_PadKey( &pad.triggers[1], K_PAD0_RIGHTTRIGGER, qfalse );
	for ( i = 0; i < PAD_ANALOG_MAX; i++ )
		IN_PadAnalog( i, 0 );
	Com_Memset( &pad, 0, sizeof( pad ) );
}

static padMode_t IN_PadMode( void )
{
	if ( Key_GetCatcher( ) & KEYCATCH_CONSOLE )
		return PADMODE_NONE;
	if ( URC_BindWaiting( ) )
		return PADMODE_BIND;
	if ( URC_HasMenus( ) || ( Key_GetCatcher( ) & KEYCATCH_UI ) )
		return PADMODE_MENU;
	return PADMODE_GAME;
}

/*
===============
IN_PadCursor

The left stick moves the menu cursor, at up to pad_cursorSpeed screen
heights a second, slower near the middle. The menus follow the system
cursor (SDL_MOUSEMOTION), so it is that cursor that moves.
===============
*/
static void IN_PadCursor( float x, float y, int msec )
{
	float mag = sqrt( x * x + y * y );
	float speed;
	int wx, wy;

	if ( mag <= 0 || !SDL_window || cls.glconfig.vidWidth <= 0 || cls.glconfig.vidHeight <= 0 )
	{
		pad.cursorMoving = qfalse;
		return;
	}
	if ( !pad.cursorMoving )
	{
		// from wherever the mouse left it
		SDL_GetMouseState( &wx, &wy );
		pad.cursorX = wx;
		pad.cursorY = wy;
		pad.cursorMoving = qtrue;
	}

	speed = pad_cursorSpeed->value * cls.glconfig.vidHeight * mag * msec * 0.001f;
	pad.cursorX = Com_Clamp( 0, cls.glconfig.vidWidth - 1, pad.cursorX + x * speed );
	pad.cursorY = Com_Clamp( 0, cls.glconfig.vidHeight - 1, pad.cursorY + y * speed );
	wx = (int)pad.cursorX;
	wy = (int)pad.cursorY;

	SDL_WarpMouseInWindow( SDL_window, wx, wy );
	Com_QueueEvent( in_eventTime, SE_MOUSE_ABS, wx, wy, 0, NULL );
}

/*
===============
IN_PadScroll

The right stick pushed up or down turns the mouse wheel over the list
under the cursor, faster the further it goes.
===============
*/
static void IN_PadScroll( float y, int now )
{
	float over = fabs( y );
	int key;

	if ( over < PAD_STICK_PRESS )
	{
		pad.scrollTime = 0;
		return;
	}
	if ( pad.scrollTime && now < pad.scrollTime )
		return;

	key = y < 0 ? K_MWHEELUP : K_MWHEELDOWN;
	Com_QueueEvent( in_eventTime, SE_KEY, key, qtrue, 0, NULL );
	Com_QueueEvent( in_eventTime, SE_KEY, key, qfalse, 0, NULL );
	pad.scrollTime = now + (int)( 250 - 170 * ( over - PAD_STICK_PRESS ) / ( 1 - PAD_STICK_PRESS ) );
}

/*
===============
IN_GamepadMove
===============
*/
static void IN_GamepadMove( void )
{
	qboolean pressed[SDL_CONTROLLER_BUTTON_MAX];
	float sx[2], sy[2], trigger[2], analog[PAD_ANALOG_MAX];
	float threshold = Com_Clamp( 0.05f, 0.95f, pad_triggerThreshold->value );
	qboolean active = qfalse;
	padMode_t mode = IN_PadMode( );
	int now = Sys_Milliseconds( );
	int msec = Com_Clamp( 0, 100, now - pad.lastTime );
	int i, s, d;

	pad.lastTime = now;

	SDL_GameControllerUpdate();

	for (i = 0; i < SDL_CONTROLLER_BUTTON_MAX; i++)
	{
		pressed[i] = SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_A + i) != 0;
		active |= pressed[i];
	}
	for (s = 0; s < 2; s++)
	{
		IN_PadStick( s, &sx[s], &sy[s] );
		trigger[s] = SDL_GameControllerGetAxis( gamepad, s ? SDL_CONTROLLER_AXIS_TRIGGERRIGHT : SDL_CONTROLLER_AXIS_TRIGGERLEFT ) / 32767.0f;
		active |= sx[s] != 0 || sy[s] != 0 || trigger[s] > threshold;
	}

	// a controller nobody has bound yet gets its bindings as it is first used
	if ( active && !pad.active )
		Key_PadDefaultBindings( );
	pad.active = active;

	for (i = 0; i < SDL_CONTROLLER_BUTTON_MAX; i++)
		IN_PadKey( &pad.buttons[i], IN_KeyFor( SDL_CONTROLLER_BUTTON_A + i ), pressed[i] );

	IN_PadKey( &pad.triggers[0], K_PAD0_LEFTTRIGGER, mode != PADMODE_NONE && trigger[0] > threshold );
	IN_PadKey( &pad.triggers[1], K_PAD0_RIGHTTRIGGER, mode != PADMODE_NONE && trigger[1] > threshold );

	for (i = 0; i < PAD_ANALOG_MAX; i++)
		analog[i] = 0;

	for (s = 0; s < 2; s++)
	{
		// how far the stick goes left, right, up and down
		float over[4];

		over[0] = MAX( 0, -sx[s] );
		over[1] = MAX( 0, sx[s] );
		over[2] = MAX( 0, -sy[s] );
		over[3] = MAX( 0, sy[s] );

		for (d = 0; d < 4; d++)
		{
			int key = padStickKeys[s][d];
			int channel, sign;
			qboolean down = qfalse;

			if ( mode == PADMODE_GAME && IN_PadAnalogBinding( key, &channel, &sign ) )
				analog[channel] += sign * over[d];
			else if ( mode == PADMODE_GAME || mode == PADMODE_BIND )
				down = over[d] > ( pad.stickKeys[s][d] ? PAD_STICK_RELEASE : PAD_STICK_PRESS );

			IN_PadKey( &pad.stickKeys[s][d], key, down );
		}
	}

	for (i = 0; i < PAD_ANALOG_MAX; i++)
		IN_PadAnalog( i, analog[i] );

	if ( mode == PADMODE_MENU )
	{
		IN_PadCursor( sx[0], sy[0], msec );
		IN_PadScroll( sy[1], now );
	}
	else
	{
		pad.cursorMoving = qfalse;
		pad.scrollTime = 0;
	}
}

/*
===============
IN_VirtualPad_f

"vpad attach", "vpad button <name> <0|1>", "vpad axis <name> <value>",
"vpad detach": a controller made by SDL, for the test harness, which
cannot press a real one. Names are SDL's (a, b, start, dpup, leftx,
lefttrigger...); axis values run -32768 to 32767, triggers 0 to 32767.
===============
*/
static SDL_Joystick *vpad = NULL;
static int vpadIndex = -1;

static void IN_DetachVirtualPad( void )
{
	if ( !vpad )
		return;
	SDL_JoystickClose( vpad );
	SDL_JoystickDetachVirtual( vpadIndex );
	vpad = NULL;
	vpadIndex = -1;
}

static void IN_VirtualPad_f( void )
{
	const char *cmd = Cmd_Argv( 1 );

	if ( !Q_stricmp( cmd, "attach" ) )
	{
		SDL_VirtualJoystickDesc desc;

		if ( vpad )
			return;
		if ( !SDL_WasInit( SDL_INIT_JOYSTICK ) || !SDL_WasInit( SDL_INIT_GAMECONTROLLER ) )
		{
			Com_Printf( "vpad: controllers are off (in_gamepad 0)\n" );
			return;
		}
		Com_Memset( &desc, 0, sizeof( desc ) );
		desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
		desc.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
		desc.naxes = SDL_CONTROLLER_AXIS_MAX;
		desc.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
		desc.button_mask = ( 1u << SDL_CONTROLLER_BUTTON_MAX ) - 1;
		desc.axis_mask = ( 1u << SDL_CONTROLLER_AXIS_MAX ) - 1;
		desc.name = "OpenFAKK2 virtual controller";
		// the harness's window need not have the focus
		SDL_SetHint( SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1" );
		vpadIndex = SDL_JoystickAttachVirtualEx( &desc );
		if ( vpadIndex < 0 || !( vpad = SDL_JoystickOpen( vpadIndex ) ) )
		{
			Com_Printf( "vpad: %s\n", SDL_GetError( ) );
			vpadIndex = -1;
			return;
		}
		// the triggers rest at the bottom of their range
		SDL_JoystickSetVirtualAxis( vpad, SDL_CONTROLLER_AXIS_TRIGGERLEFT, -32768 );
		SDL_JoystickSetVirtualAxis( vpad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, -32768 );
		// SDL_CONTROLLERDEVICEADDED opens it
	}
	else if ( !Q_stricmp( cmd, "detach" ) )
	{
		IN_DetachVirtualPad( );
	}
	else if ( !Q_stricmp( cmd, "button" ) && vpad && Cmd_Argc( ) == 4 )
	{
		SDL_GameControllerButton b = SDL_GameControllerGetButtonFromString( Cmd_Argv( 2 ) );

		if ( b == SDL_CONTROLLER_BUTTON_INVALID )
			Com_Printf( "vpad: no button %s\n", Cmd_Argv( 2 ) );
		else
			SDL_JoystickSetVirtualButton( vpad, b, atoi( Cmd_Argv( 3 ) ) ? 1 : 0 );
	}
	else if ( !Q_stricmp( cmd, "axis" ) && vpad && Cmd_Argc( ) == 4 )
	{
		SDL_GameControllerAxis a = SDL_GameControllerGetAxisFromString( Cmd_Argv( 2 ) );
		int v = atoi( Cmd_Argv( 3 ) );

		if ( a == SDL_CONTROLLER_AXIS_INVALID )
		{
			Com_Printf( "vpad: no axis %s\n", Cmd_Argv( 2 ) );
			return;
		}
		if ( a == SDL_CONTROLLER_AXIS_TRIGGERLEFT || a == SDL_CONTROLLER_AXIS_TRIGGERRIGHT )
			v = v * 2 - 32768;
		SDL_JoystickSetVirtualAxis( vpad, a, (Sint16)Com_Clamp( -32768, 32767, v ) );
	}
	else
	{
		Com_Printf( "Usage: vpad attach | detach | button <name> <0|1> | axis <name> <value>\n" );
	}
}

/*
===============
IN_JoyMove
===============
*/
static void IN_JoyMove( void )
{
	unsigned int axes = 0;
	unsigned int hats = 0;
	int total = 0;
	int i = 0;

	if (gamepad)
	{
		IN_GamepadMove();
		return;
	}

	if (!stick)
		return;

	SDL_JoystickUpdate();

	// update the ball state.
	total = SDL_JoystickNumBalls(stick);
	if (total > 0)
	{
		int balldx = 0;
		int balldy = 0;
		for (i = 0; i < total; i++)
		{
			int dx = 0;
			int dy = 0;
			SDL_JoystickGetBall(stick, i, &dx, &dy);
			balldx += dx;
			balldy += dy;
		}
		if (balldx || balldy)
		{
			// !!! FIXME: is this good for stick balls, or just mice?
			// Scale like the mouse input...
			if (abs(balldx) > 1)
				balldx *= 2;
			if (abs(balldy) > 1)
				balldy *= 2;
			Com_QueueEvent( in_eventTime, SE_MOUSE, balldx, balldy, 0, NULL );
		}
	}

	// now query the stick buttons...
	total = SDL_JoystickNumButtons(stick);
	if (total > 0)
	{
		if (total > ARRAY_LEN(stick_state.buttons))
			total = ARRAY_LEN(stick_state.buttons);
		for (i = 0; i < total; i++)
		{
			qboolean pressed = (SDL_JoystickGetButton(stick, i) != 0);
			if (pressed != stick_state.buttons[i])
			{
				Com_QueueEvent( in_eventTime, SE_KEY, K_JOY1 + i, pressed, 0, NULL );
				stick_state.buttons[i] = pressed;
			}
		}
	}

	// look at the hats...
	total = SDL_JoystickNumHats(stick);
	if (total > 0)
	{
		if (total > 4) total = 4;
		for (i = 0; i < total; i++)
		{
			((Uint8 *)&hats)[i] = SDL_JoystickGetHat(stick, i);
		}
	}

	// update hat state
	if (hats != stick_state.oldhats)
	{
		for( i = 0; i < 4; i++ ) {
			if( ((Uint8 *)&hats)[i] != ((Uint8 *)&stick_state.oldhats)[i] ) {
				// release event
				switch( ((Uint8 *)&stick_state.oldhats)[i] ) {
					case SDL_HAT_UP:
						Com_QueueEvent( in_eventTime, SE_KEY, hat_keys[4*i + 0], qfalse, 0, NULL );
						break;
					case SDL_HAT_RIGHT:
						Com_QueueEvent( in_eventTime, SE_KEY, hat_keys[4*i + 1], qfalse, 0, NULL );
						break;
					case SDL_HAT_DOWN:
						Com_QueueEvent( in_eventTime, SE_KEY, hat_keys[4*i + 2], qfalse, 0, NULL );
						break;
					case SDL_HAT_LEFT:
						Com_QueueEvent( in_eventTime, SE_KEY, hat_keys[4*i + 3], qfalse, 0, NULL );
						break;
					case SDL_HAT_RIGHTUP:
						Com_QueueEvent( in_eventTime, SE_KEY, hat_keys[4*i + 0], qfalse, 0, NULL );
						Com_QueueEvent( in_eventTime, SE_KEY, hat_keys[4*i + 1], qfalse, 0, NULL );
						break;
					case SDL_HAT_RIGHTDOWN:
						Com_QueueEvent( in_eventTime, SE_KEY, hat_keys[4*i + 2], qfalse, 0, NULL );
						Com_QueueEvent( in_eventTime, SE_KEY, hat_keys[4*i + 1], qfalse, 0, NULL );
						break;
					case SDL_HAT_LEFTUP:
						Com_QueueEvent( in_eventTime, SE_KEY, hat_keys[4*i + 0], qfalse, 0, NULL );
						Com_QueueEvent( in_eventTime, SE_KEY, hat_keys[4*i + 3], qfalse, 0, NULL );
						break;
					case SDL_HAT_LEFTDOWN:
						Com_QueueEvent( in_eventTime, SE_KEY, hat_keys[4*i + 2], qfalse, 0, NULL );
						Com_QueueEvent( in_eventTime, SE_KEY, hat_keys[4*i + 3], qfalse, 0, NULL );
						break;
					default:
						break;
				}
				// press event
				switch( ((Uint8 *)&hats)[i] ) {
					case SDL_HAT_UP:
						Com_QueueEvent( in_eventTime, SE_KEY, hat_keys[4*i + 0], qtrue, 0, NULL );
						break;
					case SDL_HAT_RIGHT:
						Com_QueueEvent( in_eventTime, SE_KEY, hat_keys[4*i + 1], qtrue, 0, NULL );
						break;
					case SDL_HAT_DOWN:
						Com_QueueEvent( in_eventTime, SE_KEY, hat_keys[4*i + 2], qtrue, 0, NULL );
						break;
					case SDL_HAT_LEFT:
						Com_QueueEvent( in_eventTime, SE_KEY, hat_keys[4*i + 3], qtrue, 0, NULL );
						break;
					case SDL_HAT_RIGHTUP:
						Com_QueueEvent( in_eventTime, SE_KEY, hat_keys[4*i + 0], qtrue, 0, NULL );
						Com_QueueEvent( in_eventTime, SE_KEY, hat_keys[4*i + 1], qtrue, 0, NULL );
						break;
					case SDL_HAT_RIGHTDOWN:
						Com_QueueEvent( in_eventTime, SE_KEY, hat_keys[4*i + 2], qtrue, 0, NULL );
						Com_QueueEvent( in_eventTime, SE_KEY, hat_keys[4*i + 1], qtrue, 0, NULL );
						break;
					case SDL_HAT_LEFTUP:
						Com_QueueEvent( in_eventTime, SE_KEY, hat_keys[4*i + 0], qtrue, 0, NULL );
						Com_QueueEvent( in_eventTime, SE_KEY, hat_keys[4*i + 3], qtrue, 0, NULL );
						break;
					case SDL_HAT_LEFTDOWN:
						Com_QueueEvent( in_eventTime, SE_KEY, hat_keys[4*i + 2], qtrue, 0, NULL );
						Com_QueueEvent( in_eventTime, SE_KEY, hat_keys[4*i + 3], qtrue, 0, NULL );
						break;
					default:
						break;
				}
			}
		}
	}

	// save hat state
	stick_state.oldhats = hats;

	// finally, look at the axes...
	total = SDL_JoystickNumAxes(stick);
	if (total > 0)
	{
		if (in_joystickUseAnalog->integer)
		{
			if (total > MAX_JOYSTICK_AXIS) total = MAX_JOYSTICK_AXIS;
			for (i = 0; i < total; i++)
			{
				Sint16 axis = SDL_JoystickGetAxis(stick, i);
				float f = ( (float) abs(axis) ) / 32767.0f;

				if( f < in_joystickThreshold->value ) axis = 0;

				if ( axis != stick_state.oldaaxes[i] )
				{
					Com_QueueEvent( in_eventTime, SE_JOYSTICK_AXIS, i, axis, 0, NULL );
					stick_state.oldaaxes[i] = axis;
				}
			}
		}
		else
		{
			if (total > 16) total = 16;
			for (i = 0; i < total; i++)
			{
				Sint16 axis = SDL_JoystickGetAxis(stick, i);
				float f = ( (float) axis ) / 32767.0f;
				if( f < -in_joystickThreshold->value ) {
					axes |= ( 1 << ( i * 2 ) );
				} else if( f > in_joystickThreshold->value ) {
					axes |= ( 1 << ( ( i * 2 ) + 1 ) );
				}
			}
		}
	}

	/* Time to update axes state based on old vs. new. */
	if (axes != stick_state.oldaxes)
	{
		for( i = 0; i < 16; i++ ) {
			if( ( axes & ( 1 << i ) ) && !( stick_state.oldaxes & ( 1 << i ) ) ) {
				Com_QueueEvent( in_eventTime, SE_KEY, joy_keys[i], qtrue, 0, NULL );
			}

			if( !( axes & ( 1 << i ) ) && ( stick_state.oldaxes & ( 1 << i ) ) ) {
				Com_QueueEvent( in_eventTime, SE_KEY, joy_keys[i], qfalse, 0, NULL );
			}
		}
	}

	/* Save for future generations. */
	stick_state.oldaxes = axes;
}

/*
===============
IN_ProcessEvents
===============
*/
static void IN_ProcessEvents( void )
{
	SDL_Event e;
	keyNum_t key = 0;
	static keyNum_t lastKeyDown = 0;

	if( !SDL_WasInit( SDL_INIT_VIDEO ) )
			return;

	while( SDL_PollEvent( &e ) )
	{
		switch( e.type )
		{
			case SDL_KEYDOWN:
				if ( e.key.repeat && Key_GetCatcher( ) == 0 )
					break;

				if( ( key = IN_TranslateSDLToQ3Key( &e.key.keysym, qtrue ) ) )
					Com_QueueEvent( in_eventTime, SE_KEY, key, qtrue, 0, NULL );

				if( key == K_BACKSPACE )
					Com_QueueEvent( in_eventTime, SE_CHAR, CTRL('h'), 0, 0, NULL );
				else if( keys[K_CTRL].down && key >= 'a' && key <= 'z' )
					Com_QueueEvent( in_eventTime, SE_CHAR, CTRL(key), 0, 0, NULL );

				lastKeyDown = key;
				break;

			case SDL_KEYUP:
				if( ( key = IN_TranslateSDLToQ3Key( &e.key.keysym, qfalse ) ) )
					Com_QueueEvent( in_eventTime, SE_KEY, key, qfalse, 0, NULL );

				lastKeyDown = 0;
				break;

			case SDL_TEXTINPUT:
				if( lastKeyDown != K_CONSOLE )
				{
					char *c = e.text.text;

					// Quick and dirty UTF-8 to UTF-32 conversion
					while( *c )
					{
						int utf32 = 0;

						if( ( *c & 0x80 ) == 0 )
							utf32 = *c++;
						else if( ( *c & 0xE0 ) == 0xC0 ) // 110x xxxx
						{
							utf32 |= ( *c++ & 0x1F ) << 6;
							utf32 |= ( *c++ & 0x3F );
						}
						else if( ( *c & 0xF0 ) == 0xE0 ) // 1110 xxxx
						{
							utf32 |= ( *c++ & 0x0F ) << 12;
							utf32 |= ( *c++ & 0x3F ) << 6;
							utf32 |= ( *c++ & 0x3F );
						}
						else if( ( *c & 0xF8 ) == 0xF0 ) // 1111 0xxx
						{
							utf32 |= ( *c++ & 0x07 ) << 18;
							utf32 |= ( *c++ & 0x3F ) << 12;
							utf32 |= ( *c++ & 0x3F ) << 6;
							utf32 |= ( *c++ & 0x3F );
						}
						else
						{
							Com_DPrintf( "Unrecognised UTF-8 lead byte: 0x%x\n", (unsigned int)*c );
							c++;
						}

						if( utf32 != 0 )
						{
							if( IN_IsConsoleKey( 0, utf32 ) )
							{
								Com_QueueEvent( in_eventTime, SE_KEY, K_CONSOLE, qtrue, 0, NULL );
								Com_QueueEvent( in_eventTime, SE_KEY, K_CONSOLE, qfalse, 0, NULL );
							}
							else
								Com_QueueEvent( in_eventTime, SE_CHAR, utf32, 0, 0, NULL );
						}
					}
				}
				break;

			case SDL_MOUSEMOTION:
				if( mouseActive )
				{
					if( !e.motion.xrel && !e.motion.yrel )
						break;
					Com_QueueEvent( in_eventTime, SE_MOUSE, e.motion.xrel, e.motion.yrel, 0, NULL );
				}
				else if( URC_HasMenus( ) || cl_mouseShown )
				{
					// FAKK2's menus use the system cursor, as the retail client did
					Com_QueueEvent( in_eventTime, SE_MOUSE_ABS, e.motion.x, e.motion.y, 0, NULL );
				}
				break;

			case SDL_MOUSEBUTTONDOWN:
			case SDL_MOUSEBUTTONUP:
				{
					int b;
					switch( e.button.button )
					{
						case SDL_BUTTON_LEFT:   b = K_MOUSE1;     break;
						case SDL_BUTTON_MIDDLE: b = K_MOUSE3;     break;
						case SDL_BUTTON_RIGHT:  b = K_MOUSE2;     break;
						case SDL_BUTTON_X1:     b = K_MOUSE4;     break;
						case SDL_BUTTON_X2:     b = K_MOUSE5;     break;
						default:                b = K_AUX1 + ( e.button.button - SDL_BUTTON_X2 + 1 ) % 16; break;
					}
					Com_QueueEvent( in_eventTime, SE_KEY, b,
						( e.type == SDL_MOUSEBUTTONDOWN ? qtrue : qfalse ), 0, NULL );
				}
				break;

			case SDL_MOUSEWHEEL:
				if( e.wheel.y > 0 )
				{
					Com_QueueEvent( in_eventTime, SE_KEY, K_MWHEELUP, qtrue, 0, NULL );
					Com_QueueEvent( in_eventTime, SE_KEY, K_MWHEELUP, qfalse, 0, NULL );
				}
				else if( e.wheel.y < 0 )
				{
					Com_QueueEvent( in_eventTime, SE_KEY, K_MWHEELDOWN, qtrue, 0, NULL );
					Com_QueueEvent( in_eventTime, SE_KEY, K_MWHEELDOWN, qfalse, 0, NULL );
				}
				break;

			case SDL_CONTROLLERDEVICEADDED:
			case SDL_CONTROLLERDEVICEREMOVED:
				if (in_joystick->integer || in_gamepad->integer)
					IN_InitJoystick();
				break;

			case SDL_QUIT:
				Cbuf_ExecuteText(EXEC_NOW, "quit Closed window\n");
				break;

			case SDL_WINDOWEVENT:
				switch( e.window.event )
				{
					case SDL_WINDOWEVENT_RESIZED:
						{
							int width, height;

							width = e.window.data1;
							height = e.window.data2;

							// ignore this event on fullscreen
							if( cls.glconfig.isFullscreen )
							{
								break;
							}

							// check if size actually changed
							if( cls.glconfig.vidWidth == width && cls.glconfig.vidHeight == height )
							{
								break;
							}

							Cvar_SetValue( "r_customwidth", width );
							Cvar_SetValue( "r_customheight", height );
							Cvar_Set( "r_mode", "-1" );

							// Wait until user stops dragging for 1 second, so
							// we aren't constantly recreating the GL context while
							// he tries to drag...
							vidRestartTime = Sys_Milliseconds( ) + 1000;
						}
						break;

					case SDL_WINDOWEVENT_MINIMIZED:    Cvar_SetValue( "com_minimized", 1 ); break;
					case SDL_WINDOWEVENT_RESTORED:
					case SDL_WINDOWEVENT_MAXIMIZED:    Cvar_SetValue( "com_minimized", 0 ); break;
					case SDL_WINDOWEVENT_FOCUS_LOST:   Cvar_SetValue( "com_unfocused", 1 ); break;
					case SDL_WINDOWEVENT_FOCUS_GAINED: Cvar_SetValue( "com_unfocused", 0 ); break;
				}
				break;

#if defined(PROTOCOL_HANDLER) && defined(__APPLE__)
			case SDL_DROPFILE:
				{
					char *filename = e.drop.file;

					// Handle macOS open URL event. URL protocol scheme must be set in Info.plist.
					if( !Q_strncmp( filename, PROTOCOL_HANDLER ":", strlen( PROTOCOL_HANDLER ":" ) ) )
					{
						char *protocolCommand = Sys_ParseProtocolUri( filename );

						if( protocolCommand )
						{
							Cbuf_ExecuteText( EXEC_APPEND, va( "%s\n", protocolCommand ) );
							free( protocolCommand );
						}
					}

					SDL_free( filename );
				}
				break;
#endif

			default:
				break;
		}
	}
}

/*
===============
IN_ModalLoopWatch

Dragging or resizing the window holds IN_Frame inside SDL_PumpEvents for as
long as the mouse button is down: Windows runs its own move loop there, and
no frame runs. The music is fed from S_Update a quarter second ahead, so it
ran dry and came back on release. SDL sends an expose event every 10 ms of
that loop; keep the sound going from it. Events are only read at the top of
the frame, so nothing is halfway through when S_Update runs here.
===============
*/
static int SDLCALL IN_ModalLoopWatch( void *userdata, SDL_Event *e )
{
	if( e->type != SDL_WINDOWEVENT || e->window.event != SDL_WINDOWEVENT_EXPOSED )
		return 0;

	// an expose read in the normal course of a frame is not a stall
	if( in_frameStart < 0 || Sys_Milliseconds( ) - in_frameStart < 20 )
		return 0;

	S_Update( );
	return 0;
}

/*
===============
IN_Frame
===============
*/
void IN_Frame( void )
{
	qboolean loading;

	in_frameStart = Sys_Milliseconds( );

	IN_JoyMove( );

	// If not DISCONNECTED (main menu) or ACTIVE (in game), we're loading
	loading = ( clc.state != CA_DISCONNECTED && clc.state != CA_ACTIVE );

	// update isFullscreen since it might of changed since the last vid_restart
	cls.glconfig.isFullscreen = Cvar_VariableIntegerValue( "r_fullscreen" ) != 0;

	if( !cls.glconfig.isFullscreen && ( Key_GetCatcher( ) & KEYCATCH_CONSOLE ) )
	{
		// Console is down in windowed mode
		IN_DeactivateMouse( cls.glconfig.isFullscreen );
	}
	else if( URC_HasMenus( ) || cl_mouseShown )
	{
		// FAKK2: a menu or +togglemouse's cursor is up; it takes the system
		// cursor (ShowCursor in the retail client), even in fullscreen
		IN_DeactivateMouse( qfalse );
	}
	else if( !cls.glconfig.isFullscreen && loading )
	{
		// Loading in windowed mode
		IN_DeactivateMouse( cls.glconfig.isFullscreen );
	}
	else if( !( SDL_GetWindowFlags( SDL_window ) & SDL_WINDOW_INPUT_FOCUS ) )
	{
		// Window not got focus
		IN_DeactivateMouse( cls.glconfig.isFullscreen );
	}
	else
		IN_ActivateMouse( cls.glconfig.isFullscreen );

	IN_ProcessEvents( );

	in_frameStart = -1;

	// Set event time for next frame to earliest possible time an event could happen
	in_eventTime = Sys_Milliseconds( );

	// In case we had to delay actual restart of video system
	if( ( vidRestartTime != 0 ) && ( vidRestartTime < Sys_Milliseconds( ) ) )
	{
		vidRestartTime = 0;
		Cbuf_AddText( "vid_restart\n" );
	}
}

/*
===============
IN_Init
===============
*/
void IN_Init( void *windowData )
{
	int appState;

	if( !SDL_WasInit( SDL_INIT_VIDEO ) )
	{
		Com_Error( ERR_FATAL, "IN_Init called before SDL_Init( SDL_INIT_VIDEO )" );
		return;
	}

	SDL_window = (SDL_Window *)windowData;

	Com_DPrintf( "\n------- Input Initialization -------\n" );

	in_keyboardDebug = Cvar_Get( "in_keyboardDebug", "0", CVAR_ARCHIVE );

	// mouse variables
	in_mouse = Cvar_Get( "in_mouse", "1", CVAR_ARCHIVE );
	// OpenFAKK2: not archived, so a test harness run with +set in_nograb 1
	// does not leave the next normal run without a mouse
	in_nograb = Cvar_Get( "in_nograb", "0", 0 );
	if ( in_nograb->flags & CVAR_ARCHIVE ) {
		// a "seta in_nograb" left in config.cfg by an older build would keep
		// the mouse free in every run and be written back on every quit
		in_nograb->flags &= ~CVAR_ARCHIVE;
		Cvar_Set( "in_nograb", "0" );
	}

	in_joystick = Cvar_Get( "in_joystick", "0", CVAR_ARCHIVE|CVAR_LATCH );
	in_joystickThreshold = Cvar_Get( "joy_threshold", "0.15", CVAR_ARCHIVE );

	// OpenFAKK2: game controllers, on by default; the sticks' dead zones and
	// the triggers' press point as fractions of their travel, and the menu
	// cursor's speed in screen heights a second
	in_gamepad = Cvar_Get( "in_gamepad", "1", CVAR_ARCHIVE|CVAR_LATCH );
	pad_deadzoneLeft = Cvar_Get( "pad_deadzoneLeft", "0.2", CVAR_ARCHIVE );
	pad_deadzoneRight = Cvar_Get( "pad_deadzoneRight", "0.2", CVAR_ARCHIVE );
	pad_triggerThreshold = Cvar_Get( "pad_triggerThreshold", "0.25", CVAR_ARCHIVE );
	pad_cursorSpeed = Cvar_Get( "pad_cursorSpeed", "1.2", CVAR_ARCHIVE );
	Cmd_RemoveCommand( "vpad" );
	Cmd_AddCommand( "vpad", IN_VirtualPad_f );

#if defined(PROTOCOL_HANDLER) && defined(__APPLE__)
	SDL_EventState( SDL_DROPFILE, SDL_ENABLE );
#endif

	SDL_StartTextInput( );

	// IN_Restart comes back through here
	SDL_DelEventWatch( IN_ModalLoopWatch, NULL );
	SDL_AddEventWatch( IN_ModalLoopWatch, NULL );

	mouseAvailable = ( in_mouse->value != 0 );
	IN_DeactivateMouse( Cvar_VariableIntegerValue( "r_fullscreen" ) != 0 );

	appState = SDL_GetWindowFlags( SDL_window );
	Cvar_SetValue( "com_unfocused",	!( appState & SDL_WINDOW_INPUT_FOCUS ) );
	Cvar_SetValue( "com_minimized", appState & SDL_WINDOW_MINIMIZED );

	IN_InitJoystick( );
	Com_DPrintf( "------------------------------------\n" );
}

/*
===============
IN_Shutdown
===============
*/
void IN_Shutdown( void )
{
	SDL_DelEventWatch( IN_ModalLoopWatch, NULL );
	SDL_StopTextInput( );

	IN_DeactivateMouse( Cvar_VariableIntegerValue( "r_fullscreen" ) != 0 );
	mouseAvailable = qfalse;

	IN_ShutdownJoystick( );

	SDL_window = NULL;
}

/*
===============
IN_Restart
===============
*/
void IN_Restart( void )
{
	IN_ShutdownJoystick( );
	IN_Init( SDL_window );
}
