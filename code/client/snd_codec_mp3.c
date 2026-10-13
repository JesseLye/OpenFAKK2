/*
===========================================================================
MP3 codec, for FAKK2's dialog.

FAKK2 keeps 1340 of its dialog files as MP3 under sound/dialog, which the
retail engine streams through the Miles sound system (its start-sound
routine, fakk2.exe 0x45bf00, sends every ".mp3" name down a streaming
path). Here they are decoded with dr_mp3 (public domain / MIT-0) like any
other sound: whole when registered, or as a stream for music.
===========================================================================
*/

#ifdef USE_CODEC_MP3

#include "client.h"
#include "snd_codec.h"

#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#ifdef _MSC_VER
#pragma warning( push, 0 )
#endif
#include "../thirdparty/dr_libs/dr_mp3.h"
#ifdef _MSC_VER
#pragma warning( pop )
#endif

// dr_mp3 gives signed 16-bit samples
#define MP3_SAMPLEWIDTH 2

snd_codec_t mp3_codec =
{
	"mp3",
	S_MP3_CodecLoad,
	S_MP3_CodecOpenStream,
	S_MP3_CodecReadStream,
	S_MP3_CodecCloseStream,
	NULL,
	S_MP3_CodecSeekStream
};

// dr_mp3 reads the file through these, with the snd_stream_t as its user data

static size_t S_MP3_Read( void *user, void *out, size_t bytes )
{
	snd_stream_t *stream = (snd_stream_t *)user;
	int read;

	if ( !out || !bytes )
		return 0;

	read = FS_Read( out, (int)bytes, stream->file );
	if ( read <= 0 )
		return 0;

	stream->pos += read;
	return read;
}

static drmp3_bool32 S_MP3_Seek( void *user, int offset, drmp3_seek_origin origin )
{
	snd_stream_t *stream = (snd_stream_t *)user;
	int whence;

	switch ( origin )
	{
	case DRMP3_SEEK_SET:
		whence = FS_SEEK_SET;
		break;
	case DRMP3_SEEK_CUR:
		whence = FS_SEEK_CUR;
		break;
	default:
		whence = FS_SEEK_END;
		break;
	}

	if ( FS_Seek( stream->file, offset, whence ) < 0 )
		return DRMP3_FALSE;

	stream->pos = FS_FTell( stream->file );
	return DRMP3_TRUE;
}

static drmp3_bool32 S_MP3_Tell( void *user, drmp3_int64 *cursor )
{
	snd_stream_t *stream = (snd_stream_t *)user;

	*cursor = FS_FTell( stream->file );
	return DRMP3_TRUE;
}

/*
=================
S_MP3_CodecOpenStream

The sample count takes a pass over the file's frame headers (no decoding),
after which the stream is back at its start.
=================
*/
snd_stream_t *S_MP3_CodecOpenStream( const char *filename )
{
	snd_stream_t *stream;
	drmp3 *mp3;
	drmp3_uint64 frames;

	if ( !filename )
		return NULL;

	stream = S_CodecUtilOpen( filename, &mp3_codec );
	if ( !stream )
		return NULL;

	mp3 = Z_Malloc( sizeof( *mp3 ) );
	stream->pos = 0;

	if ( !drmp3_init( mp3, S_MP3_Read, S_MP3_Seek, S_MP3_Tell, NULL, stream, NULL ) || !mp3->channels )
	{
		Z_Free( mp3 );
		S_CodecUtilClose( &stream );
		return NULL;
	}

	frames = drmp3_get_pcm_frame_count( mp3 );
	if ( !frames )
	{
		drmp3_uninit( mp3 );
		Z_Free( mp3 );
		S_CodecUtilClose( &stream );
		return NULL;
	}

	stream->info.rate = mp3->sampleRate;
	stream->info.width = MP3_SAMPLEWIDTH;
	stream->info.channels = mp3->channels;
	stream->info.samples = (int)frames;
	stream->info.size = stream->info.samples * stream->info.channels * MP3_SAMPLEWIDTH;
	stream->info.dataofs = 0;
	stream->ptr = mp3;

	return stream;
}

/*
=================
S_MP3_CodecSeekStream
=================
*/
qboolean S_MP3_CodecSeekStream( snd_stream_t *stream, int sampleFrame )
{
	if ( !stream || sampleFrame < 0 )
		return qfalse;

	return drmp3_seek_to_pcm_frame( (drmp3 *)stream->ptr, (drmp3_uint64)sampleFrame ) ? qtrue : qfalse;
}

/*
=================
S_MP3_CodecCloseStream
=================
*/
void S_MP3_CodecCloseStream( snd_stream_t *stream )
{
	if ( !stream )
		return;

	drmp3_uninit( (drmp3 *)stream->ptr );
	Z_Free( stream->ptr );
	S_CodecUtilClose( &stream );
}

/*
=================
S_MP3_CodecReadStream

Returns the bytes decoded, a whole number of sample frames.
=================
*/
int S_MP3_CodecReadStream( snd_stream_t *stream, int bytes, void *buffer )
{
	int frameBytes;
	drmp3_uint64 frames;

	if ( !stream || !buffer || bytes <= 0 )
		return 0;

	frameBytes = stream->info.channels * MP3_SAMPLEWIDTH;
	frames = drmp3_read_pcm_frames_s16( (drmp3 *)stream->ptr, bytes / frameBytes, (drmp3_int16 *)buffer );

	return (int)frames * frameBytes;
}

/*
=================
S_MP3_CodecLoad

Decodes the whole file into temporary hunk memory, which the caller frees.
=================
*/
void *S_MP3_CodecLoad( const char *filename, snd_info_t *info )
{
	snd_stream_t *stream;
	byte *buffer;
	int bytesRead;

	if ( !filename || !info )
		return NULL;

	stream = S_MP3_CodecOpenStream( filename );
	if ( !stream )
		return NULL;

	*info = stream->info;

	buffer = Hunk_AllocateTempMemory( info->size );
	if ( !buffer )
	{
		S_MP3_CodecCloseStream( stream );
		return NULL;
	}

	bytesRead = S_MP3_CodecReadStream( stream, info->size, buffer );
	if ( bytesRead <= 0 )
	{
		Hunk_FreeTempMemory( buffer );
		S_MP3_CodecCloseStream( stream );
		return NULL;
	}

	// a short read leaves the tail silent
	if ( bytesRead < info->size )
		Com_Memset( buffer + bytesRead, 0, info->size - bytesRead );

	S_MP3_CodecCloseStream( stream );

	return buffer;
}

#endif // USE_CODEC_MP3
