// SPDX-License-Identifier: GPL-3.0-or-later
// GLB Model Source - an FFGL source plugin for Resolume Arena
// Copyright (C) 2026 Elisa Bernard (Elisalien)

#include "GlbSource.h"
#include "ParamIds.h"
#include "Shaders.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <cstdio>
#include <cstring>

using namespace m3d;

static CFFGLPluginInfo PluginInfo(
	PluginFactory< GlbSource >,
	"EL3D",                                   // unique 4 character id
	"GLB Model Source",                       // name shown in Resolume
	2, 2,                                     // FFGL api version
	1, 0,                                     // plugin version
	FF_SOURCE,
	"Loads a .glb / .gltf model, decimates it and animates it. Every parameter can be driven by Resolume's audio analysis.",
	"Elisalien" );

namespace
{
GLuint CompileStage( GLenum type, const char* source, std::string& log )
{
	GLuint shader = glCreateShader( type );
	glShaderSource( shader, 1, &source, nullptr );
	glCompileShader( shader );

	GLint ok = GL_FALSE;
	glGetShaderiv( shader, GL_COMPILE_STATUS, &ok );
	if( ok != GL_TRUE )
	{
		GLint len = 0;
		glGetShaderiv( shader, GL_INFO_LOG_LENGTH, &len );
		std::vector< char > buffer( len > 1 ? len : 1, '\0' );
		glGetShaderInfoLog( shader, (GLsizei)buffer.size(), nullptr, buffer.data() );
		log.assign( buffer.data() );
		glDeleteShader( shader );
		return 0;
	}
	return shader;
}

GLuint LinkProgram( const char* vertex, const char* geometry, const char* fragment )
{
	std::string log;
	GLuint vs = CompileStage( GL_VERTEX_SHADER, vertex, log );
	if( vs == 0 )
	{
		FFGLLog::LogToHost( ( "GLB Source vertex shader: " + log ).c_str() );
		return 0;
	}
	GLuint gs = 0;
	if( geometry )
	{
		gs = CompileStage( GL_GEOMETRY_SHADER, geometry, log );
		if( gs == 0 )
		{
			FFGLLog::LogToHost( ( "GLB Source geometry shader: " + log ).c_str() );
			glDeleteShader( vs );
			return 0;
		}
	}
	GLuint fs = CompileStage( GL_FRAGMENT_SHADER, fragment, log );
	if( fs == 0 )
	{
		FFGLLog::LogToHost( ( "GLB Source fragment shader: " + log ).c_str() );
		glDeleteShader( vs );
		if( gs )
			glDeleteShader( gs );
		return 0;
	}

	GLuint program = glCreateProgram();
	glAttachShader( program, vs );
	if( gs )
		glAttachShader( program, gs );
	glAttachShader( program, fs );
	glLinkProgram( program );

	GLint ok = GL_FALSE;
	glGetProgramiv( program, GL_LINK_STATUS, &ok );
	if( ok != GL_TRUE )
	{
		GLint len = 0;
		glGetProgramiv( program, GL_INFO_LOG_LENGTH, &len );
		std::vector< char > buffer( len > 1 ? len : 1, '\0' );
		glGetProgramInfoLog( program, (GLsizei)buffer.size(), nullptr, buffer.data() );
		FFGLLog::LogToHost( ( std::string( "GLB Source link: " ) + buffer.data() ).c_str() );
		glDeleteProgram( program );
		program = 0;
	}

	if( program != 0 )
	{
		glDetachShader( program, vs );
		if( gs )
			glDetachShader( program, gs );
		glDetachShader( program, fs );
	}
	glDeleteShader( vs );
	if( gs )
		glDeleteShader( gs );
	glDeleteShader( fs );
	return program;
}
}//namespace

void GlbSource::AddFloatParam( unsigned int id, const char* name, float min, float max, float defaultValue, const char* group )
{
	SetParamInfo( id, name, FF_TYPE_STANDARD, defaultValue );
	SetParamRange( id, min, max );
	// SetParamInfo clamps the default of a standard param to 0..1, so restore it.
	ParamInfo* info = FindParamInfo( id );
	if( info != nullptr )
		info->defaultFloatVal = defaultValue;
	if( group )
		SetParamGroup( id, group );
}

