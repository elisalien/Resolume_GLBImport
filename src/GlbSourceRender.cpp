// SPDX-License-Identifier: GPL-3.0-or-later
// GLB Model Source - an FFGL source plugin for Resolume Arena
// Copyright (C) 2026 Elisa Bernard (Elisalien)

// Rendering half of the plugin: draw passes, the offscreen target blit,
// the per-frame update, and the host parameter plumbing.
#include "GlbSource.h"
#include "ParamIds.h"
#include "Shaders.h"
#include "ColorUtils.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <cstdio>
#include <cstring>

using namespace m3d;

void GlbSource::DrawPrimitives( GLuint program, int passMode )
{
	const Mat4 base = BaseTransform();
	const std::vector< GltfModel::Primitive >& primitives = model.Primitives();
	const std::vector< GltfModel::Material >& materials   = model.Materials();
	const std::vector< GltfModel::Node >& nodes           = model.Nodes();

	const GLint locModel     = glGetUniformLocation( program, "uModel" );
	const GLint locJoints    = glGetUniformLocation( program, "uJoints" );
	const GLint locSkinned   = glGetUniformLocation( program, "uSkinned" );
	const GLint locBaseF     = glGetUniformLocation( program, "uBaseFactor" );
	const GLint locEmissiveF = glGetUniformLocation( program, "uEmissiveFactor" );
	const GLint locMetal     = glGetUniformLocation( program, "uMetallic" );
	const GLint locRough     = glGetUniformLocation( program, "uRoughness" );
	const GLint locAlphaMode = glGetUniformLocation( program, "uAlphaMode" );
	const GLint locCutoff    = glGetUniformLocation( program, "uAlphaCutoff" );
	const GLint locHasBase   = glGetUniformLocation( program, "uHasBaseColor" );
	const GLint locHasNrm    = glGetUniformLocation( program, "uHasNormalMap" );
	const GLint locHasMR     = glGetUniformLocation( program, "uHasMetalRough" );
	const GLint locHasEmi    = glGetUniformLocation( program, "uHasEmissive" );

	const GltfModel::Material fallback;

	for( const GltfModel::Primitive& prim : primitives )
	{
		if( prim.activeIndexCount < 3 || prim.vao == 0 )
			continue;

		// A skinned primitive is placed by its joints, so its own node transform
		// must be ignored (glTF spec). Everything else uses its node world matrix.
		const bool skinned = prim.skin >= 0 && model.JointCount( prim.skin ) > 0;
		Mat4 modelMatrix   = base;
		if( !skinned && prim.node >= 0 && prim.node < (int)nodes.size() )
			modelMatrix = base * nodes[ prim.node ].global;

		if( locModel >= 0 )
			glUniformMatrix4fv( locModel, 1, GL_FALSE, modelMatrix.m );
		if( locSkinned >= 0 )
			glUniform1i( locSkinned, skinned ? 1 : 0 );
		if( skinned && locJoints >= 0 )
		{
			const int count = std::min( model.JointCount( prim.skin ), (int)GltfModel::MAX_JOINTS );
			glUniformMatrix4fv( locJoints, count, GL_FALSE, model.JointData( prim.skin ) );
		}

		const GltfModel::Material& mat =
			( prim.material >= 0 && prim.material < (int)materials.size() ) ? materials[ prim.material ] : fallback;

		if( locBaseF >= 0 )
			glUniform4fv( locBaseF, 1, mat.baseColor );
		if( locEmissiveF >= 0 )
			glUniform3fv( locEmissiveF, 1, mat.emissive );
		if( locMetal >= 0 )
			glUniform1f( locMetal, mat.metallic * pMetallic );
		if( locRough >= 0 )
			glUniform1f( locRough, mat.roughness * pRoughness );
		if( locAlphaMode >= 0 )
			glUniform1i( locAlphaMode, mat.alphaMode );
		if( locCutoff >= 0 )
			glUniform1f( locCutoff, mat.alphaCutoff );

		const GLuint texBase = model.TextureId( mat.texBaseColor );
		const GLuint texNrm  = model.TextureId( mat.texNormal );
		const GLuint texMR   = model.TextureId( mat.texMetalRough );
		const GLuint texEmi  = model.TextureId( mat.texEmissive );

		glActiveTexture( GL_TEXTURE0 );
		glBindTexture( GL_TEXTURE_2D, texBase ? texBase : whiteTexture );
		glActiveTexture( GL_TEXTURE1 );
		glBindTexture( GL_TEXTURE_2D, texNrm ? texNrm : whiteTexture );
		glActiveTexture( GL_TEXTURE2 );
		glBindTexture( GL_TEXTURE_2D, texMR ? texMR : whiteTexture );
		glActiveTexture( GL_TEXTURE3 );
		glBindTexture( GL_TEXTURE_2D, texEmi ? texEmi : whiteTexture );
		glActiveTexture( GL_TEXTURE0 );

		if( locHasBase >= 0 ) glUniform1i( locHasBase, texBase ? 1 : 0 );
		if( locHasNrm >= 0 )  glUniform1i( locHasNrm, texNrm ? 1 : 0 );
		if( locHasMR >= 0 )   glUniform1i( locHasMR, texMR ? 1 : 0 );
		if( locHasEmi >= 0 )  glUniform1i( locHasEmi, texEmi ? 1 : 0 );

		if( pCull > 0.5f && !mat.doubleSided && passMode == 0 )
			glEnable( GL_CULL_FACE );
		else
			glDisable( GL_CULL_FACE );

		glBindVertexArray( prim.vao );
		glDrawElements( passMode == 2 ? GL_POINTS : GL_TRIANGLES,
		                prim.activeIndexCount, GL_UNSIGNED_INT, nullptr );
		glBindVertexArray( 0 );
	}
	glDisable( GL_CULL_FACE );
}

