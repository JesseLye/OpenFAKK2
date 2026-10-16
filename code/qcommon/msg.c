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
#include "q_shared.h"
#include "qcommon.h"

/*
==============================================================================

			MESSAGE IO FUNCTIONS

Handles byte ordering and avoids alignment errors

FAKK2: a message's bits are the retail engine's (fakk2.exe MSG_WriteBits
0x439420, MSG_ReadBits 0x439520): packed least significant first, a byte at a
time, with no Huffman coding, in both directions. cursize and readcount count
the bytes begun and bit is the position within the last of them. Data and
strings start on a byte boundary, and moving to one passes a byte more than it
needs to (MSG_GetSpace 0x439370, reading 0x439350), on both sides, as retail
does. Out-of-band messages are written and read whole bytes at a time and
always sit on a boundary, so they come out the same.
==============================================================================
*/

int oldsize = 0;

void MSG_Init( msg_t *buf, byte *data, int length ) {
	Com_Memset (buf, 0, sizeof(*buf));
	buf->data = data;
	buf->maxsize = length;
}

void MSG_InitOOB( msg_t *buf, byte *data, int length ) {
	Com_Memset (buf, 0, sizeof(*buf));
	buf->data = data;
	buf->maxsize = length;
	buf->oob = qtrue;
}

void MSG_Clear( msg_t *buf ) {
	buf->cursize = 0;
	buf->overflowed = qfalse;
	buf->bit = 0;
}


void MSG_Bitstream( msg_t *buf ) {
	buf->oob = qfalse;
}

void MSG_BeginReading( msg_t *msg ) {
	msg->readcount = 0;
	msg->bit = 0;
	msg->oob = qfalse;
}

void MSG_BeginReadingOOB( msg_t *msg ) {
	msg->readcount = 0;
	msg->bit = 0;
	msg->oob = qtrue;
}

void MSG_Copy(msg_t *buf, byte *data, int length, msg_t *src)
{
	if (length<src->cursize) {
		Com_Error( ERR_DROP, "MSG_Copy: can't copy into a smaller msg_t buffer");
	}
	Com_Memcpy(buf, src, sizeof(msg_t));
	buf->data = data;
	Com_Memcpy(buf->data, src->data, src->cursize);
}

/*
=============================================================================

bit functions
  
=============================================================================
*/

// negative bit values include signs
void MSG_WriteBits( msg_t *msg, int value, int bits ) {
	int				put;
	unsigned int	v;

	oldsize += bits;

	if ( msg->overflowed ) {
		return;
	}

	if ( bits == 0 || bits < -31 || bits > 32 ) {
		Com_Error( ERR_DROP, "MSG_WriteBits: bad bits %i", bits );
	}

	if ( bits < 0 ) {
		bits = -bits;
	}

	if ( msg->oob ) {
		if ( msg->cursize + ( bits >> 3 ) > msg->maxsize ) {
			msg->overflowed = qtrue;
			return;
		}

		if ( bits == 8 ) {
			msg->data[msg->cursize] = value;
			msg->cursize += 1;
		} else if ( bits == 16 ) {
			short temp = value;

			CopyLittleShort( &msg->data[msg->cursize], &temp );
			msg->cursize += 2;
		} else if ( bits==32 ) {
			CopyLittleLong( &msg->data[msg->cursize], &value );
			msg->cursize += 4;
		} else {
			Com_Error( ERR_DROP, "can't write %d bits", bits );
		}
	} else {
		v = (unsigned int)value;
		while ( bits ) {
			if ( msg->bit == 0 ) {
				if ( msg->cursize >= msg->maxsize ) {
					msg->overflowed = qtrue;
					return;
				}
				msg->data[msg->cursize++] = 0;
			}
			put = 8 - msg->bit;
			if ( put > bits ) {
				put = bits;
			}
			msg->data[msg->cursize - 1] |= ( v & ( ( 1u << put ) - 1 ) ) << msg->bit;
			v >>= put;
			msg->bit = ( msg->bit + put ) & 7;
			bits -= put;
		}
	}
}

int MSG_ReadBits( msg_t *msg, int bits ) {
	int				value;
	int				get;
	qboolean		sgn;
	int				i;
	unsigned int	v;

	if ( msg->readcount > msg->cursize ) {
		return 0;
	}

	value = 0;

	if ( bits < 0 ) {
		bits = -bits;
		sgn = qtrue;
	} else {
		sgn = qfalse;
	}

	if (msg->oob) {
		if (msg->readcount + (bits>>3) > msg->cursize) {
			msg->readcount = msg->cursize + 1;
			return 0;
		}

		if(bits==8)
		{
			value = msg->data[msg->readcount];
			msg->readcount += 1;
		}
		else if(bits==16)
		{
			short temp;

			CopyLittleShort(&temp, &msg->data[msg->readcount]);
			value = temp;
			msg->readcount += 2;
		}
		else if(bits==32)
		{
			CopyLittleLong(&value, &msg->data[msg->readcount]);
			msg->readcount += 4;
		}
		else
			Com_Error(ERR_DROP, "can't read %d bits", bits);
	} else {
		v = 0;
		for ( i = 0 ; i < bits ; i += get ) {
			if ( msg->bit == 0 ) {
				if ( msg->readcount >= msg->cursize ) {
					msg->readcount = msg->cursize + 1;
					return 0;
				}
				msg->readcount++;
			}
			get = 8 - msg->bit;
			if ( get > bits - i ) {
				get = bits - i;
			}
			v |= ( ( (unsigned int)msg->data[msg->readcount - 1] >> msg->bit ) & ( ( 1u << get ) - 1 ) ) << i;
			msg->bit = ( msg->bit + get ) & 7;
		}
		value = (int)v;
	}
	if ( sgn && bits > 0 && bits < 32 ) {
		if ( value & ( 1 << ( bits - 1 ) ) ) {
			value |= -1 ^ ( ( 1 << bits ) - 1 );
		}
	}

	return value;
}