void GlbSource::RegisterParameters()
{
	//--------------------------------------------------------------- model
	SetFileParamInfo( PID_FILE, "Model File", { "glb", "gltf" }, "" );
	SetParamGroup( PID_FILE, "Model" );

	SetParamInfo( PID_RELOAD, "Reload", FF_TYPE_EVENT, 0.0f );
	SetParamGroup( PID_RELOAD, "Model" );

	AddFloatParam( PID_DENSITY, "Mesh Density", 0.01f, 1.0f, 1.0f, "Model" );
	SetParamInfo( PID_AUTOFIT, "Auto Fit", FF_TYPE_BOOLEAN, 1.0f );
	SetParamGroup( PID_AUTOFIT, "Model" );

	//----------------------------------------------------------- transform
	AddFloatParam( PID_SCALE, "Scale", 0.0f, 4.0f, 1.0f, "Transform" );
	AddFloatParam( PID_ROT_X, "Rotation X", -180.0f, 180.0f, 0.0f, "Transform" );
	AddFloatParam( PID_ROT_Y, "Rotation Y", -180.0f, 180.0f, 0.0f, "Transform" );
	AddFloatParam( PID_ROT_Z, "Rotation Z", -180.0f, 180.0f, 0.0f, "Transform" );
	AddFloatParam( PID_SPIN_X, "Spin X", -2.0f, 2.0f, 0.0f, "Transform" );
	AddFloatParam( PID_SPIN_Y, "Spin Y", -2.0f, 2.0f, 0.0f, "Transform" );
	AddFloatParam( PID_SPIN_Z, "Spin Z", -2.0f, 2.0f, 0.0f, "Transform" );
	AddFloatParam( PID_OFFSET_X, "Position X", -2.0f, 2.0f, 0.0f, "Transform" );
	AddFloatParam( PID_OFFSET_Y, "Position Y", -2.0f, 2.0f, 0.0f, "Transform" );
	AddFloatParam( PID_DISTANCE, "Camera Distance", 0.5f, 12.0f, 3.0f, "Transform" );
	AddFloatParam( PID_FOV, "Field Of View", 10.0f, 120.0f, 45.0f, "Transform" );

	//----------------------------------------------------------- animation
	SetOptionParamInfo( PID_ANIM_CLIP, "Clip", MAX_ANIM_CLIPS, 0.0f );
	SetParamElementInfo( PID_ANIM_CLIP, 0, "None", 0.0f );
	for( int i = 1; i < MAX_ANIM_CLIPS; ++i )
	{
		char label[ 24 ];
		std::snprintf( label, sizeof( label ), "Clip %d", i );
		SetParamElementInfo( PID_ANIM_CLIP, i, label, (float)i );
	}
	SetParamGroup( PID_ANIM_CLIP, "Animation" );

	AddFloatParam( PID_ANIM_SPEED, "Anim Speed", -4.0f, 4.0f, 1.0f, "Animation" );
	SetParamInfo( PID_ANIM_PLAY, "Play", FF_TYPE_BOOLEAN, 1.0f );
	SetParamGroup( PID_ANIM_PLAY, "Animation" );
	SetParamInfo( PID_ANIM_RESET, "Restart", FF_TYPE_EVENT, 0.0f );
	SetParamGroup( PID_ANIM_RESET, "Animation" );

	//---------------------------------------------------------------- look
	SetOptionParamInfo( PID_MODE, "Render Mode", 4, 0.0f );
	SetParamElementInfo( PID_MODE, 0, "Shaded", 0.0f );
	SetParamElementInfo( PID_MODE, 1, "Flat Low Poly", 1.0f );
	SetParamElementInfo( PID_MODE, 2, "Unlit", 2.0f );
	SetParamElementInfo( PID_MODE, 3, "Wireframe Only", 3.0f );
	SetParamGroup( PID_MODE, "Render" );

	SetParamInfo( PID_WIRE_OVERLAY, "Wireframe", FF_TYPE_BOOLEAN, 0.0f );
	SetParamGroup( PID_WIRE_OVERLAY, "Render" );
	AddFloatParam( PID_WIRE_WIDTH, "Wire Width", 0.2f, 8.0f, 1.5f, "Render" );
	AddFloatParam( PID_WIRE_OPACITY, "Wire Opacity", 0.0f, 1.0f, 1.0f, "Render" );
	SetParamInfo( PID_POINT_OVERLAY, "Points", FF_TYPE_BOOLEAN, 0.0f );
	SetParamGroup( PID_POINT_OVERLAY, "Render" );
	AddFloatParam( PID_POINT_SIZE, "Point Size", 1.0f, 32.0f, 4.0f, "Render" );
	AddFloatParam( PID_DISPLACE, "Displace", -0.5f, 0.5f, 0.0f, "Render" );
	SetParamInfo( PID_CULL, "Backface Cull", FF_TYPE_BOOLEAN, 0.0f );
	SetParamGroup( PID_CULL, "Render" );

	SetOptionParamInfo( PID_AA, "Anti Alias", 4, 2.0f );
	SetParamElementInfo( PID_AA, 0, "Off", 0.0f );
	SetParamElementInfo( PID_AA, 1, "2x", 1.0f );
	SetParamElementInfo( PID_AA, 2, "4x", 2.0f );
	SetParamElementInfo( PID_AA, 3, "8x", 3.0f );
	SetParamGroup( PID_AA, "Render" );

	//------------------------------------------------------------ material
	SetParamInfo( PID_HUE, "Tint", FF_TYPE_HUE, 0.0f );
	SetParamGroup( PID_HUE, "Material" );
	SetParamInfo( PID_SATURATION, "Tint Saturation", FF_TYPE_SATURATION, 0.0f );
	SetParamGroup( PID_SATURATION, "Material" );
	SetParamInfo( PID_BRIGHTNESS, "Tint Brightness", FF_TYPE_BRIGHTNESS, 1.0f );
	SetParamGroup( PID_BRIGHTNESS, "Material" );
	SetParamInfo( PID_ALPHA, "Opacity", FF_TYPE_ALPHA, 1.0f );
	SetParamGroup( PID_ALPHA, "Material" );

	AddFloatParam( PID_LIGHT_YAW, "Light Yaw", 0.0f, 1.0f, 0.35f, "Material" );
	AddFloatParam( PID_LIGHT_PITCH, "Light Pitch", 0.0f, 1.0f, 0.65f, "Material" );
	AddFloatParam( PID_LIGHT_INTENSITY, "Light Intensity", 0.0f, 4.0f, 1.0f, "Material" );
	AddFloatParam( PID_AMBIENT, "Ambient", 0.0f, 2.0f, 0.25f, "Material" );
	AddFloatParam( PID_METALLIC, "Metallic", 0.0f, 2.0f, 1.0f, "Material" );
	AddFloatParam( PID_ROUGHNESS, "Roughness", 0.0f, 2.0f, 1.0f, "Material" );
	AddFloatParam( PID_BG_ALPHA, "Background", 0.0f, 1.0f, 0.0f, "Material" );

	//--------------------------------------------------------------- audio
	SetBufferParamInfo( PID_FFT, "FFT", FFT_BINS, FF_USAGE_FFT );
	for( unsigned int i = 0; i < FFT_BINS; ++i )
		SetParamElementInfo( PID_FFT, i, "", 0.0f );
	SetParamGroup( PID_FFT, "Audio" );

	SetOptionParamInfo( PID_AUDIO_BAND, "Band", 4, 1.0f );
	SetParamElementInfo( PID_AUDIO_BAND, 0, "Volume", 0.0f );
	SetParamElementInfo( PID_AUDIO_BAND, 1, "Low", 1.0f );
	SetParamElementInfo( PID_AUDIO_BAND, 2, "Mid", 2.0f );
	SetParamElementInfo( PID_AUDIO_BAND, 3, "High", 3.0f );
	SetParamGroup( PID_AUDIO_BAND, "Audio" );

	AddFloatParam( PID_AUDIO_GAIN, "Audio Gain", 0.0f, 8.0f, 1.0f, "Audio" );
	AddFloatParam( PID_AUDIO_SMOOTH, "Audio Smooth", 0.0f, 0.99f, 0.55f, "Audio" );
	AddFloatParam( PID_AUDIO_SCALE, "Audio To Scale", 0.0f, 2.0f, 0.0f, "Audio" );
	AddFloatParam( PID_AUDIO_SPIN, "Audio To Spin", 0.0f, 4.0f, 0.0f, "Audio" );
	AddFloatParam( PID_AUDIO_ANIM, "Audio To Anim", 0.0f, 4.0f, 0.0f, "Audio" );
	AddFloatParam( PID_AUDIO_DISPLACE, "Audio To Displace", 0.0f, 1.0f, 0.0f, "Audio" );
}