void GlbSource::RenderModel( int width, int height )
{
	const float aspect = height > 0 ? (float)width / (float)height : 1.0f;
	const Mat4 proj    = perspective( radians( pFov ), aspect, 0.01f, 200.0f );
	const Vec3 eye( 0.0f, 0.0f, pDistance );
	const Mat4 view = lookAt( eye, Vec3( 0.0f, 0.0f, 0.0f ), Vec3( 0.0f, 1.0f, 0.0f ) );

	const float yaw     = pLightYaw * 2.0f * PI;
	const float pitch   = ( pLightPitch - 0.5f ) * PI;
	const Vec3 lightDir = normalize( Vec3( -std::cos( pitch ) * std::sin( yaw ),
	                                       -std::sin( pitch ),
	                                       -std::cos( pitch ) * std::cos( yaw ) ) );

	const Vec3 tint      = HsbToLinearRgb( pHue, pSaturation, pBrightness );
	const float displace = pDisplace + audioLevel * pAudioDisplace;

	const int mode         = (int)( pMode + 0.5f );
	const bool drawSurface = mode != 3;
	const bool drawWire    = mode == 3 || pWireOverlay > 0.5f;
	const bool drawPoints  = pPointOverlay > 0.5f;

	auto setSharedUniforms = [ & ]( GLuint program ) {
		GLint loc;
		if( ( loc = glGetUniformLocation( program, "uView" ) ) >= 0 )
			glUniformMatrix4fv( loc, 1, GL_FALSE, view.m );
		if( ( loc = glGetUniformLocation( program, "uProj" ) ) >= 0 )
			glUniformMatrix4fv( loc, 1, GL_FALSE, proj.m );
		if( ( loc = glGetUniformLocation( program, "uDisplace" ) ) >= 0 )
			glUniform1f( loc, displace );
		if( ( loc = glGetUniformLocation( program, "uPointSize" ) ) >= 0 )
			glUniform1f( loc, pPointSize );
		if( ( loc = glGetUniformLocation( program, "uCameraPos" ) ) >= 0 )
			glUniform3f( loc, eye.x, eye.y, eye.z );
		if( ( loc = glGetUniformLocation( program, "uBaseColorMap" ) ) >= 0 )
			glUniform1i( loc, 0 );
		if( ( loc = glGetUniformLocation( program, "uNormalMap" ) ) >= 0 )
			glUniform1i( loc, 1 );
		if( ( loc = glGetUniformLocation( program, "uMetalRoughMap" ) ) >= 0 )
			glUniform1i( loc, 2 );
		if( ( loc = glGetUniformLocation( program, "uEmissiveMap" ) ) >= 0 )
			glUniform1i( loc, 3 );
	};

	//----------------------------------------------------------- surface pass
	if( drawSurface )
	{
		glUseProgram( solidProgram );
		setSharedUniforms( solidProgram );

		GLint loc;
		if( ( loc = glGetUniformLocation( solidProgram, "uMode" ) ) >= 0 )
			glUniform1i( loc, mode );
		if( ( loc = glGetUniformLocation( solidProgram, "uTint" ) ) >= 0 )
			glUniform4f( loc, tint.x, tint.y, tint.z, pAlpha );
		if( ( loc = glGetUniformLocation( solidProgram, "uLightDir" ) ) >= 0 )
			glUniform3f( loc, lightDir.x, lightDir.y, lightDir.z );
		if( ( loc = glGetUniformLocation( solidProgram, "uLightColor" ) ) >= 0 )
		{
			const float i = pLightIntensity * 3.0f;
			glUniform3f( loc, i, i, i );
		}
		if( ( loc = glGetUniformLocation( solidProgram, "uAmbientTop" ) ) >= 0 )
			glUniform3f( loc, pAmbient, pAmbient * 1.02f, pAmbient * 1.12f );
		if( ( loc = glGetUniformLocation( solidProgram, "uAmbientBottom" ) ) >= 0 )
			glUniform3f( loc, pAmbient * 0.35f, pAmbient * 0.33f, pAmbient * 0.30f );

		glEnable( GL_DEPTH_TEST );
		glDepthFunc( GL_LEQUAL );
		glDepthMask( GL_TRUE );
		glDisable( GL_BLEND );
		DrawPrimitives( solidProgram, 0 );
	}

	//--------------------------------------------------------- wireframe pass
	if( drawWire )
	{
		glUseProgram( wireProgram );
		setSharedUniforms( wireProgram );

		GLint loc;
		if( ( loc = glGetUniformLocation( wireProgram, "uWireColor" ) ) >= 0 )
		{
			// Lines are a UI colour, so they go out in display space untouched.
			const bool tinted = pSaturation > 0.001f;
			const float r = tinted ? std::pow( tint.x, 1.0f / 2.2f ) : 1.0f;
			const float g = tinted ? std::pow( tint.y, 1.0f / 2.2f ) : 1.0f;
			const float b = tinted ? std::pow( tint.z, 1.0f / 2.2f ) : 1.0f;
			glUniform4f( loc, r, g, b, pWireOpacity * pAlpha );
		}
		if( ( loc = glGetUniformLocation( wireProgram, "uWireWidth" ) ) >= 0 )
			glUniform1f( loc, pWireWidth );

		glEnable( GL_DEPTH_TEST );
		glDepthFunc( GL_LEQUAL );
		glDepthMask( drawSurface ? GL_FALSE : GL_TRUE );
		if( drawSurface )
		{
			// Pull the lines toward the camera so the surface does not z-fight them.
			glEnable( GL_POLYGON_OFFSET_FILL );
			glPolygonOffset( -1.0f, -1.0f );
		}
		glEnable( GL_BLEND );
		glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
		DrawPrimitives( wireProgram, 1 );
		glDisable( GL_POLYGON_OFFSET_FILL );
		glPolygonOffset( 0.0f, 0.0f );
	}

	//------------------------------------------------------------ point pass
	if( drawPoints )
	{
		glUseProgram( pointProgram );
		setSharedUniforms( pointProgram );

		GLint loc;
		if( ( loc = glGetUniformLocation( pointProgram, "uPointColor" ) ) >= 0 )
			glUniform4f( loc, tint.x, tint.y, tint.z, pAlpha );
		if( ( loc = glGetUniformLocation( pointProgram, "uBaseFactor" ) ) >= 0 )
			glUniform4f( loc, 1.0f, 1.0f, 1.0f, 1.0f );

		glEnable( GL_PROGRAM_POINT_SIZE );
		glEnable( GL_DEPTH_TEST );
		glDepthFunc( GL_LEQUAL );
		glDepthMask( drawSurface ? GL_FALSE : GL_TRUE );
		glEnable( GL_BLEND );
		glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
		DrawPrimitives( pointProgram, 2 );
		glDisable( GL_PROGRAM_POINT_SIZE );
	}

	glDepthMask( GL_TRUE );
	glDisable( GL_DEPTH_TEST );
	glDisable( GL_BLEND );
	glUseProgram( 0 );
}