//================================================================================

//
// writing functions
//

void MSG_WriteChar( msg_t *sb, int c ) {
#ifdef PARANOID
	if (c < -128 || c > 127)
		Com_Error (ERR_FATAL, "MSG_WriteChar: range error");
#endif

	MSG_WriteBits( sb, c, 8 );
}

void MSG_WriteByte( msg_t *sb, int c ) {
#ifdef PARANOID
	if (c < 0 || c > 255)
		Com_Error (ERR_FATAL, "MSG_WriteByte: range error");
#endif

	MSG_WriteBits( sb, c, 8 );
}

// Moves a bitstream to the next byte boundary as retail does (MSG_GetSpace
// 0x439370): past the byte begun and one more, which is left 0.
static void MSG_AlignWrite( msg_t *msg ) {
	if ( msg->oob || !msg->bit ) {
		return;
	}
	msg->bit = 0;
	if ( msg->cursize >= msg->maxsize ) {
		msg->overflowed = qtrue;
		return;
	}
	msg->data[msg->cursize++] = 0;
}

// The reader's side of MSG_AlignWrite (0x439350)
static void MSG_AlignRead( msg_t *msg ) {
	if ( msg->oob || !msg->bit ) {
		return;
	}
	msg->bit = 0;
	msg->readcount++;
}

void MSG_WriteData( msg_t *buf, const void *data, int length ) {
	int i;
	MSG_AlignWrite( buf );
	for(i=0;i<length;i++) {
		MSG_WriteByte(buf, ((byte *)data)[i]);
	}
}

void MSG_WriteShort( msg_t *sb, int c ) {
#ifdef PARANOID
	if (c < ((short)0x8000) || c > (short)0x7fff)
		Com_Error (ERR_FATAL, "MSG_WriteShort: range error");
#endif

	MSG_WriteBits( sb, c, 16 );
}

void MSG_WriteLong( msg_t *sb, int c ) {
	MSG_WriteBits( sb, c, 32 );
}

void MSG_WriteFloat( msg_t *sb, float f ) {
	floatint_t dat;
	dat.f = f;
	MSG_WriteBits( sb, dat.i, 32 );
}

void MSG_WriteString( msg_t *sb, const char *s ) {
	if ( !s ) {
		MSG_WriteData (sb, "", 1);
	} else {
		int		l,i;
		char	string[MAX_STRING_CHARS];

		l = strlen( s );
		if ( l >= MAX_STRING_CHARS ) {
			Com_Printf( "MSG_WriteString: MAX_STRING_CHARS" );
			MSG_WriteData (sb, "", 1);
			return;
		}
		Q_strncpyz( string, s, sizeof( string ) );

		// get rid of 0x80+ and '%' chars, because old clients don't like them
		for ( i = 0 ; i < l ; i++ ) {
			if ( ((byte *)string)[i] > 127 || string[i] == '%' ) {
				string[i] = '.';
			}
		}

		MSG_WriteData (sb, string, l+1);
	}
}

void MSG_WriteBigString( msg_t *sb, const char *s ) {
	if ( !s ) {
		MSG_WriteData (sb, "", 1);
	} else {
		int		l,i;
		char	string[BIG_INFO_STRING];

		l = strlen( s );
		if ( l >= BIG_INFO_STRING ) {
			Com_Printf( "MSG_WriteString: BIG_INFO_STRING" );
			MSG_WriteData (sb, "", 1);
			return;
		}
		Q_strncpyz( string, s, sizeof( string ) );

		// get rid of 0x80+ and '%' chars, because old clients don't like them
		for ( i = 0 ; i < l ; i++ ) {
			if ( ((byte *)string)[i] > 127 || string[i] == '%' ) {
				string[i] = '.';
			}
		}

		MSG_WriteData (sb, string, l+1);
	}
}

void MSG_WriteAngle( msg_t *sb, float f ) {
	MSG_WriteByte (sb, (int)(f*256/360) & 255);
}

void MSG_WriteAngle16( msg_t *sb, float f ) {
	MSG_WriteShort (sb, ANGLE2SHORT(f));
}


//============================================================

//
// reading functions
//

// returns -1 if no more characters are available
int MSG_ReadChar (msg_t *msg ) {
	int	c;
	
	c = (signed char)MSG_ReadBits( msg, 8 );
	if ( msg->readcount > msg->cursize ) {
		c = -1;
	}	
	
	return c;
}

int MSG_ReadByte( msg_t *msg ) {
	int	c;
	
	c = (unsigned char)MSG_ReadBits( msg, 8 );
	if ( msg->readcount > msg->cursize ) {
		c = -1;
	}	
	return c;
}

int MSG_LookaheadByte( msg_t *msg ) {
	const int readcount = msg->readcount;
	const int bit = msg->bit;
	int c = MSG_ReadByte(msg);
	msg->readcount = readcount;
	msg->bit = bit;
	return c;
}

int MSG_ReadShort( msg_t *msg ) {
	int	c;
	
	c = (short)MSG_ReadBits( msg, 16 );
	if ( msg->readcount > msg->cursize ) {
		c = -1;
	}	

	return c;
}

int MSG_ReadLong( msg_t *msg ) {
	int	c;
	
	c = MSG_ReadBits( msg, 32 );
	if ( msg->readcount > msg->cursize ) {
		c = -1;
	}	
	
	return c;
}

