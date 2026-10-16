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
#include "client.h"

/*

key up events are sent even if in console mode

*/

field_t	historyEditLines[COMMAND_HISTORY];

int			nextHistoryLine;		// the last line in the history buffer, not masked
int			historyLine;	// the line being displayed from history buffer
							// will be <= nextHistoryLine

field_t		g_consoleField;
field_t		chatField;
qboolean	chat_team;

int			chat_playerNum;


qboolean	key_overstrikeMode;

int				anykeydown;
qkey_t		keys[MAX_KEYS];


typedef struct {
	char	*name;
	int		keynum;
} keyname_t;


// names not in this list can either be lowercase ascii, or '0xnn' hex sequences
keyname_t keynames[] =
{
	{"TAB", K_TAB},
	{"ENTER", K_ENTER},
	{"ESCAPE", K_ESCAPE},
	{"SPACE", K_SPACE},
	{"BACKSPACE", K_BACKSPACE},
	{"UPARROW", K_UPARROW},
	{"DOWNARROW", K_DOWNARROW},
	{"LEFTARROW", K_LEFTARROW},
	{"RIGHTARROW", K_RIGHTARROW},

	{"ALT", K_ALT},
	{"CTRL", K_CTRL},
	{"SHIFT", K_SHIFT},

	{"COMMAND", K_COMMAND},

	{"CAPSLOCK", K_CAPSLOCK},

	
	{"F1", K_F1},
	{"F2", K_F2},
	{"F3", K_F3},
	{"F4", K_F4},
	{"F5", K_F5},
	{"F6", K_F6},
	{"F7", K_F7},
	{"F8", K_F8},
	{"F9", K_F9},
	{"F10", K_F10},
	{"F11", K_F11},
	{"F12", K_F12},
	{"F13", K_F13},
	{"F14", K_F14},
	{"F15", K_F15},

	{"INS", K_INS},
	{"DEL", K_DEL},
	{"PGDN", K_PGDN},
	{"PGUP", K_PGUP},
	{"HOME", K_HOME},
	{"END", K_END},

	{"MOUSE1", K_MOUSE1},
	{"MOUSE2", K_MOUSE2},
	{"MOUSE3", K_MOUSE3},
	{"MOUSE4", K_MOUSE4},
	{"MOUSE5", K_MOUSE5},

	{"MWHEELUP",	K_MWHEELUP },
	{"MWHEELDOWN",	K_MWHEELDOWN },

	{"JOY1", K_JOY1},
	{"JOY2", K_JOY2},
	{"JOY3", K_JOY3},
	{"JOY4", K_JOY4},
	{"JOY5", K_JOY5},
	{"JOY6", K_JOY6},
	{"JOY7", K_JOY7},
	{"JOY8", K_JOY8},
	{"JOY9", K_JOY9},
	{"JOY10", K_JOY10},
	{"JOY11", K_JOY11},
	{"JOY12", K_JOY12},
	{"JOY13", K_JOY13},
	{"JOY14", K_JOY14},
	{"JOY15", K_JOY15},
	{"JOY16", K_JOY16},
	{"JOY17", K_JOY17},
	{"JOY18", K_JOY18},
	{"JOY19", K_JOY19},
	{"JOY20", K_JOY20},
	{"JOY21", K_JOY21},
	{"JOY22", K_JOY22},
	{"JOY23", K_JOY23},
	{"JOY24", K_JOY24},
	{"JOY25", K_JOY25},
	{"JOY26", K_JOY26},
	{"JOY27", K_JOY27},
	{"JOY28", K_JOY28},
	{"JOY29", K_JOY29},
	{"JOY30", K_JOY30},
	{"JOY31", K_JOY31},
	{"JOY32", K_JOY32},

	{"AUX1", K_AUX1},
	{"AUX2", K_AUX2},
	{"AUX3", K_AUX3},
	{"AUX4", K_AUX4},
	{"AUX5", K_AUX5},
	{"AUX6", K_AUX6},
	{"AUX7", K_AUX7},
	{"AUX8", K_AUX8},
	{"AUX9", K_AUX9},
	{"AUX10", K_AUX10},
	{"AUX11", K_AUX11},
	{"AUX12", K_AUX12},
	{"AUX13", K_AUX13},
	{"AUX14", K_AUX14},
	{"AUX15", K_AUX15},
	{"AUX16", K_AUX16},

	{"KP_HOME",			K_KP_HOME },
	{"KP_UPARROW",		K_KP_UPARROW },
	{"KP_PGUP",			K_KP_PGUP },
	{"KP_LEFTARROW",	K_KP_LEFTARROW },
	{"KP_5",			K_KP_5 },
	{"KP_RIGHTARROW",	K_KP_RIGHTARROW },
	{"KP_END",			K_KP_END },
	{"KP_DOWNARROW",	K_KP_DOWNARROW },
	{"KP_PGDN",			K_KP_PGDN },
	{"KP_ENTER",		K_KP_ENTER },
	{"KP_INS",			K_KP_INS },
	{"KP_DEL",			K_KP_DEL },
	{"KP_SLASH",		K_KP_SLASH },
	{"KP_MINUS",		K_KP_MINUS },
	{"KP_PLUS",			K_KP_PLUS },
	{"KP_NUMLOCK",		K_KP_NUMLOCK },
	{"KP_STAR",			K_KP_STAR },
	{"KP_EQUALS",		K_KP_EQUALS },

	{"PAUSE", K_PAUSE},
	
	{"SEMICOLON", ';'},	// because a raw semicolon separates commands

	{"WORLD_0", K_WORLD_0},
	{"WORLD_1", K_WORLD_1},
	{"WORLD_2", K_WORLD_2},
	{"WORLD_3", K_WORLD_3},
	{"WORLD_4", K_WORLD_4},
	{"WORLD_5", K_WORLD_5},
	{"WORLD_6", K_WORLD_6},
	{"WORLD_7", K_WORLD_7},
	{"WORLD_8", K_WORLD_8},
	{"WORLD_9", K_WORLD_9},
	{"WORLD_10", K_WORLD_10},
	{"WORLD_11", K_WORLD_11},
	{"WORLD_12", K_WORLD_12},
	{"WORLD_13", K_WORLD_13},
	{"WORLD_14", K_WORLD_14},
	{"WORLD_15", K_WORLD_15},
	{"WORLD_16", K_WORLD_16},
	{"WORLD_17", K_WORLD_17},
	{"WORLD_18", K_WORLD_18},
	{"WORLD_19", K_WORLD_19},
	{"WORLD_20", K_WORLD_20},
	{"WORLD_21", K_WORLD_21},
	{"WORLD_22", K_WORLD_22},
	{"WORLD_23", K_WORLD_23},
	{"WORLD_24", K_WORLD_24},
	{"WORLD_25", K_WORLD_25},
	{"WORLD_26", K_WORLD_26},
	{"WORLD_27", K_WORLD_27},
	{"WORLD_28", K_WORLD_28},
	{"WORLD_29", K_WORLD_29},
	{"WORLD_30", K_WORLD_30},
	{"WORLD_31", K_WORLD_31},
	{"WORLD_32", K_WORLD_32},
	{"WORLD_33", K_WORLD_33},
	{"WORLD_34", K_WORLD_34},
	{"WORLD_35", K_WORLD_35},
	{"WORLD_36", K_WORLD_36},
	{"WORLD_37", K_WORLD_37},
	{"WORLD_38", K_WORLD_38},
	{"WORLD_39", K_WORLD_39},
	{"WORLD_40", K_WORLD_40},
	{"WORLD_41", K_WORLD_41},
	{"WORLD_42", K_WORLD_42},
	{"WORLD_43", K_WORLD_43},
	{"WORLD_44", K_WORLD_44},
	{"WORLD_45", K_WORLD_45},
	{"WORLD_46", K_WORLD_46},
	{"WORLD_47", K_WORLD_47},
	{"WORLD_48", K_WORLD_48},
	{"WORLD_49", K_WORLD_49},
	{"WORLD_50", K_WORLD_50},
	{"WORLD_51", K_WORLD_51},
	{"WORLD_52", K_WORLD_52},
	{"WORLD_53", K_WORLD_53},
	{"WORLD_54", K_WORLD_54},
	{"WORLD_55", K_WORLD_55},
	{"WORLD_56", K_WORLD_56},
	{"WORLD_57", K_WORLD_57},
	{"WORLD_58", K_WORLD_58},
	{"WORLD_59", K_WORLD_59},
	{"WORLD_60", K_WORLD_60},
	{"WORLD_61", K_WORLD_61},
	{"WORLD_62", K_WORLD_62},
	{"WORLD_63", K_WORLD_63},
	{"WORLD_64", K_WORLD_64},
	{"WORLD_65", K_WORLD_65},
	{"WORLD_66", K_WORLD_66},
	{"WORLD_67", K_WORLD_67},
	{"WORLD_68", K_WORLD_68},
	{"WORLD_69", K_WORLD_69},
	{"WORLD_70", K_WORLD_70},
	{"WORLD_71", K_WORLD_71},
	{"WORLD_72", K_WORLD_72},
	{"WORLD_73", K_WORLD_73},
	{"WORLD_74", K_WORLD_74},
	{"WORLD_75", K_WORLD_75},
	{"WORLD_76", K_WORLD_76},
	{"WORLD_77", K_WORLD_77},
	{"WORLD_78", K_WORLD_78},
	{"WORLD_79", K_WORLD_79},
	{"WORLD_80", K_WORLD_80},
	{"WORLD_81", K_WORLD_81},
	{"WORLD_82", K_WORLD_82},
	{"WORLD_83", K_WORLD_83},
	{"WORLD_84", K_WORLD_84},
	{"WORLD_85", K_WORLD_85},
	{"WORLD_86", K_WORLD_86},
	{"WORLD_87", K_WORLD_87},
	{"WORLD_88", K_WORLD_88},
	{"WORLD_89", K_WORLD_89},
	{"WORLD_90", K_WORLD_90},
	{"WORLD_91", K_WORLD_91},
	{"WORLD_92", K_WORLD_92},
	{"WORLD_93", K_WORLD_93},
	{"WORLD_94", K_WORLD_94},
	{"WORLD_95", K_WORLD_95},

	{"WINDOWS", K_SUPER},
	{"COMPOSE", K_COMPOSE},
	{"MODE", K_MODE},
	{"HELP", K_HELP},
	{"PRINT", K_PRINT},
	{"SYSREQ", K_SYSREQ},
	{"SCROLLOCK", K_SCROLLOCK },
	{"BREAK", K_BREAK},
	{"MENU", K_MENU},
	{"POWER", K_POWER},
	{"EURO", K_EURO},
	{"UNDO", K_UNDO},

	{"PAD0_A", K_PAD0_A },
	{"PAD0_B", K_PAD0_B },
	{"PAD0_X", K_PAD0_X },
	{"PAD0_Y", K_PAD0_Y },
	{"PAD0_BACK", K_PAD0_BACK },
	{"PAD0_GUIDE", K_PAD0_GUIDE },
	{"PAD0_START", K_PAD0_START },
	{"PAD0_LEFTSTICK_CLICK", K_PAD0_LEFTSTICK_CLICK },
	{"PAD0_RIGHTSTICK_CLICK", K_PAD0_RIGHTSTICK_CLICK },
	{"PAD0_LEFTSHOULDER", K_PAD0_LEFTSHOULDER },
	{"PAD0_RIGHTSHOULDER", K_PAD0_RIGHTSHOULDER },
	{"PAD0_DPAD_UP", K_PAD0_DPAD_UP },
	{"PAD0_DPAD_DOWN", K_PAD0_DPAD_DOWN },
	{"PAD0_DPAD_LEFT", K_PAD0_DPAD_LEFT },
	{"PAD0_DPAD_RIGHT", K_PAD0_DPAD_RIGHT },

	{"PAD0_LEFTSTICK_LEFT", K_PAD0_LEFTSTICK_LEFT },
	{"PAD0_LEFTSTICK_RIGHT", K_PAD0_LEFTSTICK_RIGHT },
	{"PAD0_LEFTSTICK_UP", K_PAD0_LEFTSTICK_UP },
	{"PAD0_LEFTSTICK_DOWN", K_PAD0_LEFTSTICK_DOWN },
	{"PAD0_RIGHTSTICK_LEFT", K_PAD0_RIGHTSTICK_LEFT },
	{"PAD0_RIGHTSTICK_RIGHT", K_PAD0_RIGHTSTICK_RIGHT },
	{"PAD0_RIGHTSTICK_UP", K_PAD0_RIGHTSTICK_UP },
	{"PAD0_RIGHTSTICK_DOWN", K_PAD0_RIGHTSTICK_DOWN },
	{"PAD0_LEFTTRIGGER", K_PAD0_LEFTTRIGGER },
	{"PAD0_RIGHTTRIGGER", K_PAD0_RIGHTTRIGGER },

	{"PAD0_MISC1", K_PAD0_MISC1 },
	{"PAD0_PADDLE1", K_PAD0_PADDLE1 },
	{"PAD0_PADDLE2", K_PAD0_PADDLE2 },
	{"PAD0_PADDLE3", K_PAD0_PADDLE3 },
	{"PAD0_PADDLE4", K_PAD0_PADDLE4 },
	{"PAD0_TOUCHPAD", K_PAD0_TOUCHPAD },

	{NULL,0}
};