GlbSource::GlbSource()
{
	SetMinInputs( 0 );
	SetMaxInputs( 0 );
	RegisterParameters();
	FFGLLog::LogToHost( "Created GLB Model Source" );
}

GlbSource::~GlbSource() = default;

bool GlbSource::BuildPrograms()
{
	solidProgram = LinkProgram( shaders::kModelVertex, nullptr, shaders::kModelFragment );
	wireProgram  = LinkProgram( shaders::kModelVertex, shaders::kWireGeometry, shaders::kWireFragment );
	pointProgram = LinkProgram( shaders::kModelVertex, nullptr, shaders::kPointFragment );
	quadProgram  = LinkProgram( shaders::kQuadVertex, nullptr, shaders::kQuadFragment );

	if( !solidProgram || !wireProgram || !pointProgram || !quadProgram )
		return false;

	static const float quadVertices[] = {
		-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f//oversized triangle, covers the screen
	};
	glGenVertexArrays( 1, &quadVao );
	glGenBuffers( 1, &quadVbo );
	glBindVertexArray( quadVao );
	glBindBuffer( GL_ARRAY_BUFFER, quadVbo );
	glBufferData( GL_ARRAY_BUFFER, sizeof( quadVertices ), quadVertices, GL_STATIC_DRAW );
	glEnableVertexAttribArray( 0 );
	glVertexAttribPointer( 0, 2, GL_FLOAT, GL_FALSE, sizeof( float ) * 2, (void*)0 );
	glBindVertexArray( 0 );
	glBindBuffer( GL_ARRAY_BUFFER, 0 );

	// 1x1 white pixel, bound to every unused sampler so the shaders stay valid.
	const unsigned char white[ 4 ] = { 255, 255, 255, 255 };
	glGenTextures( 1, &whiteTexture );
	glBindTexture( GL_TEXTURE_2D, whiteTexture );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glBindTexture( GL_TEXTURE_2D, 0 );

	programsReady = true;
	return true;
}