float MSG_ReadFloat( msg_t *msg ) {
	floatint_t dat;
	
	dat.i = MSG_ReadBits( msg, 32 );
	if ( msg->readcount > msg->cursize ) {
		dat.f = -1;
	}	
	
	return dat.f;	
}

char *MSG_ReadString( msg_t *msg ) {
	static char	string[MAX_STRING_CHARS];
	int		l,c;
	
	MSG_AlignRead( msg );
	l = 0;
	do {
		c = MSG_ReadByte(msg);		// use ReadByte so -1 is out of bounds
		if ( c == -1 || c == 0 ) {
			break;
		}
		// translate all fmt spec to avoid crash bugs
		if ( c == '%' ) {
			c = '.';
		}
		// don't allow higher ascii values
		if ( c > 127 ) {
			c = '.';
		}
		// break only after reading all expected data from bitstream
		if ( l >= sizeof(string)-1 ) {
			break;
		}
		string[l++] = c;
	} while (1);
	
	string[l] = '\0';
	
	return string;
}

char *MSG_ReadBigString( msg_t *msg ) {
	static char	string[BIG_INFO_STRING];
	int		l,c;
	
	MSG_AlignRead( msg );
	l = 0;
	do {
		c = MSG_ReadByte(msg);		// use ReadByte so -1 is out of bounds
		if ( c == -1 || c == 0 ) {
			break;
		}
		// translate all fmt spec to avoid crash bugs
		if ( c == '%' ) {
			c = '.';
		}
		// don't allow higher ascii values
		if ( c > 127 ) {
			c = '.';
		}
		// break only after reading all expected data from bitstream
		if ( l >= sizeof(string)-1 ) {
			break;
		}
		string[l++] = c;
	} while (1);
	
	string[l] = '\0';
	
	return string;
}

char *MSG_ReadStringLine( msg_t *msg ) {
	static char	string[MAX_STRING_CHARS];
	int		l,c;

	MSG_AlignRead( msg );
	l = 0;
	do {
		c = MSG_ReadByte(msg);		// use ReadByte so -1 is out of bounds
		if (c == -1 || c == 0 || c == '\n') {
			break;
		}
		// translate all fmt spec to avoid crash bugs
		if ( c == '%' ) {
			c = '.';
		}
		// don't allow higher ascii values
		if ( c > 127 ) {
			c = '.';
		}
		// break only after reading all expected data from bitstream
		if ( l >= sizeof(string)-1 ) {
			break;
		}
		string[l++] = c;
	} while (1);
	
	string[l] = '\0';
	
	return string;
}

float MSG_ReadAngle16( msg_t *msg ) {
	return SHORT2ANGLE(MSG_ReadShort(msg));
}

void MSG_ReadData( msg_t *msg, void *data, int len ) {
	int		i;

	MSG_AlignRead( msg );
	for (i=0 ; i<len ; i++) {
		((byte *)data)[i] = MSG_ReadByte (msg);
	}
}

// a string hasher which gives the same hash value even if the
// string is later modified via the legacy MSG read/write code
int MSG_HashKey(const char *string, int maxlen) {
	int hash, i;

	hash = 0;
	for (i = 0; i < maxlen && string[i] != '\0'; i++) {
		if (string[i] & 0x80 || string[i] == '%')
			hash += '.' * (119 + i);
		else
			hash += string[i] * (119 + i);
	}
	hash = (hash ^ (hash >> 10) ^ (hash >> 20));
	return hash;
}

extern cvar_t *cl_shownet;

#define	LOG(x) if( cl_shownet && cl_shownet->integer == 4 ) { Com_Printf("%s ", x ); };

/*
=============================================================================

delta functions with keys
  
=============================================================================
*/

int kbitmask[32] = {
	0x00000001, 0x00000003, 0x00000007, 0x0000000F,
	0x0000001F,	0x0000003F,	0x0000007F,	0x000000FF,
	0x000001FF,	0x000003FF,	0x000007FF,	0x00000FFF,
	0x00001FFF,	0x00003FFF,	0x00007FFF,	0x0000FFFF,
	0x0001FFFF,	0x0003FFFF,	0x0007FFFF,	0x000FFFFF,
	0x001FFFFf,	0x003FFFFF,	0x007FFFFF,	0x00FFFFFF,
	0x01FFFFFF,	0x03FFFFFF,	0x07FFFFFF,	0x0FFFFFFF,
	0x1FFFFFFF,	0x3FFFFFFF,	0x7FFFFFFF,	0xFFFFFFFF,
};

void MSG_WriteDeltaKey( msg_t *msg, int key, int oldV, int newV, int bits ) {
	if ( oldV == newV ) {
		MSG_WriteBits( msg, 0, 1 );
		return;
	}
	MSG_WriteBits( msg, 1, 1 );
	MSG_WriteBits( msg, newV ^ key, bits );
}

int	MSG_ReadDeltaKey( msg_t *msg, int key, int oldV, int bits ) {
	if ( MSG_ReadBits( msg, 1 ) ) {
		return MSG_ReadBits( msg, bits ) ^ (key & kbitmask[ bits - 1 ]);
	}
	return oldV;
}

void MSG_WriteDeltaKeyFloat( msg_t *msg, int key, float oldV, float newV ) {
	floatint_t fi;
	if ( oldV == newV ) {
		MSG_WriteBits( msg, 0, 1 );
		return;
	}
	fi.f = newV;
	MSG_WriteBits( msg, 1, 1 );
	MSG_WriteBits( msg, fi.i ^ key, 32 );
}