/*
=============================================================================

EDIT FIELDS

=============================================================================
*/


/*
===================
Field_Draw

Handles horizontal scrolling and cursor blinking
x, y, and width are in pixels
===================
*/
static void Field_VariableSizeDraw( field_t *edit, int x, int y, int width, int size,
	qboolean drawSmall, qboolean showCursor, qboolean noColorEscape ) {
	int		len;
	int		drawLen;
	int		prestep;
	int		cursorChar;
	char	str[MAX_STRING_CHARS];
	int		i;

	drawLen = edit->widthInChars - 1; // - 1 so there is always a space for the cursor
	len = strlen( edit->buffer );

	// guarantee that cursor will be visible
	if ( len <= drawLen ) {
		prestep = 0;
	} else {
		if ( edit->scroll + drawLen > len ) {
			edit->scroll = len - drawLen;
			if ( edit->scroll < 0 ) {
				edit->scroll = 0;
			}
		}
		prestep = edit->scroll;
	}

	if ( prestep + drawLen > len ) {
		drawLen = len - prestep;
	}

	// extract <drawLen> characters from the field at <prestep>
	if ( drawLen >= MAX_STRING_CHARS ) {
		Com_Error( ERR_DROP, "drawLen >= MAX_STRING_CHARS" );
	}

	Com_Memcpy( str, edit->buffer + prestep, drawLen );
	str[ drawLen ] = 0;

	// draw it
	if ( drawSmall ) {
		float	color[4];

		color[0] = color[1] = color[2] = color[3] = 1.0;
		SCR_DrawSmallStringExt( x, y, str, color, qfalse, noColorEscape );
	} else {
		// draw big string with drop shadow
		SCR_DrawBigString( x, y, str, 1.0, noColorEscape );
	}

	// draw the cursor
	if ( showCursor ) {
		if ( (int)( cls.realtime >> 8 ) & 1 ) {
			return;		// off blink
		}

		if ( key_overstrikeMode ) {
			cursorChar = 11;
		} else {
			cursorChar = 10;
		}

		i = drawLen - strlen( str );

		if ( drawSmall ) {
			SCR_DrawSmallChar( x + ( edit->cursor - prestep - i ) * size, y, cursorChar );
		} else {
			str[0] = cursorChar;
			str[1] = 0;
			SCR_DrawBigString( x + ( edit->cursor - prestep - i ) * size, y, str, 1.0, qfalse );

		}
	}
}

void Field_Draw( field_t *edit, int x, int y, int width, qboolean showCursor, qboolean noColorEscape ) 
{
	Field_VariableSizeDraw( edit, x, y, width, g_smallchar_width, qtrue, showCursor, noColorEscape );
}