void GlbSource::ReleasePrograms()
{
	if( solidProgram ) glDeleteProgram( solidProgram );
	if( wireProgram )  glDeleteProgram( wireProgram );
	if( pointProgram ) glDeleteProgram( pointProgram );
	if( quadProgram )  glDeleteProgram( quadProgram );
	solidProgram = wireProgram = pointProgram = quadProgram = 0;

	if( quadVao ) glDeleteVertexArrays( 1, &quadVao );
	if( quadVbo ) glDeleteBuffers( 1, &quadVbo );
	if( whiteTexture ) glDeleteTextures( 1, &whiteTexture );
	quadVao = quadVbo = whiteTexture = 0;
	programsReady = false;
}

FFResult GlbSource::InitGL( const FFGLViewportStruct* vp )
{
#if !defined( __APPLE__ )
	// Apple's gl3.h exposes 4.1 core directly; everywhere else the entry points
	// have to be resolved first.
	glewExperimental = GL_TRUE;
	glewInit();
	glGetError();//glewInit leaves an error behind on core profiles
#endif

	if( !BuildPrograms() )
	{
		ReleasePrograms();
		return FF_FAIL;
	}

	lastHostTime = (float)( hostTime / 1000.0 );
	if( !filePath.empty() )
		loadRequested = true;

	return CFFGLPlugin::InitGL( vp );
}