float MSG_ReadDeltaKeyFloat( msg_t *msg, int key, float oldV ) {
	if ( MSG_ReadBits( msg, 1 ) ) {
		floatint_t fi;

		fi.i = MSG_ReadBits( msg, 32 ) ^ key;
		return fi.f;
	}
	return oldV;
}


/*
============================================================================

usercmd_t communication

============================================================================
*/

/*
=====================
MSG_WriteDeltaUsercmdKey
=====================
*/
void MSG_WriteDeltaUsercmdKey( msg_t *msg, int key, usercmd_t *from, usercmd_t *to ) {
	if ( to->serverTime - from->serverTime < 256 ) {
		MSG_WriteBits( msg, 1, 1 );
		MSG_WriteBits( msg, to->serverTime - from->serverTime, 8 );
	} else {
		MSG_WriteBits( msg, 0, 1 );
		MSG_WriteBits( msg, to->serverTime, 32 );
	}
	if (from->angles[0] == to->angles[0] &&
		from->angles[1] == to->angles[1] &&
		from->angles[2] == to->angles[2] &&
		from->forwardmove == to->forwardmove &&
		from->rightmove == to->rightmove &&
		from->upmove == to->upmove &&
		from->buttons == to->buttons &&
		from->weapon == to->weapon) {
			MSG_WriteBits( msg, 0, 1 );				// no change
			oldsize += 7;
			return;
	}
	key ^= to->serverTime;
	MSG_WriteBits( msg, 1, 1 );
	MSG_WriteDeltaKey( msg, key, from->angles[0], to->angles[0], 16 );
	MSG_WriteDeltaKey( msg, key, from->angles[1], to->angles[1], 16 );
	MSG_WriteDeltaKey( msg, key, from->angles[2], to->angles[2], 16 );
	MSG_WriteDeltaKey( msg, key, from->forwardmove, to->forwardmove, 8 );
	MSG_WriteDeltaKey( msg, key, from->rightmove, to->rightmove, 8 );
	MSG_WriteDeltaKey( msg, key, from->upmove, to->upmove, 8 );
	MSG_WriteDeltaKey( msg, key, from->buttons, to->buttons, 16 );
	MSG_WriteDeltaKey( msg, key, from->weapon, to->weapon, 8 );
}


/*
=====================
MSG_ReadDeltaUsercmdKey
=====================
*/
void MSG_ReadDeltaUsercmdKey( msg_t *msg, int key, usercmd_t *from, usercmd_t *to ) {
	if ( MSG_ReadBits( msg, 1 ) ) {
		to->serverTime = from->serverTime + MSG_ReadBits( msg, 8 );
	} else {
		to->serverTime = MSG_ReadBits( msg, 32 );
	}
	if ( MSG_ReadBits( msg, 1 ) ) {
		key ^= to->serverTime;
		to->angles[0] = MSG_ReadDeltaKey( msg, key, from->angles[0], 16);
		to->angles[1] = MSG_ReadDeltaKey( msg, key, from->angles[1], 16);
		to->angles[2] = MSG_ReadDeltaKey( msg, key, from->angles[2], 16);
		to->forwardmove = MSG_ReadDeltaKey( msg, key, from->forwardmove, 8);
		if( to->forwardmove == -128 )
			to->forwardmove = -127;
		to->rightmove = MSG_ReadDeltaKey( msg, key, from->rightmove, 8);
		if( to->rightmove == -128 )
			to->rightmove = -127;
		to->upmove = MSG_ReadDeltaKey( msg, key, from->upmove, 8);
		if( to->upmove == -128 )
			to->upmove = -127;
		to->buttons = MSG_ReadDeltaKey( msg, key, from->buttons, 16);
		to->weapon = MSG_ReadDeltaKey( msg, key, from->weapon, 8);
	} else {
		to->angles[0] = from->angles[0];
		to->angles[1] = from->angles[1];
		to->angles[2] = from->angles[2];
		to->forwardmove = from->forwardmove;
		to->rightmove = from->rightmove;
		to->upmove = from->upmove;
		to->buttons = from->buttons;
		to->weapon = from->weapon;
	}
}

/*
=============================================================================

entityState_t communication
  
=============================================================================
*/

typedef struct {
	char	*name;
	int		offset;
	int		bits;		// 0 = float
} netField_t;

// using the stringizing operator to save typing...
#define	NETF(x) #x,(size_t)&((entityState_t*)0)->x