void Field_BigDraw( field_t *edit, int x, int y, int width, qboolean showCursor, qboolean noColorEscape ) 
{
	Field_VariableSizeDraw( edit, x, y, width, BIGCHAR_WIDTH, qfalse, showCursor, noColorEscape );
}

/*
================
Field_Paste
================
*/
void Field_Paste( field_t *edit ) {
	char	*cbd;
	int		pasteLen, i;

	cbd = Sys_GetClipboardData();

	if ( !cbd ) {
		return;
	}

	// send as if typed, so insert / overstrike works properly
	pasteLen = strlen( cbd );
	for ( i = 0 ; i < pasteLen ; i++ ) {
		Field_CharEvent( edit, cbd[i] );
	}

	Z_Free( cbd );
}

/*
=================
Field_KeyDownEvent

Performs the basic line editing functions for the console,
in-game talk, and menu fields

Key events are used for non-printable characters, others are gotten from char events.
=================
*/
void Field_KeyDownEvent( field_t *edit, int key ) {
	int		len;

	// shift-insert is paste
	if ( ( ( key == K_INS ) || ( key == K_KP_INS ) ) && keys[K_SHIFT].down ) {
		Field_Paste( edit );
		return;
	}

	key = tolower( key );
	len = strlen( edit->buffer );

	switch ( key ) {
		case K_DEL:
			if ( edit->cursor < len ) {
				memmove( edit->buffer + edit->cursor, 
					edit->buffer + edit->cursor + 1, len - edit->cursor );
			}
			break;

		case K_RIGHTARROW:
			if ( edit->cursor < len ) {
				edit->cursor++;
			}
			break;

		case K_LEFTARROW:
			if ( edit->cursor > 0 ) {
				edit->cursor--;
			}
			break;

		case K_HOME:
			edit->cursor = 0;
			break;

		case K_END:
			edit->cursor = len;
			break;

		case K_INS:
			key_overstrikeMode = !key_overstrikeMode;
			break;

		default:
			break;
	}

	// Change scroll if cursor is no longer visible
	if ( edit->cursor < edit->scroll ) {
		edit->scroll = edit->cursor;
	} else if ( edit->cursor >= edit->scroll + edit->widthInChars && edit->cursor <= len ) {
		edit->scroll = edit->cursor - edit->widthInChars + 1;
	}
}

/*
==================
Field_CharEvent
==================
*/
void Field_CharEvent( field_t *edit, int ch ) {
	int		len;

	if ( ch == 'v' - 'a' + 1 ) {	// ctrl-v is paste
		Field_Paste( edit );
		return;
	}

	if ( ch == 'c' - 'a' + 1 ) {	// ctrl-c clears the field
		Field_Clear( edit );
		return;
	}

	len = strlen( edit->buffer );

	if ( ch == 'h' - 'a' + 1 )	{	// ctrl-h is backspace
		if ( edit->cursor > 0 ) {
			memmove( edit->buffer + edit->cursor - 1, 
				edit->buffer + edit->cursor, len + 1 - edit->cursor );
			edit->cursor--;
			if ( edit->cursor < edit->scroll )
			{
				edit->scroll--;
			}
		}
		return;
	}

	if ( ch == 'a' - 'a' + 1 ) {	// ctrl-a is home
		edit->cursor = 0;
		edit->scroll = 0;
		return;
	}

	if ( ch == 'e' - 'a' + 1 ) {	// ctrl-e is end
		edit->cursor = len;
		edit->scroll = edit->cursor - edit->widthInChars;
		return;
	}

	//
	// ignore any other non printable chars
	//
	if ( ch < 32 ) {
		return;
	}

	if ( key_overstrikeMode ) {	
		// - 2 to leave room for the leading slash and trailing \0
		if ( edit->cursor == MAX_EDIT_LINE - 2 )
			return;
		edit->buffer[edit->cursor] = ch;
		edit->cursor++;
	} else {	// insert mode
		// - 2 to leave room for the leading slash and trailing \0
		if ( len == MAX_EDIT_LINE - 2 ) {
			return; // all full
		}
		memmove( edit->buffer + edit->cursor + 1, 
			edit->buffer + edit->cursor, len + 1 - edit->cursor );
		edit->buffer[edit->cursor] = ch;
		edit->cursor++;
	}


	if ( edit->cursor >= edit->widthInChars ) {
		edit->scroll++;
	}

	if ( edit->cursor == len + 1) {
		edit->buffer[edit->cursor] = 0;
	}
}

/*
=============================================================================

CONSOLE LINE EDITING

==============================================================================
*/

/*
====================
Console_Key

Handles history and console scrollback
====================
*/
void Console_Key (int key) {
	// ctrl-L clears screen
	if ( key == 'l' && keys[K_CTRL].down ) {
		Cbuf_AddText ("clear\n");
		return;
	}

	// enter finishes the line
	if ( key == K_ENTER || key == K_KP_ENTER ) {
		// if not in the game explicitly prepend a slash if needed
		if ( clc.state != CA_ACTIVE && con_autochat->integer &&
				g_consoleField.buffer[0] &&
				g_consoleField.buffer[0] != '\\' &&
				g_consoleField.buffer[0] != '/' ) {
			char	temp[MAX_EDIT_LINE-1];

			Q_strncpyz( temp, g_consoleField.buffer, sizeof( temp ) );
			Com_sprintf( g_consoleField.buffer, sizeof( g_consoleField.buffer ), "\\%s", temp );
			g_consoleField.cursor++;
		}

		Com_Printf ( "]%s\n", g_consoleField.buffer );

		// leading slash is an explicit command
		if ( g_consoleField.buffer[0] == '\\' || g_consoleField.buffer[0] == '/' ) {
			Cbuf_AddText( g_consoleField.buffer+1 );	// valid command
			Cbuf_AddText ("\n");
		} else {
			// other text will be chat messages
			if ( !g_consoleField.buffer[0] ) {
				return;	// empty lines just scroll the console without adding to history
			} else {
				if ( con_autochat->integer ) {
					Cbuf_AddText ("cmd say ");
				}

				Cbuf_AddText( g_consoleField.buffer );
				Cbuf_AddText ("\n");
			}
		}

		// copy line to history buffer
		historyEditLines[nextHistoryLine % COMMAND_HISTORY] = g_consoleField;
		nextHistoryLine++;
		historyLine = nextHistoryLine;

		Field_Clear( &g_consoleField );

		g_consoleField.widthInChars = g_console_field_width;

		CL_SaveConsoleHistory( );

		if ( clc.state == CA_DISCONNECTED ) {
			SCR_UpdateScreen ();	// force an update, because the command
		}							// may take some time
		return;
	}

	// command completion

	if (key == K_TAB) {
		Field_AutoComplete(&g_consoleField);
		return;
	}

	// command history (ctrl-p ctrl-n for unix style)

	if ( (key == K_MWHEELUP && keys[K_SHIFT].down) || ( key == K_UPARROW ) || ( key == K_KP_UPARROW ) ||
		 ( ( tolower(key) == 'p' ) && keys[K_CTRL].down ) ) {
		if ( nextHistoryLine - historyLine < COMMAND_HISTORY 
			&& historyLine > 0 ) {
			historyLine--;
		}
		g_consoleField = historyEditLines[ historyLine % COMMAND_HISTORY ];
		return;
	}

	if ( (key == K_MWHEELDOWN && keys[K_SHIFT].down) || ( key == K_DOWNARROW ) || ( key == K_KP_DOWNARROW ) ||
		 ( ( tolower(key) == 'n' ) && keys[K_CTRL].down ) ) {
		historyLine++;
		if (historyLine >= nextHistoryLine) {
			historyLine = nextHistoryLine;
			Field_Clear( &g_consoleField );
			g_consoleField.widthInChars = g_console_field_width;
			return;
		}
		g_consoleField = historyEditLines[ historyLine % COMMAND_HISTORY ];
		return;
	}

	// console scrolling
	if ( key == K_PGUP ) {
		Con_PageUp();
		return;
	}

	if ( key == K_PGDN) {
		Con_PageDown();
		return;
	}

	if ( key == K_MWHEELUP) {	//----(SA)	added some mousewheel functionality to the console
		Con_PageUp();
		if(keys[K_CTRL].down) {	// hold <ctrl> to accelerate scrolling
			Con_PageUp();
			Con_PageUp();
		}
		return;
	}

	if ( key == K_MWHEELDOWN) {	//----(SA)	added some mousewheel functionality to the console
		Con_PageDown();
		if(keys[K_CTRL].down) {	// hold <ctrl> to accelerate scrolling
			Con_PageDown();
			Con_PageDown();
		}
		return;
	}

	// ctrl-home = top of console
	if ( key == K_HOME && keys[K_CTRL].down ) {
		Con_Top();
		return;
	}

	// ctrl-end = bottom of console
	if ( key == K_END && keys[K_CTRL].down ) {
		Con_Bottom();
		return;
	}

	// pass to the normal editline routine
	Field_KeyDownEvent( &g_consoleField, key );
}

