/*
===========================================================================
r_gamma (the options menu's "Brightness Slider", ui/options.urc) applied by
the renderer to the finished frame.

Retail FAKK2 builds a 256-entry table from r_gamma and the overbright bits
in R_SetColorMappings (fakk2.exe 0x46ff70, ours in tr_image.c) and gives it
to the display's gamma ramp (GLimp_SetGamma, 0x48ad00), or bakes it into
every texture when there is no ramp. A ramp now changes
the whole desktop's brightness or nothing at all, depending on the driver,
so this renderer applies the same table itself: at the end of each frame
the back buffer is copied to a texture and drawn back through a fragment
shader that looks each channel up in the table (a 256 x 1 texture, read
nearest, so every value maps exactly as the ramp would have mapped it).

The pass doesn't run while the table is the identity (r_gamma 1 and no
overbright bits, the defaults), so the picture there is the one drawn.
It needs GLSL (OpenGL 2.0) and two texture units; without them, or with
r_gammaShader 0, the display's ramp is used as retail did. Never both: the
ramp is left alone while the pass is in use.

The game draws as though the ramp were there (glConfig.deviceSupportsGamma
is set): textures aren't gamma-baked, and overbright bits are allowed in
fullscreen as retail allowed them with a ramp.
===========================================================================
*/

#include "tr_local.h"

static qboolean	gammaPassReady;			// shader built in the current GL context
static qboolean	gammaPassIdentity = qtrue;	// the table maps every value to itself
static qboolean	gammaInFramebuffer;		// the back buffer holds a frame the pass has run on
static GLuint	gammaProgram;
static GLuint	gammaTableTexture;
static GLuint	gammaScreenTexture;
static int		gammaScreenWidth, gammaScreenHeight;

static const char *gammaVertexShader =
	"#version 110\n"
	"void main() {\n"
	"	gl_Position = gl_Vertex;\n"
	"	gl_TexCoord[0] = gl_MultiTexCoord0;\n"
	"}\n";

// a channel's 8-bit value v reads as v / 255; (v + 0.5) / 256 is the
// centre of the table's texel v
static const char *gammaFragmentShader =
	"#version 110\n"
	"uniform sampler2D u_Screen;\n"
	"uniform sampler2D u_Table;\n"
	"void main() {\n"
	"	vec4 c = texture2D( u_Screen, gl_TexCoord[0].st );\n"
	"	vec3 i = ( c.rgb * 255.0 + 0.5 ) / 256.0;\n"
	"	gl_FragColor = vec4( texture2D( u_Table, vec2( i.r, 0.5 ) ).r,\n"
	"		texture2D( u_Table, vec2( i.g, 0.5 ) ).r,\n"
	"		texture2D( u_Table, vec2( i.b, 0.5 ) ).r, c.a );\n"
	"}\n";

/*
=================
R_CompileGammaShader
=================
*/
static GLuint R_CompileGammaShader( GLenum type, const char *source ) {
	GLuint	shader;
	GLint	compiled;
	char	log[1024];

	shader = qglCreateShader( type );
	qglShaderSource( shader, 1, &source, NULL );
	qglCompileShader( shader );
	qglGetShaderiv( shader, GL_COMPILE_STATUS, &compiled );
	if ( !compiled ) {
		log[0] = 0;
		qglGetShaderInfoLog( shader, sizeof( log ), NULL, log );
		ri.Printf( PRINT_ALL, "WARNING: gamma shader didn't compile: %s\n", log );
		qglDeleteShader( shader );
		return 0;
	}
	return shader;
}

/*
=================
R_ForgetGammaTextures

The pass binds its textures behind GL_Bind's back; make GL_Bind bind again.
=================
*/
static void R_ForgetGammaTextures( void ) {
	GL_SelectTexture( 1 );
	qglBindTexture( GL_TEXTURE_2D, 0 );
	glState.currenttextures[1] = 0;
	GL_SelectTexture( 0 );
	qglBindTexture( GL_TEXTURE_2D, 0 );
	glState.currenttextures[0] = 0;
}