netField_t	entityStateFields[] =
{
// FAKK2: the retail engine's fields, order and widths (fakk2.exe 0x4ea228).
// Fields the SDK marks "not sent over" (bone_quat, bindparent, quat, mat) are
// left out, and so are origin and angles, which the reader takes from
// netorigin and netangles as retail does. surfaces follows separately.
{ NETF(eType), 8 },
{ NETF(eFlags), 16 },
{ NETF(pos.trType), 8 },
{ NETF(pos.trTime), 32 },
{ NETF(pos.trDuration), 32 },
{ NETF(pos.trBase[0]), 0 },
{ NETF(pos.trBase[1]), 0 },
{ NETF(pos.trBase[2]), 0 },
{ NETF(pos.trDelta[0]), 0 },
{ NETF(pos.trDelta[1]), 0 },
{ NETF(pos.trDelta[2]), 0 },
{ NETF(apos.trType), 8 },
{ NETF(apos.trTime), 32 },
{ NETF(apos.trDuration), 32 },
{ NETF(apos.trBase[0]), 0 },
{ NETF(apos.trBase[1]), 0 },
{ NETF(apos.trBase[2]), 0 },
{ NETF(apos.trDelta[0]), 0 },
{ NETF(apos.trDelta[1]), 0 },
{ NETF(apos.trDelta[2]), 0 },
{ NETF(netorigin[0]), 0 },
{ NETF(netorigin[1]), 0 },
{ NETF(netorigin[2]), 0 },
{ NETF(origin2[0]), 0 },
{ NETF(origin2[1]), 0 },
{ NETF(origin2[2]), 0 },
{ NETF(netangles[0]), 0 },
{ NETF(netangles[1]), 0 },
{ NETF(netangles[2]), 0 },
{ NETF(constantLight), 32 },
{ NETF(loopSound), 16 },
{ NETF(loopSoundVolume), 0 },
{ NETF(loopSoundMinDist), 0 },
{ NETF(parent), 16 },
{ NETF(tag_num), 10 },
{ NETF(attach_use_angles), 1 },
{ NETF(attach_offset[0]), 0 },
{ NETF(attach_offset[1]), 0 },
{ NETF(attach_offset[2]), 0 },
{ NETF(modelindex), 16 },
{ NETF(skinNum), 16 },
{ NETF(anim), 11 },
{ NETF(frame), 10 },
{ NETF(crossblend_time), -16 },
{ NETF(torso_anim), 11 },
{ NETF(torso_frame), 10 },
{ NETF(torso_crossblend_time), -16 },
{ NETF(bone_tag[0]), -8 },
{ NETF(bone_tag[1]), -8 },
{ NETF(bone_tag[2]), -8 },
{ NETF(bone_tag[3]), -8 },
{ NETF(bone_tag[4]), -8 },
{ NETF(bone_angles[0][0]), 0 },
{ NETF(bone_angles[0][1]), 0 },
{ NETF(bone_angles[0][2]), 0 },
{ NETF(bone_angles[1][0]), 0 },
{ NETF(bone_angles[1][1]), 0 },
{ NETF(bone_angles[1][2]), 0 },
{ NETF(bone_angles[2][0]), 0 },
{ NETF(bone_angles[2][1]), 0 },
{ NETF(bone_angles[2][2]), 0 },
{ NETF(bone_angles[3][0]), 0 },
{ NETF(bone_angles[3][1]), 0 },
{ NETF(bone_angles[3][2]), 0 },
{ NETF(bone_angles[4][0]), 0 },
{ NETF(bone_angles[4][1]), 0 },
{ NETF(bone_angles[4][2]), 0 },
{ NETF(clientNum), 8 },
{ NETF(groundEntityNum), GENTITYNUM_BITS },
{ NETF(solid), 32 },
{ NETF(scale), 0 },
{ NETF(alpha), 0 },
{ NETF(renderfx), 32 }
};

// Which fields changed goes as a bit per field of entityStateFields, or as the
// index of one of these common masks when it is one of them (fakk2.exe
// 0x4e6220); index 31 means the mask itself follows.
#define	CHANGE_VECTOR_BYTES		10
#define	NUM_CHANGE_VECTORS		31
#define	CHANGE_VECTOR_BITS		5

static const byte changeVectors[NUM_CHANGE_VECTORS][CHANGE_VECTOR_BYTES] =
{
	{ 0xe0, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x60, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x60, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0xe1, 0x00, 0x00, 0x0e, 0x04, 0x20, 0x00, 0x00, 0x00, 0x00 },
	{ 0xe0, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x60, 0xc0, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00 },
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00 },
	{ 0xa0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0xe0, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x60, 0x80, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00 },
	{ 0x60, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00 },
	{ 0xed, 0x07, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00 },
	{ 0x60, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x60, 0xc0, 0x00, 0x00, 0x00, 0x10, 0x02, 0x00, 0x00, 0x00 },
	{ 0xa0, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0xed, 0x07, 0x00, 0x00, 0x00, 0x30, 0x00, 0x00, 0x00, 0x00 },
	{ 0xc0, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x60, 0x80, 0x00, 0x00, 0x00, 0x10, 0x02, 0x00, 0x00, 0x00 },
	{ 0x60, 0xc0, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0xe0, 0xc0, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x60, 0x00, 0x00, 0x00, 0x00, 0x10, 0x02, 0x00, 0x00, 0x00 },
	{ 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0xe0, 0xc0, 0x00, 0x00, 0x00, 0x10, 0x02, 0x00, 0x00, 0x00 },
	{ 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x60, 0x80, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0xe1, 0x00, 0x00, 0x0e, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00 },
	{ 0x00, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
};

// masks read as an index and read in full (fakk2.exe 0xacc1c0, 0xacc1c4)
static int	vectorsCompressed, vectorsUncompressed;

/*
=================
MSG_ReportChangeVectors_f

What retail's changeVectors prints (fakk2.exe 0x439b80): its search for new
common masks was compiled out.
=================
*/
void MSG_ReportChangeVectors_f( void ) {
	int		total = vectorsCompressed + vectorsUncompressed;

	Com_Printf( "FIND_NEW_CHANGE_VECTORS not defined.\n" );
	Com_Printf( "%i%% of vectors compressed\n", total ? vectorsCompressed * 100 / total : 0 );
}

/*
=================
MSG_GetNullEntityState

The state a game state's baselines are written and read against (fakk2.exe
0x43a220), not all zeros.
=================
*/
void MSG_GetNullEntityState( entityState_t *nullState ) {
	int		i;

	Com_Memset( nullState, 0, sizeof( *nullState ) );
	nullState->alpha = 1.0f;
	nullState->scale = 1.0f;
	nullState->parent = ENTITYNUM_NONE;
	nullState->constantLight = 0xffffff;
	nullState->renderfx = 16;
	for ( i = 0 ; i < NUM_BONE_CONTROLLERS ; i++ ) {
		nullState->bone_tag[i] = -1;
	}
}