//============================================================================


/*
================
Message_Key

In game talk message
================
*/
void Message_Key( int key ) {

	char	buffer[MAX_STRING_CHARS];


	if (key == K_ESCAPE) {
		Key_SetCatcher( Key_GetCatcher( ) & ~KEYCATCH_MESSAGE );
		Field_Clear( &chatField );
		return;
	}

	if ( key == K_ENTER || key == K_KP_ENTER )
	{
		if ( chatField.buffer[0] && clc.state == CA_ACTIVE ) {
			if (chat_playerNum != -1 )

				Com_sprintf( buffer, sizeof( buffer ), "tell %i \"%s\"\n", chat_playerNum, chatField.buffer );

			else if (chat_team)

				Com_sprintf( buffer, sizeof( buffer ), "say_team \"%s\"\n", chatField.buffer );
			else
				Com_sprintf( buffer, sizeof( buffer ), "say \"%s\"\n", chatField.buffer );



			CL_AddReliableCommand(buffer, qfalse);
		}
		Key_SetCatcher( Key_GetCatcher( ) & ~KEYCATCH_MESSAGE );
		Field_Clear( &chatField );
		return;
	}

	Field_KeyDownEvent( &chatField, key );
}

//============================================================================


qboolean Key_GetOverstrikeMode( void ) {
	return key_overstrikeMode;
}


void Key_SetOverstrikeMode( qboolean state ) {
	key_overstrikeMode = state;
}


/*
===================
Key_IsDown
===================
*/
qboolean Key_IsDown( int keynum ) {
	if ( keynum < 0 || keynum >= MAX_KEYS ) {
		return qfalse;
	}

	return keys[keynum].down;
}

/*
===================
Key_StringToKeynum

Returns a key number to be used to index keys[] by looking at
the given string.  Single ascii characters return themselves, while
the K_* names are matched up.

0x11 will be interpreted as raw hex, which will allow new controlers

to be configured even if they don't have defined names.
===================
*/
int Key_StringToKeynum( char *str ) {
	keyname_t	*kn;
	int			n;
	
	if ( !str || !str[0] ) {
		return -1;
	}
	if ( !str[1] ) {
		return tolower( str[0] );
	}

	// check for hex code
	n = Com_HexStrToInt( str );
	if ( n >= 0 && n < MAX_KEYS ) {
		return n;
	}

	// scan for a text match
	for ( kn=keynames ; kn->name ; kn++ ) {
		if ( !Q_stricmp( str,kn->name ) )
			return kn->keynum;
	}

	return -1;
}

/*
===================
Key_KeynumToString

Returns a string (either a single ascii char, a K_* name, or a 0x11 hex string) for the
given keynum.
===================
*/
char *Key_KeynumToString( int keynum ) {
	keyname_t	*kn;	
	static	char	tinystr[5];
	int			i, j;

	if ( keynum == -1 ) {
		return "<KEY NOT FOUND>";
	}

	if ( keynum < 0 || keynum >= MAX_KEYS ) {
		return "<OUT OF RANGE>";
	}

	// check for printable ascii (don't use quote)
	if ( keynum > 32 && keynum < 127 && keynum != '"' && keynum != ';' ) {
		tinystr[0] = keynum;
		tinystr[1] = 0;
		return tinystr;
	}

	// check for a key string
	for ( kn=keynames ; kn->name ; kn++ ) {
		if (keynum == kn->keynum) {
			return kn->name;
		}
	}

	// make a hex string
	i = keynum >> 4;
	j = keynum & 15;

	tinystr[0] = '0';
	tinystr[1] = 'x';
	tinystr[2] = i > 9 ? i - 10 + 'a' : i + '0';
	tinystr[3] = j > 9 ? j - 10 + 'a' : j + '0';
	tinystr[4] = 0;

	return tinystr;
}

/*
=============================================================================

GAME CONTROLLERS

OpenFAKK2: a controller's buttons, stick directions and triggers are the
PAD0_ keys (sdl_input.c). The controls menu shows them by the names below;
"bind" and the config keep the PAD0_ names.

=============================================================================
*/

static const keyname_t padDisplayNames[] = {
	{ "Pad A", K_PAD0_A },
	{ "Pad B", K_PAD0_B },
	{ "Pad X", K_PAD0_X },
	{ "Pad Y", K_PAD0_Y },
	{ "Pad Back", K_PAD0_BACK },
	{ "Pad Guide", K_PAD0_GUIDE },
	{ "Pad Start", K_PAD0_START },
	{ "Pad L3", K_PAD0_LEFTSTICK_CLICK },
	{ "Pad R3", K_PAD0_RIGHTSTICK_CLICK },
	{ "Pad LB", K_PAD0_LEFTSHOULDER },
	{ "Pad RB", K_PAD0_RIGHTSHOULDER },
	{ "D-Pad Up", K_PAD0_DPAD_UP },
	{ "D-Pad Down", K_PAD0_DPAD_DOWN },
	{ "D-Pad Left", K_PAD0_DPAD_LEFT },
	{ "D-Pad Right", K_PAD0_DPAD_RIGHT },
	{ "LS Left", K_PAD0_LEFTSTICK_LEFT },
	{ "LS Right", K_PAD0_LEFTSTICK_RIGHT },
	{ "LS Up", K_PAD0_LEFTSTICK_UP },
	{ "LS Down", K_PAD0_LEFTSTICK_DOWN },
	{ "RS Left", K_PAD0_RIGHTSTICK_LEFT },
	{ "RS Right", K_PAD0_RIGHTSTICK_RIGHT },
	{ "RS Up", K_PAD0_RIGHTSTICK_UP },
	{ "RS Down", K_PAD0_RIGHTSTICK_DOWN },
	{ "Pad LT", K_PAD0_LEFTTRIGGER },
	{ "Pad RT", K_PAD0_RIGHTTRIGGER },
	{ "Pad Share", K_PAD0_MISC1 },
	{ "Paddle 1", K_PAD0_PADDLE1 },
	{ "Paddle 2", K_PAD0_PADDLE2 },
	{ "Paddle 3", K_PAD0_PADDLE3 },
	{ "Paddle 4", K_PAD0_PADDLE4 },
	{ "Touchpad", K_PAD0_TOUCHPAD },
	{ NULL, 0 }
};

qboolean Key_IsPadKey( int keynum ) {
	return keynum >= K_PAD0_A && keynum <= K_PAD0_TOUCHPAD;
}

/*
===================
Key_KeynumToDisplayString

A key's name for the controls menu: a controller's by what is printed on it.
===================
*/
const char *Key_KeynumToDisplayString( int keynum ) {
	const keyname_t	*kn;

	if ( Key_IsPadKey( keynum ) ) {
		for ( kn = padDisplayNames ; kn->name ; kn++ ) {
			if ( kn->keynum == keynum ) {
				return kn->name;
			}
		}
	}
	return Key_KeynumToString( keynum );
}

