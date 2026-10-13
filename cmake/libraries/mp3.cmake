if(NOT USE_CODEC_MP3)
    return()
endif()

if(NOT BUILD_CLIENT)
    return()
endif()

# dr_mp3 is a single header under thirdparty/dr_libs, included by
# client/snd_codec_mp3.c; nothing to build here
list(APPEND CLIENT_DEFINITIONS USE_CODEC_MP3)