// if (int)f == f and (int)f + ( 1<<(FLOAT_INT_BITS-1) ) < ( 1 << FLOAT_INT_BITS )
// the float will be sent with FLOAT_INT_BITS, otherwise all 32 bits will be sent
#define	FLOAT_INT_BITS	13
#define	FLOAT_INT_BIAS	(1<<(FLOAT_INT_BITS-1))

// One changed field: an integer in its bits, or a float as a bit and then 13
// bits of integral value or all 32 (fakk2.exe 0x439d7c)
static void MSG_WriteNetField( msg_t *msg, const netField_t *field, const int *toF ) {
	float	fullFloat;
	int		trunc;

	if ( field->bits ) {
		MSG_WriteBits( msg, *toF, field->bits );
		return;
	}
	fullFloat = *(const float *)toF;
	trunc = (int)fullFloat;
	if ( trunc == fullFloat && trunc + FLOAT_INT_BIAS >= 0 &&
		trunc + FLOAT_INT_BIAS < ( 1 << FLOAT_INT_BITS ) ) {
		MSG_WriteBits( msg, 0, 1 );
		MSG_WriteBits( msg, trunc + FLOAT_INT_BIAS, FLOAT_INT_BITS );
	} else {
		MSG_WriteBits( msg, 1, 1 );
		MSG_WriteBits( msg, *toF, 32 );
	}
}

static void MSG_ReadNetField( msg_t *msg, const netField_t *field, int *toF, qboolean print ) {
	int		trunc;

	if ( field->bits ) {
		*toF = MSG_ReadBits( msg, field->bits );
		if ( print ) {
			Com_Printf( "%s:%i ", field->name, *toF );
		}
	} else if ( MSG_ReadBits( msg, 1 ) == 0 ) {
		trunc = MSG_ReadBits( msg, FLOAT_INT_BITS ) - FLOAT_INT_BIAS;
		*(float *)toF = trunc;
		if ( print ) {
			Com_Printf( "%s:%i ", field->name, trunc );
		}
	} else {
		*toF = MSG_ReadBits( msg, 32 );
		if ( print ) {
			Com_Printf( "%s:%f ", field->name, *(float *)toF );
		}
	}
}

/*
==================
MSG_WriteDeltaEntity

Writes part of a packetentities message, including the entity number.
Can delta from either a baseline or a previous packet_entity
If to is NULL, a remove entity update will be sent
If force is not set, then nothing at all will be generated if the entity is
identical, under the assumption that the in-order delta code will catch it.
==================
*/
void MSG_WriteDeltaEntity( msg_t *msg, struct entityState_s *from, struct entityState_s *to,
						   qboolean force ) {
	int			i, vector;
	int			numFields;
	netField_t	*field;
	int			*fromF, *toF;
	byte		changes[CHANGE_VECTOR_BYTES];
	unsigned	surfaces;

	numFields = ARRAY_LEN( entityStateFields );

	// a NULL to is a delta remove message
	if ( to == NULL ) {
		if ( from == NULL ) {
			return;
		}
		MSG_WriteBits( msg, from->number, GENTITYNUM_BITS );
		MSG_WriteBits( msg, 1, 1 );
		return;
	}

	if ( to->number < 0 || to->number >= MAX_GENTITIES ) {
		Com_Error (ERR_FATAL, "MSG_WriteDeltaEntity: Bad entity number: %i", to->number );
	}

	Com_Memset( changes, 0, sizeof( changes ) );
	for ( i = 0, field = entityStateFields ; i < numFields ; i++, field++ ) {
		fromF = (int *)( (byte *)from + field->offset );
		toF = (int *)( (byte *)to + field->offset );
		if ( *fromF != *toF ) {
			changes[i >> 3] |= 1 << ( i & 7 );
		}
	}
	surfaces = 0;
	for ( i = 0 ; i < MAX_MODEL_SURFACES ; i++ ) {
		if ( from->surfaces[i] != to->surfaces[i] ) {
			surfaces |= 1u << i;
		}
	}

	for ( i = 0 ; i < CHANGE_VECTOR_BYTES && !changes[i] ; i++ ) {
	}
	if ( i == CHANGE_VECTOR_BYTES && !surfaces ) {
		// nothing at all changed
		if ( !force ) {
			return;		// nothing at all
		}
		// write two bits for no change
		MSG_WriteBits( msg, to->number, GENTITYNUM_BITS );
		MSG_WriteBits( msg, 0, 1 );		// not removed
		MSG_WriteBits( msg, 0, 1 );		// no delta
		return;
	}

	for ( vector = 0 ; vector < NUM_CHANGE_VECTORS ; vector++ ) {
		if ( !memcmp( changes, changeVectors[vector], CHANGE_VECTOR_BYTES ) ) {
			break;
		}
	}

	MSG_WriteBits( msg, to->number, GENTITYNUM_BITS );
	MSG_WriteBits( msg, 0, 1 );			// not removed
	MSG_WriteBits( msg, 1, 1 );			// we have a delta
	MSG_WriteBits( msg, vector, CHANGE_VECTOR_BITS );
	if ( vector == NUM_CHANGE_VECTORS ) {
		// not a common one: the mask itself, a byte at a time and the last bit
		for ( i = 0 ; i + 8 <= numFields ; i += 8 ) {
			MSG_WriteByte( msg, changes[i >> 3] );
		}
		MSG_WriteBits( msg, changes[i >> 3], numFields - i );
	}

	oldsize += numFields;

	for ( i = 0, field = entityStateFields ; i < numFields ; i++, field++ ) {
		if ( changes[i >> 3] & ( 1 << ( i & 7 ) ) ) {
			MSG_WriteNetField( msg, field, (int *)( (byte *)to + field->offset ) );
		}
	}

	if ( !surfaces ) {
		MSG_WriteBits( msg, 0, 1 );
		return;
	}
	MSG_WriteBits( msg, 1, 1 );
	MSG_WriteLong( msg, (int)surfaces );
	for ( i = 0 ; i < MAX_MODEL_SURFACES ; i++ ) {
		if ( surfaces & ( 1u << i ) ) {
			MSG_WriteByte( msg, to->surfaces[i] );
		}
	}
}