// what a controller is bound to until the player binds it: the triggers are
// FAKK2's two hands, the shoulders step through the inventory and the D-pad
// jumps to four of its slots; Start is the escape key (CL_KeyEvent)
static const struct {
	int			keynum;
	const char	*binding;
} padDefaultBindings[] = {
	{ K_PAD0_LEFTSTICK_UP,		"+forward" },
	{ K_PAD0_LEFTSTICK_DOWN,	"+back" },
	{ K_PAD0_LEFTSTICK_LEFT,	"+moveleft" },
	{ K_PAD0_LEFTSTICK_RIGHT,	"+moveright" },
	{ K_PAD0_RIGHTSTICK_LEFT,	"+left" },
	{ K_PAD0_RIGHTSTICK_RIGHT,	"+right" },
	{ K_PAD0_RIGHTSTICK_UP,		"+lookup" },
	{ K_PAD0_RIGHTSTICK_DOWN,	"+lookdown" },
	{ K_PAD0_LEFTTRIGGER,		"+attackleft" },
	{ K_PAD0_RIGHTTRIGGER,		"+attackright" },
	{ K_PAD0_A,					"+moveup" },
	{ K_PAD0_B,					"+movedown" },
	{ K_PAD0_X,					"+use" },
	{ K_PAD0_Y,					"inventory" },
	{ K_PAD0_LEFTSHOULDER,		"previnv" },
	{ K_PAD0_RIGHTSHOULDER,		"nextinv" },
	{ K_PAD0_DPAD_UP,			"warpinv swords" },
	{ K_PAD0_DPAD_RIGHT,		"warpinv guns" },
	{ K_PAD0_DPAD_DOWN,			"warpinv health" },
	{ K_PAD0_DPAD_LEFT,			"warpinv defense" },
	{ K_PAD0_LEFTSTICK_CLICK,	"+speed" },
	{ K_PAD0_RIGHTSTICK_CLICK,	"+cameralook" },
	{ K_PAD0_BACK,				"holster" },
};

/*
===================
Key_PadDefaultBindings

Called as a controller starts being used: when none of its keys is bound
(the first time, or after unbindall), it gets the bindings above. The
keyboard's and the mouse's are left alone.
===================
*/
void Key_PadDefaultBindings( void ) {
	int		k;

	for ( k = K_PAD0_A ; k <= K_PAD0_TOUCHPAD ; k++ ) {
		if ( keys[k].binding && keys[k].binding[0] ) {
			return;
		}
	}
	for ( k = 0 ; k < ARRAY_LEN( padDefaultBindings ) ; k++ ) {
		Key_SetBinding( padDefaultBindings[k].keynum, padDefaultBindings[k].binding );
	}
	Com_Printf( "The controller has no bindings: it gets the default ones\n" );
}

/*
===================
Key_PadMenuKey

While a menu is up the controller works it as the mouse and keyboard do:
A clicks, B goes back, the D-pad is the arrows (the left stick moves the
cursor, sdl_input.c). Start is escape everywhere, so it brings the main
menu up and takes it down. A bind list cell waiting for its key gets the
controller's own.
===================
*/
static int Key_PadMenuKey( int key ) {
	if ( key == K_PAD0_START ) {
		return K_ESCAPE;
	}
	if ( Key_GetCatcher( ) & KEYCATCH_CONSOLE ) {
		return key;
	}
	if ( !URC_HasMenus() && !( Key_GetCatcher( ) & KEYCATCH_UI ) ) {
		return key;
	}
	if ( URC_BindWaiting() ) {
		return key;
	}
	switch ( key ) {
	case K_PAD0_A:			return K_MOUSE1;
	case K_PAD0_B:			return K_ESCAPE;
	case K_PAD0_DPAD_UP:	return K_UPARROW;
	case K_PAD0_DPAD_DOWN:	return K_DOWNARROW;
	case K_PAD0_DPAD_LEFT:	return K_LEFTARROW;
	case K_PAD0_DPAD_RIGHT:	return K_RIGHTARROW;
	default:				return key;
	}
}


/*
===================
Key_SetBinding
===================
*/
void Key_SetBinding( int keynum, const char *binding ) {
	if ( keynum < 0 || keynum >= MAX_KEYS ) {
		return;
	}

	// free old bindings
	if ( keys[ keynum ].binding ) {
		Z_Free( keys[ keynum ].binding );
	}
		
	// allocate memory for new binding
	keys[keynum].binding = CopyString( binding );

	// consider this like modifying an archived cvar, so the
	// file write will be triggered at the next opportunity
	cvar_modifiedFlags |= CVAR_ARCHIVE;
}


/*
===================
Key_GetBinding
===================
*/
char *Key_GetBinding( int keynum ) {
	if ( keynum < 0 || keynum >= MAX_KEYS ) {
		return "";
	}

	return keys[ keynum ].binding;
}

/* 
===================
Key_GetKey
===================
*/

int Key_GetKey(const char *binding) {
  int i;

  if (binding) {
  	for (i=0 ; i < MAX_KEYS ; i++) {
      if (keys[i].binding && Q_stricmp(binding, keys[i].binding) == 0) {
        return i;
      }
    }
  }
  return -1;
}

/*
===================
Key_Unbind_f
===================
*/
void Key_Unbind_f (void)
{
	int		b;

	if (Cmd_Argc() != 2)
	{
		Com_Printf ("unbind <key> : remove commands from a key\n");
		return;
	}
	
	b = Key_StringToKeynum (Cmd_Argv(1));
	if (b==-1)
	{
		Com_Printf ("\"%s\" isn't a valid key\n", Cmd_Argv(1));
		return;
	}

	Key_SetBinding (b, "");
}

/*
===================
Key_Unbindall_f
===================
*/
void Key_Unbindall_f (void)
{
	int		i;
	
	for (i=0 ; i < MAX_KEYS; i++)
		if (keys[i].binding)
			Key_SetBinding (i, "");
}


/*
===================
Key_Bind_f
===================
*/
void Key_Bind_f (void)
{
	int			i, c, b;
	char		cmd[1024];
	
	c = Cmd_Argc();

	if (c < 2)
	{
		Com_Printf ("bind <key> [command] : attach a command to a key\n");
		return;
	}
	b = Key_StringToKeynum (Cmd_Argv(1));
	if (b==-1)
	{
		Com_Printf ("\"%s\" isn't a valid key\n", Cmd_Argv(1));
		return;
	}

	if (c == 2)
	{
		if (keys[b].binding && keys[b].binding[0])
			Com_Printf ("\"%s\" = \"%s\"\n", Key_KeynumToString(b), keys[b].binding );
		else
			Com_Printf ("\"%s\" is not bound\n", Key_KeynumToString(b) );
		return;
	}
	
// copy the rest of the command line
	cmd[0] = 0;		// start out with a null string
	for (i=2 ; i< c ; i++)
	{
		strcat (cmd, Cmd_Argv(i));
		if (i != (c-1))
			strcat (cmd, " ");
	}

	Key_SetBinding (b, cmd);
}

/*
=============================================================================

FAKK2's ALT and CTRL bindings (fakk2.exe 0x41f4f0, 0x41f640)

A key has two more bindings, used when it is pressed with ALT held and
cl_altbindings is on, or with CTRL held and cl_ctrlbindings is on
(CL_KeyEvent 0x41f9c0). default.cfg gives ALT+n "weapgroup<n>" and CTRL+n
"saveweaps <n>": the player's weapon groups. unbindall leaves them alone.

=============================================================================
*/

static char		*keyAltBindings[MAX_KEYS];
static char		*keyCtrlBindings[MAX_KEYS];
static qboolean	keyAltPressed[MAX_KEYS];		// pressed while ALT was held
static qboolean	keyCtrlPressed[MAX_KEYS];
static qboolean	keyAltHeld, keyCtrlHeld;
static cvar_t	*cl_altbindings;
static cvar_t	*cl_ctrlbindings;

static void Key_SetModBinding( char **table, int keynum, const char *binding ) {
	if ( keynum < 0 || keynum >= MAX_KEYS ) {
		return;
	}
	if ( table[keynum] ) {
		Z_Free( table[keynum] );
	}
	table[keynum] = CopyString( binding );
	cvar_modifiedFlags |= CVAR_ARCHIVE;
}