void GlbSource::BlitToHost( GLuint hostFbo, const GLint* savedViewport )
{
	glBindFramebuffer( GL_FRAMEBUFFER, hostFbo );
	glViewport( savedViewport[ 0 ], savedViewport[ 1 ], savedViewport[ 2 ], savedViewport[ 3 ] );

	glUseProgram( quadProgram );
	const GLint locTex = glGetUniformLocation( quadProgram, "uTexture" );
	if( locTex >= 0 )
		glUniform1i( locTex, 0 );
	const GLint locOpacity = glGetUniformLocation( quadProgram, "uOpacity" );
	if( locOpacity >= 0 )
		glUniform1f( locOpacity, 1.0f );

	glActiveTexture( GL_TEXTURE0 );
	glBindTexture( GL_TEXTURE_2D, target.resolveColor );

	glDisable( GL_DEPTH_TEST );
	glDisable( GL_BLEND );
	glBindVertexArray( quadVao );
	glDrawArrays( GL_TRIANGLES, 0, 3 );
	glBindVertexArray( 0 );

	glBindTexture( GL_TEXTURE_2D, 0 );
	glUseProgram( 0 );
}

FFResult GlbSource::ProcessOpenGL( ProcessOpenGLStruct* pGL )
{
	if( !programsReady )
		return FF_FAIL;

	const float now = (float)( hostTime / 1000.0 );
	float deltaTime = now - lastHostTime;
	if( deltaTime < 0.0f || deltaTime > 0.5f )
		deltaTime = 1.0f / 60.0f;//first frame, or the host jumped
	lastHostTime = now;

	ProcessPendingLoad();
	UpdateAudio( deltaTime );

	//---------------------------------------------------------------- motion
	const float spinBoost = 1.0f + audioLevel * pAudioSpin;
	const float turn      = 2.0f * PI * deltaTime * spinBoost;
	spinPhaseX = std::fmod( spinPhaseX + pSpinX * turn, 2.0f * PI );
	spinPhaseY = std::fmod( spinPhaseY + pSpinY * turn, 2.0f * PI );
	spinPhaseZ = std::fmod( spinPhaseZ + pSpinZ * turn, 2.0f * PI );

	if( model.IsLoaded() )
	{
		//------------------------------------------------------------ density
		if( std::fabs( pDensity - appliedDensity ) > 0.002f )
		{
			model.SetDensity( pDensity );
			appliedDensity = pDensity;
		}

		//---------------------------------------------------------- animation
		if( pAnimPlay > 0.5f )
			animTime += deltaTime * ( pAnimSpeed + audioLevel * pAudioAnim );
		const int clip = (int)( pAnimClip + 0.5f ) - 1;
		model.Evaluate( clip, animTime );
	}

	//------------------------------------------------------------- rendering
	GLint savedViewport[ 4 ] = { 0, 0, 0, 0 };
	glGetIntegerv( GL_VIEWPORT, savedViewport );

	int width  = savedViewport[ 2 ];
	int height = savedViewport[ 3 ];
	if( width <= 0 || height <= 0 )
	{
		width  = (int)currentViewport.width;
		height = (int)currentViewport.height;
	}

	static const int kSampleCounts[ 4 ] = { 0, 2, 4, 8 };
	int aaIndex = (int)( pAA + 0.5f );
	aaIndex     = aaIndex < 0 ? 0 : ( aaIndex > 3 ? 3 : aaIndex );

	if( !EnsureRenderTarget( width, height, kSampleCounts[ aaIndex ] ) )
		return FF_FAIL;

	glBindFramebuffer( GL_FRAMEBUFFER, target.fbo );
	glViewport( 0, 0, target.width, target.height );
	glClearColor( 0.0f, 0.0f, 0.0f, pBgAlpha );
	glClearDepth( 1.0 );
	glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );

	if( model.IsLoaded() )
		RenderModel( target.width, target.height );

	// Resolve (or plain copy when msaa is off) into the sampleable texture.
	glBindFramebuffer( GL_READ_FRAMEBUFFER, target.fbo );
	glBindFramebuffer( GL_DRAW_FRAMEBUFFER, target.resolveFbo );
	glBlitFramebuffer( 0, 0, target.width, target.height,
	                   0, 0, target.width, target.height,
	                   GL_COLOR_BUFFER_BIT, GL_NEAREST );

	BlitToHost( pGL->HostFBO, savedViewport );

	//--------------------------------------------- FFGL wants a default state
	glBindFramebuffer( GL_FRAMEBUFFER, pGL->HostFBO );
	glViewport( savedViewport[ 0 ], savedViewport[ 1 ], savedViewport[ 2 ], savedViewport[ 3 ] );
	glDisable( GL_DEPTH_TEST );
	glDisable( GL_CULL_FACE );
	glDisable( GL_BLEND );
	glDepthMask( GL_TRUE );
	glDepthFunc( GL_LESS );
	glBlendFunc( GL_ONE, GL_ZERO );
	glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
	glActiveTexture( GL_TEXTURE0 );
	glBindTexture( GL_TEXTURE_2D, 0 );
	glUseProgram( 0 );
	glBindVertexArray( 0 );

	return FF_SUCCESS;
}