/*
==================
MSG_ReadDeltaEntity

The entity number has already been read from the message, which
is how the from state is identified.

If the delta removes the entity, entityState_t->number will be set to MAX_GENTITIES-1

Can go from either a baseline or a previous packet_entity
==================
*/
void MSG_ReadDeltaEntity( msg_t *msg, entityState_t *from, entityState_t *to,
						 int number) {
	int			i, vector;
	int			numFields;
	netField_t	*field;
	int			print;
	unsigned	surfaces;
	byte		explicitChanges[CHANGE_VECTOR_BYTES];
	const byte	*changes;

	if ( number < 0 || number >= MAX_GENTITIES) {
		Com_Error( ERR_DROP, "Bad delta entity number: %i", number );
	}

	// check for a remove
	if ( MSG_ReadBits( msg, 1 ) == 1 ) {
		Com_Memset( to, 0, sizeof( *to ) );
		to->number = MAX_GENTITIES - 1;
		if ( cl_shownet && ( cl_shownet->integer >= 2 || cl_shownet->integer == -1 ) ) {
			Com_Printf( "%3i: #%-3i remove\n", msg->readcount, number );
		}
		return;
	}

	// check for no delta
	if ( MSG_ReadBits( msg, 1 ) == 0 ) {
		*to = *from;
		to->number = number;
		return;
	}

	// shownet 2/3 will interleave with other printed info, -1 will
	// just print the delta records`
	if ( cl_shownet && ( cl_shownet->integer >= 2 || cl_shownet->integer == -1 ) ) {
		print = 1;
		Com_Printf( "%3i: #%-3i ", msg->readcount, number );
	} else {
		print = 0;
	}

	numFields = ARRAY_LEN( entityStateFields );
	vector = MSG_ReadBits( msg, CHANGE_VECTOR_BITS );
	if ( vector == NUM_CHANGE_VECTORS ) {
		vectorsUncompressed++;
		Com_Memset( explicitChanges, 0, sizeof( explicitChanges ) );
		for ( i = 0 ; i + 8 <= numFields ; i += 8 ) {
			explicitChanges[i >> 3] = MSG_ReadByte( msg );
		}
		explicitChanges[i >> 3] = MSG_ReadBits( msg, numFields - i );
		changes = explicitChanges;
		if ( print ) {
			Com_Printf( "<uc> " );
		}
	} else {
		vectorsCompressed++;
		changes = changeVectors[vector];
		if ( print ) {
			Com_Printf( "<%2i> ", vector );
		}
	}

	*to = *from;
	to->number = number;

	for ( i = 0, field = entityStateFields ; i < numFields ; i++, field++ ) {
		if ( changes[i >> 3] & ( 1 << ( i & 7 ) ) ) {
			MSG_ReadNetField( msg, field, (int *)( (byte *)to + field->offset ), print );
		}
	}

	if ( MSG_ReadBits( msg, 1 ) ) {
		surfaces = MSG_ReadLong( msg );
		for ( i = 0 ; i < MAX_MODEL_SURFACES ; i++ ) {
			if ( surfaces & ( 1u << i ) ) {
				to->surfaces[i] = MSG_ReadByte( msg );
			}
		}
	}

	// FAKK2 (fakk2.exe 0x439e70): the fields that travel are the net ones, and
	// the quaternions the game modules use are derived here, never sent
	VectorCopy( to->netorigin, to->origin );
	VectorCopy( to->netangles, to->angles );
	EulerToQuat( to->angles, to->quat );
	for ( i = 0 ; i < NUM_BONE_CONTROLLERS ; i++ ) {
		if ( to->bone_tag[i] >= 0 ) {
			EulerToQuat( to->bone_angles[i], to->bone_quat[i] );
		}
	}

	if ( print ) {
		Com_Printf( "\n" );
	}
}


/*
============================================================================

plyer_state_t communication

============================================================================
*/

// using the stringizing operator to save typing...
#define	PSF(x) #x,(size_t)&((playerState_t*)0)->x