// altbind/ctrlbind <key> [command]: as bind, but the key is named as given
static void Key_ModBind( char **table, const char *usage ) {
	char	cmd[1024];
	int		i, c, b;

	c = Cmd_Argc();
	if ( c < 2 ) {
		Com_Printf( "%s", usage );
		return;
	}
	b = Key_StringToKeynum( Cmd_Argv( 1 ) );
	if ( b == -1 ) {
		Com_Printf( "\"%s\" isn't a valid key\n", Cmd_Argv( 1 ) );
		return;
	}
	if ( c == 2 ) {
		if ( table[b] ) {
			Com_Printf( "\"%s\" = \"%s\"\n", Cmd_Argv( 1 ), table[b] );
		} else {
			Com_Printf( "\"%s\" is not bound\n", Cmd_Argv( 1 ) );
		}
		return;
	}

	cmd[0] = 0;
	for ( i = 2 ; i < c ; i++ ) {
		Q_strcat( cmd, sizeof( cmd ), Cmd_Argv( i ) );
		if ( i != c - 1 ) {
			Q_strcat( cmd, sizeof( cmd ), " " );
		}
	}
	Key_SetModBinding( table, b, cmd );
}

static void Key_ModUnbind( char **table, const char *usage ) {
	int		b;

	if ( Cmd_Argc() != 2 ) {
		Com_Printf( "%s", usage );
		return;
	}
	b = Key_StringToKeynum( Cmd_Argv( 1 ) );
	if ( b == -1 ) {
		Com_Printf( "\"%s\" isn't a valid key\n", Cmd_Argv( 1 ) );
		return;
	}
	Key_SetModBinding( table, b, "" );
}

static void Key_ModBindlist( char **table ) {
	int		i;

	for ( i = 0 ; i < MAX_KEYS ; i++ ) {
		if ( table[i] && table[i][0] ) {
			Com_Printf( "%s \"%s\"\n", Key_KeynumToString( i ), table[i] );
		}
	}
}

static void Key_AltBind_f( void ) {
	Key_ModBind( keyAltBindings, "altbind <key> [command] : attach a command to a key\n" );
}

static void Key_CtrlBind_f( void ) {
	Key_ModBind( keyCtrlBindings, "ctrlbind <key> [command] : attach a command to a key\n" );
}

static void Key_AltUnbind_f( void ) {
	Key_ModUnbind( keyAltBindings, "unaltbind <key> : remove commands from a key\n" );
}

static void Key_CtrlUnbind_f( void ) {
	Key_ModUnbind( keyCtrlBindings, "unctrlbind <key> : remove commands from a key\n" );
}

static void Key_AltBindlist_f( void ) {
	Key_ModBindlist( keyAltBindings );
}

static void Key_CtrlBindlist_f( void ) {
	Key_ModBindlist( keyCtrlBindings );
}

/*
============
Key_WriteBindings

Writes lines containing "bind key value", and FAKK2's "altbind" and "ctrlbind"
after each key's (0x41f790)
============
*/
void Key_WriteBindings( fileHandle_t f ) {
	int		i;

	FS_Printf (f, "unbindall\n" );

	for (i=0 ; i<MAX_KEYS ; i++) {
		if (keys[i].binding && keys[i].binding[0] ) {
			FS_Printf (f, "bind %s \"%s\"\n", Key_KeynumToString(i), keys[i].binding);
		}
		if ( keyAltBindings[i] && keyAltBindings[i][0] ) {
			FS_Printf( f, "altbind %s \"%s\"\n", Key_KeynumToString( i ), keyAltBindings[i] );
		}
		if ( keyCtrlBindings[i] && keyCtrlBindings[i][0] ) {
			FS_Printf( f, "ctrlbind %s \"%s\"\n", Key_KeynumToString( i ), keyCtrlBindings[i] );
		}
	}
}


/*
============
Key_Bindlist_f

============
*/
void Key_Bindlist_f( void ) {
	int		i;

	for ( i = 0 ; i < MAX_KEYS ; i++ ) {
		if ( keys[i].binding && keys[i].binding[0] ) {
			Com_Printf( "%s \"%s\"\n", Key_KeynumToString(i), keys[i].binding );
		}
	}
}

/*
============
Key_KeynameCompletion
============
*/
void Key_KeynameCompletion( void(*callback)(const char *s) ) {
	int		i;

	for( i = 0; keynames[ i ].name != NULL; i++ )
		callback( keynames[ i ].name );
}

/*
====================
Key_CompleteUnbind
====================
*/
static void Key_CompleteUnbind( char *args, int argNum )
{
	if( argNum == 2 )
	{
		// Skip "unbind "
		char *p = Com_SkipTokens( args, 1, " " );

		if( p > args )
			Field_CompleteKeyname( );
	}
}

/*
====================
Key_CompleteBind
====================
*/
static void Key_CompleteBind( char *args, int argNum )
{
	char *p;

	if( argNum == 2 )
	{
		// Skip "bind "
		p = Com_SkipTokens( args, 1, " " );

		if( p > args )
			Field_CompleteKeyname( );
	}
	else if( argNum >= 3 )
	{
		// Skip "bind <key> "
		p = Com_SkipTokens( args, 2, " " );

		if( p > args )
			Field_CompleteCommand( p, qtrue, qtrue );
	}
}

/*
===================
CL_InitKeyCommands
===================
*/
void CL_InitKeyCommands( void ) {
	// register our functions
	Cmd_AddCommand ("bind",Key_Bind_f);
	Cmd_SetCommandCompletionFunc( "bind", Key_CompleteBind );
	Cmd_AddCommand ("unbind",Key_Unbind_f);
	Cmd_SetCommandCompletionFunc( "unbind", Key_CompleteUnbind );
	Cmd_AddCommand ("unbindall",Key_Unbindall_f);
	Cmd_AddCommand ("bindlist",Key_Bindlist_f);

	// FAKK2: the ALT and CTRL bindings (0x41f900, the switches 0x42269c)
	Cmd_AddCommand( "altbind", Key_AltBind_f );
	Cmd_SetCommandCompletionFunc( "altbind", Key_CompleteBind );
	Cmd_AddCommand( "unaltbind", Key_AltUnbind_f );
	Cmd_SetCommandCompletionFunc( "unaltbind", Key_CompleteUnbind );
	Cmd_AddCommand( "ctrlbind", Key_CtrlBind_f );
	Cmd_SetCommandCompletionFunc( "ctrlbind", Key_CompleteBind );
	Cmd_AddCommand( "unctrlbind", Key_CtrlUnbind_f );
	Cmd_SetCommandCompletionFunc( "unctrlbind", Key_CompleteUnbind );
	Cmd_AddCommand( "altbindlist", Key_AltBindlist_f );
	Cmd_AddCommand( "ctrlbindlist", Key_CtrlBindlist_f );
	cl_altbindings = Cvar_Get( "cl_altbindings", "0", CVAR_ARCHIVE );
	cl_ctrlbindings = Cvar_Get( "cl_ctrlbindings", "0", CVAR_ARCHIVE );
}

/*
===================
CL_BindUICommand

Returns qtrue if bind command should be executed while user interface is shown
===================
*/
static qboolean CL_BindUICommand( const char *cmd ) {
	if ( Key_GetCatcher( ) & KEYCATCH_CONSOLE )
		return qfalse;

	if ( !Q_stricmp( cmd, "toggleconsole" ) )
		return qtrue;
	if ( !Q_stricmp( cmd, "togglemenu" ) )
		return qtrue;

	return qfalse;
}

/*
===================
CL_ParseBinding

Execute the commands in the bind string
===================
*/
static void CL_ParseBindingText( const char *binding, int key, qboolean down, unsigned time );