//----------------------------------------------------------------- parameters

FFResult GlbSource::SetTextParameter( unsigned int index, const char* value )
{
	if( index != PID_FILE )
		return FF_FAIL;

	const std::string incoming = value ? value : "";
	std::lock_guard< std::mutex > lock( fileMutex );
	if( incoming != filePath )
	{
		filePath      = incoming;
		loadRequested = true;
	}
	return FF_SUCCESS;
}

char* GlbSource::GetTextParameter( unsigned int index )
{
	if( index != PID_FILE )
		return nullptr;
	// The host does not copy the string, so hand back a buffer that outlives
	// the call rather than the member the ui thread may be rewriting.
	static std::string handback;
	{
		std::lock_guard< std::mutex > lock( fileMutex );
		handback = filePath;
	}
	return const_cast< char* >( handback.c_str() );
}

FFResult GlbSource::SetFloatParameter( unsigned int index, float value )
{
	switch( index )
	{
	case PID_RELOAD:
		if( value != 0.0f )
		{
			std::lock_guard< std::mutex > lock( fileMutex );
			loadRequested = true;
		}
		break;
	case PID_DENSITY:       pDensity = value; break;
	case PID_AUTOFIT:       pAutoFit = value; break;

	case PID_SCALE:         pScale = value; break;
	case PID_ROT_X:         pRotX = value; break;
	case PID_ROT_Y:         pRotY = value; break;
	case PID_ROT_Z:         pRotZ = value; break;
	case PID_SPIN_X:        pSpinX = value; break;
	case PID_SPIN_Y:        pSpinY = value; break;
	case PID_SPIN_Z:        pSpinZ = value; break;
	case PID_OFFSET_X:      pOffsetX = value; break;
	case PID_OFFSET_Y:      pOffsetY = value; break;
	case PID_DISTANCE:      pDistance = value; break;
	case PID_FOV:           pFov = value; break;

	case PID_ANIM_CLIP:     pAnimClip = value; break;
	case PID_ANIM_SPEED:    pAnimSpeed = value; break;
	case PID_ANIM_PLAY:     pAnimPlay = value; break;
	case PID_ANIM_RESET:
		if( value != 0.0f )
			animTime = 0.0f;
		break;

	case PID_MODE:          pMode = value; break;
	case PID_WIRE_OVERLAY:  pWireOverlay = value; break;
	case PID_WIRE_WIDTH:    pWireWidth = value; break;
	case PID_WIRE_OPACITY:  pWireOpacity = value; break;
	case PID_POINT_OVERLAY: pPointOverlay = value; break;
	case PID_POINT_SIZE:    pPointSize = value; break;
	case PID_DISPLACE:      pDisplace = value; break;
	case PID_CULL:          pCull = value; break;
	case PID_AA:            pAA = value; break;

	case PID_HUE:           pHue = value; break;
	case PID_SATURATION:    pSaturation = value; break;
	case PID_BRIGHTNESS:    pBrightness = value; break;
	case PID_ALPHA:         pAlpha = value; break;
	case PID_LIGHT_YAW:     pLightYaw = value; break;
	case PID_LIGHT_PITCH:   pLightPitch = value; break;
	case PID_LIGHT_INTENSITY: pLightIntensity = value; break;
	case PID_AMBIENT:       pAmbient = value; break;
	case PID_METALLIC:      pMetallic = value; break;
	case PID_ROUGHNESS:     pRoughness = value; break;
	case PID_BG_ALPHA:      pBgAlpha = value; break;

	case PID_FFT:           break;//host fills the element values directly
	case PID_AUDIO_BAND:    pAudioBand = value; break;
	case PID_AUDIO_GAIN:    pAudioGain = value; break;
	case PID_AUDIO_SMOOTH:  pAudioSmooth = value; break;
	case PID_AUDIO_SCALE:   pAudioScale = value; break;
	case PID_AUDIO_SPIN:    pAudioSpin = value; break;
	case PID_AUDIO_ANIM:    pAudioAnim = value; break;
	case PID_AUDIO_DISPLACE: pAudioDisplace = value; break;

	default:
		return FF_FAIL;
	}
	return FF_SUCCESS;
}