/*
=================
R_InitGammaPass

Called once a GL context is made, before R_SetColorMappings.
=================
*/
void R_InitGammaPass( void ) {
	GLuint	vertexShader, fragmentShader;
	GLint	linked;
	byte	identity[256];
	int		i;

	gammaPassReady = qfalse;
	gammaPassIdentity = qtrue;
	gammaInFramebuffer = qfalse;
	gammaScreenWidth = gammaScreenHeight = 0;

	if ( !r_gammaShader->integer ) {
		return;
	}
	if ( !qglCreateShader || !qglActiveTextureARB || glConfig.numTextureUnits < 2 ) {
		ri.Printf( PRINT_ALL, "...no GLSL or multitexture, r_gamma goes on the display's gamma ramp\n" );
		return;
	}

	vertexShader = R_CompileGammaShader( GL_VERTEX_SHADER, gammaVertexShader );
	fragmentShader = R_CompileGammaShader( GL_FRAGMENT_SHADER, gammaFragmentShader );
	if ( !vertexShader || !fragmentShader ) {
		if ( vertexShader ) {
			qglDeleteShader( vertexShader );
		}
		if ( fragmentShader ) {
			qglDeleteShader( fragmentShader );
		}
		return;
	}

	gammaProgram = qglCreateProgram();
	qglAttachShader( gammaProgram, vertexShader );
	qglAttachShader( gammaProgram, fragmentShader );
	qglLinkProgram( gammaProgram );
	qglDeleteShader( vertexShader );
	qglDeleteShader( fragmentShader );
	qglGetProgramiv( gammaProgram, GL_LINK_STATUS, &linked );
	if ( !linked ) {
		char	log[1024];

		log[0] = 0;
		qglGetProgramInfoLog( gammaProgram, sizeof( log ), NULL, log );
		ri.Printf( PRINT_ALL, "WARNING: gamma shader didn't link: %s\n", log );
		qglDeleteProgram( gammaProgram );
		gammaProgram = 0;
		return;
	}

	qglUseProgram( gammaProgram );
	qglUniform1i( qglGetUniformLocation( gammaProgram, "u_Screen" ), 0 );
	qglUniform1i( qglGetUniformLocation( gammaProgram, "u_Table" ), 1 );
	qglUseProgram( 0 );

	for ( i = 0 ; i < 256 ; i++ ) {
		identity[i] = i;
	}

	qglGenTextures( 1, &gammaTableTexture );
	qglGenTextures( 1, &gammaScreenTexture );

	GL_SelectTexture( 0 );
	qglBindTexture( GL_TEXTURE_2D, gammaTableTexture );
	qglTexImage2D( GL_TEXTURE_2D, 0, GL_LUMINANCE8, 256, 1, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, identity );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );

	qglBindTexture( GL_TEXTURE_2D, gammaScreenTexture );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );

	R_ForgetGammaTextures();

	gammaPassReady = qtrue;
	glConfig.deviceSupportsGamma = qtrue;
}

/*
=================
R_ShutdownGammaPass

Before the GL context goes.
=================
*/
void R_ShutdownGammaPass( void ) {
	if ( gammaPassReady ) {
		qglDeleteProgram( gammaProgram );
		qglDeleteTextures( 1, &gammaTableTexture );
		qglDeleteTextures( 1, &gammaScreenTexture );
	}
	gammaProgram = 0;
	gammaTableTexture = gammaScreenTexture = 0;
	gammaPassReady = qfalse;
	gammaPassIdentity = qtrue;
	gammaInFramebuffer = qfalse;
}

/*
=================
R_GammaPassActive

True when the pass, not the display's ramp, owns r_gamma.
=================
*/
qboolean R_GammaPassActive( void ) {
	return gammaPassReady;
}

/*
=================
R_SetGammaPassTable
=================
*/
void R_SetGammaPassTable( const byte table[256] ) {
	int		i;

	if ( !gammaPassReady ) {
		return;
	}

	gammaPassIdentity = qtrue;
	for ( i = 0 ; i < 256 ; i++ ) {
		if ( table[i] != i ) {
			gammaPassIdentity = qfalse;
			break;
		}
	}

	GL_SelectTexture( 0 );
	qglBindTexture( GL_TEXTURE_2D, gammaTableTexture );
	qglTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_LUMINANCE, GL_UNSIGNED_BYTE, table );
	qglBindTexture( GL_TEXTURE_2D, 0 );
	glState.currenttextures[0] = 0;
}

/*
=================
RB_GammaPass

The frame's last drawing, before the buffer swap.
=================
*/
void RB_GammaPass( void ) {
	int		width, height;

	if ( !gammaPassReady || gammaPassIdentity ) {
		return;
	}

	width = glConfig.vidWidth;
	height = glConfig.vidHeight;

	GL_SelectTexture( 1 );
	qglBindTexture( GL_TEXTURE_2D, gammaTableTexture );
	GL_SelectTexture( 0 );
	qglBindTexture( GL_TEXTURE_2D, gammaScreenTexture );
	if ( width != gammaScreenWidth || height != gammaScreenHeight ) {
		qglTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL );
		gammaScreenWidth = width;
		gammaScreenHeight = height;
	}
	qglCopyTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height );

	qglViewport( 0, 0, width, height );
	qglScissor( 0, 0, width, height );
	qglColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
	GL_State( GLS_DEPTHTEST_DISABLE );
	GL_Cull( CT_TWO_SIDED );

	qglUseProgram( gammaProgram );
	qglBegin( GL_QUADS );
	qglTexCoord2f( 0, 0 );
	qglVertex2f( -1, -1 );
	qglTexCoord2f( 1, 0 );
	qglVertex2f( 1, -1 );
	qglTexCoord2f( 1, 1 );
	qglVertex2f( 1, 1 );
	qglTexCoord2f( 0, 1 );
	qglVertex2f( -1, 1 );
	qglEnd();
	qglUseProgram( 0 );

	R_ForgetGammaTextures();

	gammaInFramebuffer = qtrue;
}

/*
=================
RB_GammaPassNewFrame

The back buffer is being drawn again: what is in it has no gamma yet.
=================
*/
void RB_GammaPassNewFrame( void ) {
	gammaInFramebuffer = qfalse;
}

/*
=================
RB_FramebufferNeedsGamma

Whether a picture read back from the back buffer (a screenshot, a save's
picture, a levelshot, a video frame) still needs the gamma table to look
as the frame did on screen. A console screenshot reads the buffer before
the next frame draws, after the pass; a save's picture is taken inside
the frame, before it.
=================
*/
qboolean RB_FramebufferNeedsGamma( void ) {
	return glConfig.deviceSupportsGamma && !gammaInFramebuffer;
}