void CL_ParseBinding( int key, qboolean down, unsigned time )
{
	// FAKK2 (CL_KeyEvent 0x41f9c0): a key pressed with ALT or CTRL held runs
	// that binding instead, when it has one; on release, both its button
	// commands and the plain binding's are let go
	if ( down ) {
		if ( keyAltPressed[key] && keyAltBindings[key] ) {
			CL_ParseBindingText( keyAltBindings[key], key, down, time );
			return;
		}
		if ( keyCtrlPressed[key] && keyCtrlBindings[key] ) {
			CL_ParseBindingText( keyCtrlBindings[key], key, down, time );
			return;
		}
	} else {
		if ( keyAltPressed[key] ) {
			keyAltPressed[key] = qfalse;
			if ( keyAltBindings[key] ) {
				CL_ParseBindingText( keyAltBindings[key], key, down, time );
			}
		}
		if ( keyCtrlPressed[key] ) {
			keyCtrlPressed[key] = qfalse;
			if ( keyCtrlBindings[key] ) {
				CL_ParseBindingText( keyCtrlBindings[key], key, down, time );
			}
		}
	}
	CL_ParseBindingText( keys[key].binding, key, down, time );
}

static void CL_ParseBindingText( const char *binding, int key, qboolean down, unsigned time )
{
	char buf[ MAX_STRING_CHARS ], *p = buf, *end;
	qboolean allCommands, allowUpCmds;

	if( clc.state == CA_DISCONNECTED && Key_GetCatcher( ) == 0 && !CL_Viewer_Active() )
		return;
	if( !binding || !binding[0] )
		return;
	Q_strncpyz( buf, binding, sizeof( buf ) );

	// run all bind commands if console, ui, etc aren't reading keys
	allCommands = ( Key_GetCatcher( ) == 0 );

	// allow button up commands if in game even if key catcher is set
	allowUpCmds = ( clc.state != CA_DISCONNECTED || CL_Viewer_Active() );

	while( 1 )
	{
		while( isspace( *p ) )
			p++;
		end = strchr( p, ';' );
		if( end )
			*end = '\0';
		if( *p == '+' )
		{
			// button commands add keynum and time as parameters
			// so that multiple sources can be discriminated and
			// subframe corrected
			if ( allCommands || ( allowUpCmds && !down ) ) {
				char cmd[1024];
				Com_sprintf( cmd, sizeof( cmd ), "%c%s %d %d\n",
					( down ) ? '+' : '-', p + 1, key, time );
				Cbuf_AddText( cmd );
			}
		}
		else if( down )
		{
			// normal commands only execute on key press
			if ( allCommands || CL_BindUICommand( p ) ) {
				Cbuf_AddText( p );
				Cbuf_AddText( "\n" );
			}
		}
		if( !end )
			break;
		p = end + 1;
	}
}

/*
===================
CL_KeyDownEvent

Called by CL_KeyEvent to handle a keypress
===================
*/
void CL_KeyDownEvent( int key, unsigned time )
{
	keys[key].down = qtrue;
	keys[key].repeats++;
	if( keys[key].repeats == 1 )
		anykeydown++;

	if( keys[K_ALT].down && key == K_ENTER )
	{
		// don't repeat fullscreen toggle when keys are held down
		if ( keys[K_ENTER].repeats > 1 ) {
			return;
		}

		Cvar_SetValue( "r_fullscreen",
			!Cvar_VariableIntegerValue( "r_fullscreen" ) );
		return;
	}

	// console key is hardcoded, so the user can never unbind it
	if( key == K_CONSOLE || ( keys[K_SHIFT].down && key == K_ESCAPE ) )
	{
		Con_ToggleConsole_f ();
		Key_ClearStates ();
		return;
	}

	// FAKK2 (0x41fad1): with +togglemouse's cursor up the mouse buttons
	// skip their bindings; a menu or a floating window up still takes them
	if ( cl_mouseShown && key >= K_MOUSE1 && key <= K_MOUSE5 ) {
		if ( ( Key_GetCatcher( ) & KEYCATCH_UI || URC_HasWindows() ) && !uivm ) {
			URC_KeyEvent( key, qtrue );
		}
		return;
	}


	// keys can still be used for bound actions
	if ( ( key < 128 || key == K_MOUSE1 ) &&
		( clc.demoplaying || clc.state == CA_CINEMATIC ) && Key_GetCatcher( ) == 0 ) {

		if (Cvar_VariableValue ("com_cameraMode") == 0) {
			Cvar_Set ("nextdemo","");
			key = K_ESCAPE;
		}
	}

	// FAKK2: while the inventory bar or screen is up, the attack keys equip
	// the item it shows instead of firing, escape closes the screen (cl_inv.c)
	if ( !( Key_GetCatcher() & ( KEYCATCH_CONSOLE | KEYCATCH_UI ) ) && INV_KeyEvent( key ) ) {
		return;
	}

	// escape is always handled special
	if ( key == K_ESCAPE ) {
		if ( Key_GetCatcher( ) & KEYCATCH_MESSAGE ) {
			// clear message mode
			Message_Key( key );
			return;
		}

		// escape always gets out of CGAME stuff
		if (Key_GetCatcher( ) & KEYCATCH_CGAME) {
			Key_SetCatcher( Key_GetCatcher( ) & ~KEYCATCH_CGAME );
			return;
		}

		if ( !uivm ) {
			// FAKK2's own menus (cl_urc.c): escape closes the one up, or
			// brings the main menu up over the game or the intro, as the
			// retail client's binding (togglemenu) does
			if ( URC_HasMenus() ) {
				URC_KeyEvent( key, qtrue );
			} else if ( ( clc.state == CA_ACTIVE || clc.state == CA_DISCONNECTED ) && !clc.demoplaying ) {
				URC_PushMenu( "main" );
			}
			return;
		}

		if ( !( Key_GetCatcher( ) & KEYCATCH_UI ) ) {
			if ( clc.state == CA_ACTIVE && !clc.demoplaying ) {
				VM_Call( uivm, UI_SET_ACTIVE_MENU, UIMENU_INGAME );
			}
			else if ( clc.state != CA_DISCONNECTED ) {
				CL_Disconnect_f();
				S_StopAllSounds();
				VM_Call( uivm, UI_SET_ACTIVE_MENU, UIMENU_MAIN );
			}
			return;
		}

		VM_Call( uivm, UI_KEY_EVENT, key, qtrue );
		return;
	}

	// send the bound action
	CL_ParseBinding( key, qtrue, time );

	// distribute the key down event to the appropriate handler
	if ( Key_GetCatcher( ) & KEYCATCH_CONSOLE ) {
		Console_Key( key );
	} else if ( Key_GetCatcher( ) & KEYCATCH_UI ) {
		if ( uivm ) {
			VM_Call( uivm, UI_KEY_EVENT, key, qtrue );
		} else {
			URC_KeyEvent( key, qtrue );
		}
	} else if ( Key_GetCatcher( ) & KEYCATCH_CGAME ) {
		// FAKK2's client game module takes no key events
	} else if ( Key_GetCatcher( ) & KEYCATCH_MESSAGE ) {
		Message_Key( key );
	} else if ( clc.state == CA_DISCONNECTED && !CL_Viewer_Active() ) {
		Console_Key( key );
	}
}

/*
===================
CL_KeyUpEvent

Called by CL_KeyEvent to handle a keyrelease
===================
*/
void CL_KeyUpEvent( int key, unsigned time )
{
	keys[key].repeats = 0;
	keys[key].down = qfalse;
	anykeydown--;

	if (anykeydown < 0) {
		anykeydown = 0;
	}

	// don't process key-up events for the console key
	if ( key == K_CONSOLE || ( key == K_ESCAPE && keys[K_SHIFT].down ) )
		return;

	// FAKK2 (0x41fad1): with +togglemouse's cursor up the mouse buttons
	// skip their bindings
	if ( cl_mouseShown && key >= K_MOUSE1 && key <= K_MOUSE5 ) {
		if ( ( Key_GetCatcher( ) & KEYCATCH_UI || URC_HasWindows() ) && !uivm ) {
			URC_KeyEvent( key, qfalse );
		}
		return;
	}

	//
	// key up events only perform actions if the game key binding is
	// a button command (leading + sign).  These will be processed even in
	// console mode and menu mode, to keep the character from continuing
	// an action started before a mode switch.
	//
	CL_ParseBinding( key, qfalse, time );

	if ( Key_GetCatcher( ) & KEYCATCH_UI && !uivm ) {
		URC_KeyEvent( key, qfalse );
	}
	if ( Key_GetCatcher( ) & KEYCATCH_UI && uivm ) {
		VM_Call( uivm, UI_KEY_EVENT, key, qfalse );
	}
}