float GlbSource::GetFloatParameter( unsigned int index )
{
	switch( index )
	{
	case PID_RELOAD:        return 0.0f;
	case PID_DENSITY:       return pDensity;
	case PID_AUTOFIT:       return pAutoFit;

	case PID_SCALE:         return pScale;
	case PID_ROT_X:         return pRotX;
	case PID_ROT_Y:         return pRotY;
	case PID_ROT_Z:         return pRotZ;
	case PID_SPIN_X:        return pSpinX;
	case PID_SPIN_Y:        return pSpinY;
	case PID_SPIN_Z:        return pSpinZ;
	case PID_OFFSET_X:      return pOffsetX;
	case PID_OFFSET_Y:      return pOffsetY;
	case PID_DISTANCE:      return pDistance;
	case PID_FOV:           return pFov;

	case PID_ANIM_CLIP:     return pAnimClip;
	case PID_ANIM_SPEED:    return pAnimSpeed;
	case PID_ANIM_PLAY:     return pAnimPlay;
	case PID_ANIM_RESET:    return 0.0f;

	case PID_MODE:          return pMode;
	case PID_WIRE_OVERLAY:  return pWireOverlay;
	case PID_WIRE_WIDTH:    return pWireWidth;
	case PID_WIRE_OPACITY:  return pWireOpacity;
	case PID_POINT_OVERLAY: return pPointOverlay;
	case PID_POINT_SIZE:    return pPointSize;
	case PID_DISPLACE:      return pDisplace;
	case PID_CULL:          return pCull;
	case PID_AA:            return pAA;

	case PID_HUE:           return pHue;
	case PID_SATURATION:    return pSaturation;
	case PID_BRIGHTNESS:    return pBrightness;
	case PID_ALPHA:         return pAlpha;
	case PID_LIGHT_YAW:     return pLightYaw;
	case PID_LIGHT_PITCH:   return pLightPitch;
	case PID_LIGHT_INTENSITY: return pLightIntensity;
	case PID_AMBIENT:       return pAmbient;
	case PID_METALLIC:      return pMetallic;
	case PID_ROUGHNESS:     return pRoughness;
	case PID_BG_ALPHA:      return pBgAlpha;

	case PID_FFT:           return 0.0f;
	case PID_AUDIO_BAND:    return pAudioBand;
	case PID_AUDIO_GAIN:    return pAudioGain;
	case PID_AUDIO_SMOOTH:  return pAudioSmooth;
	case PID_AUDIO_SCALE:   return pAudioScale;
	case PID_AUDIO_SPIN:    return pAudioSpin;
	case PID_AUDIO_ANIM:    return pAudioAnim;
	case PID_AUDIO_DISPLACE: return pAudioDisplace;

	default:
		return 0.0f;
	}
}