FFResult GlbSource::DeInitGL()
{
	model.Release();
	ReleaseRenderTarget();
	ReleasePrograms();
	return FF_SUCCESS;
}

void GlbSource::ReleaseRenderTarget()
{
	if( target.fbo )          glDeleteFramebuffers( 1, &target.fbo );
	if( target.resolveFbo )   glDeleteFramebuffers( 1, &target.resolveFbo );
	if( target.color )        glDeleteRenderbuffers( 1, &target.color );
	if( target.depth )        glDeleteRenderbuffers( 1, &target.depth );
	if( target.resolveColor ) glDeleteTextures( 1, &target.resolveColor );
	target = RenderTarget();
}

bool GlbSource::EnsureRenderTarget( int width, int height, int samples )
{
	if( width <= 0 || height <= 0 )
		return false;
	if( target.fbo && target.width == width && target.height == height && target.samples == samples )
		return true;

	ReleaseRenderTarget();

	GLint maxSamples = 0;
	glGetIntegerv( GL_MAX_SAMPLES, &maxSamples );
	if( samples > maxSamples )
		samples = maxSamples;
	if( samples < 0 )
		samples = 0;

	// Offscreen pass: the host's framebuffer has no depth attachment, so 3D
	// geometry has to be rendered into our own and then blitted back.
	glGenFramebuffers( 1, &target.fbo );
	glBindFramebuffer( GL_FRAMEBUFFER, target.fbo );

	glGenRenderbuffers( 1, &target.color );
	glBindRenderbuffer( GL_RENDERBUFFER, target.color );
	if( samples > 0 )
		glRenderbufferStorageMultisample( GL_RENDERBUFFER, samples, GL_RGBA8, width, height );
	else
		glRenderbufferStorage( GL_RENDERBUFFER, GL_RGBA8, width, height );
	glFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, target.color );

	glGenRenderbuffers( 1, &target.depth );
	glBindRenderbuffer( GL_RENDERBUFFER, target.depth );
	if( samples > 0 )
		glRenderbufferStorageMultisample( GL_RENDERBUFFER, samples, GL_DEPTH24_STENCIL8, width, height );
	else
		glRenderbufferStorage( GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height );
	glFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, target.depth );

	const bool complete = glCheckFramebufferStatus( GL_FRAMEBUFFER ) == GL_FRAMEBUFFER_COMPLETE;

	// Resolve target, also what we sample when drawing into the host fbo.
	glGenFramebuffers( 1, &target.resolveFbo );
	glBindFramebuffer( GL_FRAMEBUFFER, target.resolveFbo );
	glGenTextures( 1, &target.resolveColor );
	glBindTexture( GL_TEXTURE_2D, target.resolveColor );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.resolveColor, 0 );
	const bool resolveComplete = glCheckFramebufferStatus( GL_FRAMEBUFFER ) == GL_FRAMEBUFFER_COMPLETE;

	glBindFramebuffer( GL_FRAMEBUFFER, 0 );
	glBindRenderbuffer( GL_RENDERBUFFER, 0 );
	glBindTexture( GL_TEXTURE_2D, 0 );

	if( !complete || !resolveComplete )
	{
		ReleaseRenderTarget();
		if( samples > 0 )
			return EnsureRenderTarget( width, height, 0 );//retry without msaa
		return false;
	}

	target.width   = width;
	target.height  = height;
	target.samples = samples;
	return true;
}

void GlbSource::PublishClipNames()
{
	const std::vector< GltfModel::Animation >& clips = model.Animations();
	const int count = std::min( (int)clips.size(), MAX_ANIM_CLIPS - 1 );
	if( count == publishedClipCount )
		return;

	std::vector< std::string > names;
	std::vector< float > values;
	names.push_back( "None" );
	values.push_back( 0.0f );
	for( int i = 0; i < count; ++i )
	{
		names.push_back( clips[ i ].name );
		values.push_back( (float)( i + 1 ) );
	}
	SetParamElements( PID_ANIM_CLIP, names, values, true );
	publishedClipCount = count;

	if( pAnimClip > (float)count )
		pAnimClip = 0.0f;
}