netField_t	playerStateFields[] =
{
// FAKK2: the retail engine's fields, order and widths (fakk2.exe 0x4ea598).
// walking, groundPlane, falldir, groundTrace and ping are not sent.
{ PSF(commandTime), 32 },
{ PSF(pm_type), 8 },
{ PSF(bobCycle), 8 },
{ PSF(pm_flags), 16 },
{ PSF(pm_time), -16 },
{ PSF(pm_runtime), -16 },
{ PSF(origin[0]), 0 },
{ PSF(origin[1]), 0 },
{ PSF(origin[2]), 0 },
{ PSF(velocity[0]), 0 },
{ PSF(velocity[1]), 0 },
{ PSF(velocity[2]), 0 },
{ PSF(gravity), 16 },
{ PSF(speed), 16 },
{ PSF(delta_angles[0]), 16 },
{ PSF(delta_angles[1]), 16 },
{ PSF(delta_angles[2]), 16 },
{ PSF(groundEntityNum), GENTITYNUM_BITS },
{ PSF(feetfalling), 8 },
{ PSF(clientNum), 8 },
{ PSF(viewangles[0]), 0 },
{ PSF(viewangles[1]), 0 },
{ PSF(viewangles[2]), 0 },
{ PSF(viewheight), -8 },
{ PSF(current_music_mood), 8 },
{ PSF(fallback_music_mood), 8 },
{ PSF(music_volume), 0 },
{ PSF(music_volume_fade_time), 0 },
{ PSF(reverb_type), 6 },
{ PSF(reverb_level), 0 },
{ PSF(blend[0]), 0 },
{ PSF(blend[1]), 0 },
{ PSF(blend[2]), 0 },
{ PSF(blend[3]), 0 },
{ PSF(fov), 0 },
{ PSF(camera_angles[0]), 0 },
{ PSF(camera_angles[1]), 0 },
{ PSF(camera_angles[2]), 0 },
{ PSF(camera_origin[0]), 0 },
{ PSF(camera_origin[1]), 0 },
{ PSF(camera_origin[2]), 0 },
{ PSF(camera_offset[0]), 0 },
{ PSF(camera_offset[1]), 0 },
{ PSF(camera_offset[2]), 0 },
{ PSF(damage_angles[0]), 0 },
{ PSF(damage_angles[1]), 0 },
{ PSF(damage_angles[2]), 0 },
{ PSF(camera_time), 0 },
{ PSF(camera_flags), 16 }
};

// playerState_t arrays, each PS_ARRAY_LEN ints, sent as a bit, then when one
// changed a 32-bit mask and a short for each change (fakk2.exe 0x43a37e)
#define	PS_ARRAY_LEN	32
#define	PSA(x) (size_t)&((playerState_t*)0)->x[0], #x

static const struct {
	size_t		offset;
	const char	*name;
} playerStateArrays[] =
{
	{ PSA(stats) },
	{ PSA(activeItems) },
	{ PSA(inventory_name_index) },
	{ PSA(ammo_name_index) },
	{ PSA(ammo_amount) },
	{ PSA(max_ammo_amount) }
};

/*
=============
MSG_WriteDeltaPlayerstate

fakk2.exe 0x43a280: a bit per field, then the arrays
=============
*/
void MSG_WriteDeltaPlayerstate( msg_t *msg, struct playerState_s *from, struct playerState_s *to ) {
	int				i, j;
	playerState_t	dummy;
	int				numFields;
	netField_t		*field;
	int				*fromF, *toF;
	unsigned		changed;

	if (!from) {
		from = &dummy;
		Com_Memset (&dummy, 0, sizeof(dummy));
	}

	numFields = ARRAY_LEN( playerStateFields );

	for ( i = 0, field = playerStateFields ; i < numFields ; i++, field++ ) {
		fromF = (int *)( (byte *)from + field->offset );
		toF = (int *)( (byte *)to + field->offset );

		if ( *fromF == *toF ) {
			MSG_WriteBits( msg, 0, 1 );	// no change
			continue;
		}
		MSG_WriteBits( msg, 1, 1 );	// changed
		MSG_WriteNetField( msg, field, toF );
	}

	for ( j = 0 ; j < (int)ARRAY_LEN( playerStateArrays ) ; j++ ) {
		fromF = (int *)( (byte *)from + playerStateArrays[j].offset );
		toF = (int *)( (byte *)to + playerStateArrays[j].offset );
		changed = 0;
		for ( i = 0 ; i < PS_ARRAY_LEN ; i++ ) {
			if ( toF[i] != fromF[i] ) {
				changed |= 1u << i;
			}
		}
		if ( !changed ) {
			MSG_WriteBits( msg, 0, 1 );	// no change
			continue;
		}
		MSG_WriteBits( msg, 1, 1 );	// changed
		MSG_WriteLong( msg, (int)changed );
		for ( i = 0 ; i < PS_ARRAY_LEN ; i++ ) {
			if ( changed & ( 1u << i ) ) {
				MSG_WriteShort( msg, toF[i] );
			}
		}
	}
}


/*
===================
MSG_ReadDeltaPlayerstate

fakk2.exe 0x43a680
===================
*/
void MSG_ReadDeltaPlayerstate (msg_t *msg, playerState_t *from, playerState_t *to ) {
	int			i, j;
	netField_t	*field;
	int			numFields;
	int			print;
	int			*toF;
	unsigned	changed;
	playerState_t	dummy;

	if ( !from ) {
		from = &dummy;
		Com_Memset( &dummy, 0, sizeof( dummy ) );
	}
	*to = *from;

	// shownet 2/3 will interleave with other printed info, -2 will
	// just print the delta records
	if ( cl_shownet && ( cl_shownet->integer >= 2 || cl_shownet->integer == -2 ) ) {
		print = 1;
		Com_Printf( "%3i: playerstate ", msg->readcount );
	} else {
		print = 0;
	}

	numFields = ARRAY_LEN( playerStateFields );
	for ( i = 0, field = playerStateFields ; i < numFields ; i++, field++ ) {
		if ( MSG_ReadBits( msg, 1 ) ) {
			MSG_ReadNetField( msg, field, (int *)( (byte *)to + field->offset ), print );
		}
	}

	for ( j = 0 ; j < (int)ARRAY_LEN( playerStateArrays ) ; j++ ) {
		if ( !MSG_ReadBits( msg, 1 ) ) {
			continue;
		}
		LOG( playerStateArrays[j].name );
		toF = (int *)( (byte *)to + playerStateArrays[j].offset );
		changed = MSG_ReadLong( msg );
		for ( i = 0 ; i < PS_ARRAY_LEN ; i++ ) {
			if ( changed & ( 1u << i ) ) {
				toF[i] = MSG_ReadShort( msg );
			}
		}
	}

	if ( print ) {
		Com_Printf( "\n" );
	}
}

//===========================================================================
