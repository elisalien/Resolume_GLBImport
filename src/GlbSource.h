// SPDX-License-Identifier: GPL-3.0-or-later
// GLB Model Source - an FFGL source plugin for Resolume Arena
// Copyright (C) 2026 Elisa Bernard (Elisalien)

#pragma once
#include <FFGLSDK.h>
#include <mutex>
#include <string>
#include <vector>
#include "GltfModel.h"
#include "Math3D.h"

/// FFGL 2.2 source plugin: loads a .glb / .gltf model, decimates it on load,
/// plays its animation clips and exposes transform / look / audio parameters.
class GlbSource : public CFFGLPlugin
{
public:
	GlbSource();
	~GlbSource() override;

	FFResult InitGL( const FFGLViewportStruct* vp ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* pGL ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;
	FFResult SetTextParameter( unsigned int index, const char* value ) override;
	char* GetTextParameter( unsigned int index ) override;
	char* GetParameterDisplay( unsigned int index ) override;

private:
	struct RenderTarget
	{
		GLuint fbo = 0, color = 0, depth = 0;
		GLuint resolveFbo = 0, resolveColor = 0;
		int width = 0, height = 0, samples = 0;
	};

	// setup ---------------------------------------------------------------
	void RegisterParameters();
	void AddFloatParam( unsigned int id, const char* name, float min, float max, float defaultValue, const char* group );
	bool BuildPrograms();
	void ReleasePrograms();

	// per frame -----------------------------------------------------------
	void ProcessPendingLoad();
	void UpdateAudio( float deltaTime );
	bool EnsureRenderTarget( int width, int height, int samples );
	void ReleaseRenderTarget();
	void RenderModel( int width, int height );
	void DrawPrimitives( GLuint program, int passMode );
	void BlitToHost( GLuint hostFbo, const GLint* savedViewport );
	void PublishClipNames();
	m3d::Mat4 BaseTransform() const;

	// state ---------------------------------------------------------------
	GltfModel model;
	RenderTarget target;

	GLuint solidProgram = 0;
	GLuint wireProgram  = 0;
	GLuint pointProgram = 0;
	GLuint quadProgram  = 0;
	GLuint quadVao = 0, quadVbo = 0;
	GLuint whiteTexture = 0;
	bool programsReady  = false;

	// The host can change the file parameter from its ui thread while
	// ProcessOpenGL is running on the render thread, so these two are guarded.
	mutable std::mutex fileMutex;
	std::string filePath;   //what the host gave us
	std::string loadedPath; //what is actually in memory
	bool loadRequested = false;
	int publishedClipCount = -1;

	float lastHostTime = 0.0f;
	float animTime     = 0.0f;
	float spinPhaseX = 0.0f, spinPhaseY = 0.0f, spinPhaseZ = 0.0f;

	float audioLevel   = 0.0f;//smoothed, 0..1
	float audioRaw     = 0.0f;
	float appliedDensity = 1.0f;

	// parameters ----------------------------------------------------------
	float pDensity   = 1.0f;
	float pAutoFit   = 1.0f;
	float pScale     = 1.0f;
	float pRotX = 0.0f, pRotY = 0.0f, pRotZ = 0.0f;
	float pSpinX = 0.0f, pSpinY = 0.0f, pSpinZ = 0.0f;
	float pOffsetX = 0.0f, pOffsetY = 0.0f;
	float pDistance = 3.0f;
	float pFov      = 45.0f;

	float pAnimClip  = 0.0f;
	float pAnimSpeed = 1.0f;
	float pAnimPlay  = 1.0f;

	float pMode         = 0.0f;
	float pWireOverlay  = 0.0f;
	float pWireWidth    = 1.5f;
	float pWireOpacity  = 1.0f;
	float pPointOverlay = 0.0f;
	float pPointSize    = 4.0f;
	float pDisplace     = 0.0f;
	float pCull         = 0.0f;
	float pAA           = 2.0f;//index into { off, 2x, 4x, 8x }

	float pHue = 0.0f, pSaturation = 0.0f, pBrightness = 1.0f, pAlpha = 1.0f;
	float pLightYaw = 0.35f, pLightPitch = 0.65f, pLightIntensity = 1.0f;
	float pAmbient   = 0.25f;
	float pMetallic  = 1.0f;
	float pRoughness = 1.0f;
	float pBgAlpha   = 0.0f;

	float pAudioBand     = 1.0f;//0 volume, 1 low, 2 mid, 3 high
	float pAudioGain     = 1.0f;
	float pAudioSmooth   = 0.55f;
	float pAudioScale    = 0.0f;
	float pAudioSpin     = 0.0f;
	float pAudioAnim     = 0.0f;
	float pAudioDisplace = 0.0f;
};