void GlbSource::ProcessPendingLoad()
{
	std::string wanted;
	{
		std::lock_guard< std::mutex > lock( fileMutex );
		if( !loadRequested )
			return;
		loadRequested = false;
		wanted        = filePath;
	}

	model.Release();
	if( wanted.empty() )
	{
		model.Clear();
		loadedPath.clear();
		publishedClipCount = -1;
		PublishClipNames();
		return;
	}

	if( !model.Load( wanted ) )
	{
		FFGLLog::LogToHost( ( "GLB Source: " + model.LastError() ).c_str() );
		loadedPath.clear();
		publishedClipCount = -1;
		PublishClipNames();
		return;
	}

	model.Upload();
	loadedPath      = wanted;
	appliedDensity  = 1.0f;
	animTime        = 0.0f;
	publishedClipCount = -1;
	PublishClipNames();

	char message[ 320 ];
	std::snprintf( message, sizeof( message ), "GLB Source: loaded %s (%d triangles, %d clips)",
	               wanted.c_str(), (int)model.TriangleCountFull(), (int)model.Animations().size() );
	FFGLLog::LogToHost( message );
}

void GlbSource::UpdateAudio( float deltaTime )
{
	const ParamInfo* info = FindParamInfo( PID_FFT );
	if( info == nullptr || info->elements.empty() )
	{
		audioRaw = 0.0f;
	}
	else
	{
		const size_t bins = info->elements.size();
		const float nyquist  = ( sampleRate > 0 ? (float)sampleRate : 44100.0f ) * 0.5f;
		const float binWidth = nyquist / (float)bins;

		float fromHz = 0.0f, toHz = nyquist;
		switch( (int)( pAudioBand + 0.5f ) )
		{
		case 1: fromHz = 0.0f;    toHz = 200.0f;   break;//low
		case 2: fromHz = 200.0f;  toHz = 2000.0f;  break;//mid
		case 3: fromHz = 2000.0f; toHz = nyquist;  break;//high
		default: break;                                  //full volume
		}

		size_t first = (size_t)std::floor( fromHz / binWidth );
		size_t last  = (size_t)std::ceil( toHz / binWidth );
		first = std::min( first, bins - 1 );
		last  = std::min( std::max( last, first + 1 ), bins );

		float sum = 0.0f;
		for( size_t i = first; i < last; ++i )
			sum += std::fabs( info->elements[ i ].value );
		audioRaw = sum / (float)( last - first );
	}

	float level = clampf( audioRaw * pAudioGain, 0.0f, 1.0f );

	// Fast attack, parameter-controlled release: peaks stay punchy on a kick.
	const float smooth = clampf( pAudioSmooth, 0.0f, 0.99f );
	const float rate   = 1.0f - std::pow( smooth, std::max( deltaTime, 1.0f / 240.0f ) * 60.0f );
	if( level > audioLevel )
		audioLevel = level;
	else
		audioLevel += ( level - audioLevel ) * clampf( rate, 0.02f, 1.0f );
}

Mat4 GlbSource::BaseTransform() const
{
	Mat4 fit;
	if( pAutoFit > 0.5f )
	{
		const float radius = std::max( model.BoundsRadius(), 1e-4f );
		fit = scale( Vec3( 1.0f / radius, 1.0f / radius, 1.0f / radius ) ) *
		      translate( Vec3( -model.BoundsCenter().x, -model.BoundsCenter().y, -model.BoundsCenter().z ) );
	}

	const float audioScale = 1.0f + audioLevel * pAudioScale;
	const float s          = std::max( pScale * audioScale, 0.0001f );

	const Mat4 spin = rotateY( spinPhaseY ) * rotateX( spinPhaseX ) * rotateZ( spinPhaseZ );
	const Mat4 user = rotateY( radians( pRotY ) ) * rotateX( radians( pRotX ) ) * rotateZ( radians( pRotZ ) );

	return translate( Vec3( pOffsetX, pOffsetY, 0.0f ) ) * scale( Vec3( s, s, s ) ) * spin * user * fit;
}
