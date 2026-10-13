#include "tr_common.h"

/*
========================================================================

FTX files are FAKK2's texture format: a twelve byte header followed by
uncompressed 32 bit RGBA pixels, top row first.

========================================================================
*/

typedef struct {
	int		width;
	int		height;
	int		has_alpha;
} ftxHeader_t;

void R_LoadFTX( const char *name, byte **pic, int *width, int *height )
{
	union {
		byte *b;
		void *v;
	} buffer;
	ftxHeader_t	header;
	int			length;
	unsigned	numPixels;

	*pic = NULL;
	if ( width ) {
		*width = 0;
	}
	if ( height ) {
		*height = 0;
	}

	length = ri.FS_ReadFile( ( char * ) name, &buffer.v );
	if ( !buffer.b || length < 0 ) {
		return;
	}

	if ( length < (int)sizeof( header ) ) {
		ri.FS_FreeFile( buffer.v );
		ri.Error( ERR_DROP, "LoadFTX: header too short (%s)", name );
	}

	header.width = LittleLong( ( (ftxHeader_t *)buffer.b )->width );
	header.height = LittleLong( ( (ftxHeader_t *)buffer.b )->height );
	header.has_alpha = LittleLong( ( (ftxHeader_t *)buffer.b )->has_alpha );

	if ( header.width <= 0 || header.height <= 0 || header.width > 4096 || header.height > 4096 ) {
		ri.FS_FreeFile( buffer.v );
		ri.Error( ERR_DROP, "LoadFTX: %s has an invalid image size", name );
	}

	numPixels = (unsigned)header.width * (unsigned)header.height;
	if ( (unsigned)length - sizeof( header ) < numPixels * 4 ) {
		ri.FS_FreeFile( buffer.v );
		ri.Error( ERR_DROP, "LoadFTX: file truncated (%s)", name );
	}

	*pic = ri.Malloc( numPixels * 4 );
	Com_Memcpy( *pic, buffer.b + sizeof( header ), numPixels * 4 );

	if ( width ) {
		*width = header.width;
	}
	if ( height ) {
		*height = header.height;
	}

	ri.FS_FreeFile( buffer.v );
}