/*
===================
CL_KeyEvent

Called by the system for both key up and key down events
===================
*/
void CL_KeyEvent (int key, qboolean down, unsigned time) {
	// OpenFAKK2: a controller key is let go as what it went down as
	static int	padKeyAs[MAX_KEYS];

	if ( Key_IsPadKey( key ) ) {
		if ( down ) {
			padKeyAs[key] = Key_PadMenuKey( key );
			key = padKeyAs[key];
		} else if ( padKeyAs[key] ) {
			int		as = padKeyAs[key];

			padKeyAs[key] = 0;
			key = as;
		}
	}

	// FAKK2 (0x41fa1f): ALT and CTRL are followed while their switches are on,
	// and a key pressed meanwhile is marked for that binding
	if ( key == K_ALT ) {
		if ( cl_altbindings && cl_altbindings->integer ) {
			keyAltHeld = down;
		}
	} else if ( key == K_CTRL ) {
		if ( cl_ctrlbindings && cl_ctrlbindings->integer ) {
			keyCtrlHeld = down;
		}
	}
	if ( down ) {
		if ( keyAltHeld && key != K_ALT ) {
			keyAltPressed[key] = qtrue;
		}
		if ( keyCtrlHeld && key != K_CTRL ) {
			keyCtrlPressed[key] = qtrue;
		}
	}

	if( down )
		CL_KeyDownEvent( key, time );
	else
		CL_KeyUpEvent( key, time );
}

/*
===================
CL_CharEvent

Normal keyboard characters, already shifted / capslocked / etc
===================
*/
void CL_CharEvent( int key ) {
	// delete is not a printable character and is
	// otherwise handled by Field_KeyDownEvent
	if ( key == 127 ) {
		return;
	}

	// distribute the key down event to the appropriate handler
	if ( Key_GetCatcher( ) & KEYCATCH_CONSOLE )
	{
		Field_CharEvent( &g_consoleField, key );
	}
	else if ( Key_GetCatcher( ) & KEYCATCH_UI )
	{
		if ( uivm )
			VM_Call( uivm, UI_KEY_EVENT, key | K_CHAR_FLAG, qtrue );
	}
	else if ( Key_GetCatcher( ) & KEYCATCH_MESSAGE ) 
	{
		Field_CharEvent( &chatField, key );
	}
	else if ( clc.state == CA_DISCONNECTED )
	{
		Field_CharEvent( &g_consoleField, key );
	}
}


/*
===================
Key_ClearStates
===================
*/
void Key_ClearStates (void)
{
	int		i;

	anykeydown = 0;

	for ( i=0 ; i < MAX_KEYS ; i++ ) {
		if ( keys[i].down ) {
			CL_KeyEvent( i, qfalse, 0 );

		}
		keys[i].down = 0;
		keys[i].repeats = 0;
	}
}

static int keyCatchers = 0;

/*
====================
Key_GetCatcher
====================
*/
int Key_GetCatcher( void ) {
	return keyCatchers;
}

/*
====================
Key_SetCatcher
====================
*/
void Key_SetCatcher( int catcher ) {
	// If the catcher state is changing, clear all key states
	if( catcher != keyCatchers )
		Key_ClearStates( );

	keyCatchers = catcher;
}

// This must not exceed MAX_CMD_LINE
#define			MAX_CONSOLE_SAVE_BUFFER	1024
#define			CONSOLE_HISTORY_FILE    "q3history"
static char	consoleSaveBuffer[ MAX_CONSOLE_SAVE_BUFFER ];
static int	consoleSaveBufferSize = 0;

/*
================
CL_LoadConsoleHistory

Load the console history from CONSOLE_HISTORY_FILE
================
*/
void CL_LoadConsoleHistory( void )
{
	char					*token, *text_p;
	int						i, numChars, numLines = 0;
	fileHandle_t	f;

	// ioquake3's, kept as a convenience; retail has no history file. None
	// yet (a first start) is no fault, so nothing is printed for it
	consoleSaveBufferSize = FS_FOpenFileRead( CONSOLE_HISTORY_FILE, &f, qfalse );
	if( !f )
		return;

	if( consoleSaveBufferSize < MAX_CONSOLE_SAVE_BUFFER &&
			FS_Read( consoleSaveBuffer, consoleSaveBufferSize, f ) == consoleSaveBufferSize )
	{
		consoleSaveBuffer[consoleSaveBufferSize] = '\0';
		text_p = consoleSaveBuffer;

		for( i = COMMAND_HISTORY - 1; i >= 0; i-- )
		{
			if( !*( token = COM_Parse( &text_p ) ) )
				break;

			historyEditLines[ i ].cursor = atoi( token );

			if( !*( token = COM_Parse( &text_p ) ) )
				break;

			historyEditLines[ i ].scroll = atoi( token );

			if( !*( token = COM_Parse( &text_p ) ) )
				break;

			numChars = atoi( token );
			text_p++;
			if( numChars > ( strlen( consoleSaveBuffer ) -	( text_p - consoleSaveBuffer ) ) )
			{
				Com_DPrintf( S_COLOR_YELLOW "WARNING: probable corrupt history\n" );
				break;
			}
			Com_Memcpy( historyEditLines[ i ].buffer,
					text_p, numChars );
			historyEditLines[ i ].buffer[ numChars ] = '\0';
			text_p += numChars;

			numLines++;
		}

		memmove( &historyEditLines[ 0 ], &historyEditLines[ i + 1 ],
				numLines * sizeof( field_t ) );
		for( i = numLines; i < COMMAND_HISTORY; i++ )
			Field_Clear( &historyEditLines[ i ] );

		historyLine = nextHistoryLine = numLines;
	}
	else
		Com_Printf( "Couldn't read %s.\n", CONSOLE_HISTORY_FILE );

	FS_FCloseFile( f );
}

/*
================
CL_SaveConsoleHistory

Save the console history into CONSOLE_HISTORY_FILE
so that it persists across invocations of q3
================
*/
void CL_SaveConsoleHistory( void )
{
	int						i;
	int						lineLength, saveBufferLength, additionalLength;
	fileHandle_t	f;

	consoleSaveBuffer[ 0 ] = '\0';

	i = ( nextHistoryLine - 1 ) % COMMAND_HISTORY;
	do
	{
		if( historyEditLines[ i ].buffer[ 0 ] )
		{
			lineLength = strlen( historyEditLines[ i ].buffer );
			saveBufferLength = strlen( consoleSaveBuffer );

			//ICK
			additionalLength = lineLength + strlen( "999 999 999  " );

			if( saveBufferLength + additionalLength < MAX_CONSOLE_SAVE_BUFFER )
			{
				Q_strcat( consoleSaveBuffer, MAX_CONSOLE_SAVE_BUFFER,
						va( "%d %d %d %s ",
						historyEditLines[ i ].cursor,
						historyEditLines[ i ].scroll,
						lineLength,
						historyEditLines[ i ].buffer ) );
			}
			else
				break;
		}
		i = ( i - 1 + COMMAND_HISTORY ) % COMMAND_HISTORY;
	}
	while( i != ( nextHistoryLine - 1 ) % COMMAND_HISTORY );

	consoleSaveBufferSize = strlen( consoleSaveBuffer );

	f = FS_FOpenFileWrite_HomeState( CONSOLE_HISTORY_FILE );
	if( !f )
	{
		Com_Printf( "Couldn't write %s.\n", CONSOLE_HISTORY_FILE );
		return;
	}

	if( FS_Write( consoleSaveBuffer, consoleSaveBufferSize, f ) < consoleSaveBufferSize )
		Com_Printf( "Couldn't write %s.\n", CONSOLE_HISTORY_FILE );

	FS_FCloseFile( f );
}