char* GlbSource::GetParameterDisplay( unsigned int index )
{
	// The host does not take ownership of this string, so it has to outlive the
	// call. One static buffer per plugin is what the FFGL examples do as well.
	static char display[ 32 ];
	std::memset( display, 0, sizeof( display ) );

	switch( index )
	{
	case PID_DENSITY:
		std::snprintf( display, sizeof( display ), "%d tris",
		               model.IsLoaded() ? (int)model.TriangleCountActive() : 0 );
		return display;
	case PID_SCALE:
		std::snprintf( display, sizeof( display ), "%.2f", pScale );
		return display;
	case PID_ROT_X:
	case PID_ROT_Y:
	case PID_ROT_Z:
		std::snprintf( display, sizeof( display ), "%.0f deg", GetFloatParameter( index ) );
		return display;
	case PID_SPIN_X:
	case PID_SPIN_Y:
	case PID_SPIN_Z:
		std::snprintf( display, sizeof( display ), "%.2f t/s", GetFloatParameter( index ) );
		return display;
	case PID_FOV:
		std::snprintf( display, sizeof( display ), "%.0f deg", pFov );
		return display;
	case PID_ANIM_SPEED:
		std::snprintf( display, sizeof( display ), "%.2f x", pAnimSpeed );
		return display;
	case PID_WIRE_WIDTH:
		std::snprintf( display, sizeof( display ), "%.2f px", pWireWidth );
		return display;
	case PID_POINT_SIZE:
		std::snprintf( display, sizeof( display ), "%.1f px", pPointSize );
		return display;
	default:
		return CFFGLPlugin::GetParameterDisplay( index );
	}
}
