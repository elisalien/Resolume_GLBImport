// SPDX-License-Identifier: GPL-3.0-or-later
// GLB Model Source - an FFGL source plugin for Resolume Arena
// Copyright (C) 2026 Elisa Bernard (Elisalien)

#pragma once
#include <FFGLSDK.h>

/// Parameter indices exposed to the host. The order is what Resolume shows in
/// the plugin's control panel, and it is also what gets serialised into a
/// composition, so never reorder these once a comp has been saved with them.
enum ParamID : FFUInt32
{
	PID_FILE = 0,
	PID_RELOAD,
	PID_DENSITY,
	PID_AUTOFIT,

	PID_SCALE,
	PID_ROT_X,
	PID_ROT_Y,
	PID_ROT_Z,
	PID_SPIN_X,
	PID_SPIN_Y,
	PID_SPIN_Z,
	PID_OFFSET_X,
	PID_OFFSET_Y,
	PID_DISTANCE,
	PID_FOV,

	PID_ANIM_CLIP,
	PID_ANIM_SPEED,
	PID_ANIM_PLAY,
	PID_ANIM_RESET,

	PID_MODE,
	PID_WIRE_OVERLAY,
	PID_WIRE_WIDTH,
	PID_WIRE_OPACITY,
	PID_POINT_OVERLAY,
	PID_POINT_SIZE,
	PID_DISPLACE,
	PID_CULL,
	PID_AA,

	PID_HUE,
	PID_SATURATION,
	PID_BRIGHTNESS,
	PID_ALPHA,
	PID_LIGHT_YAW,
	PID_LIGHT_PITCH,
	PID_LIGHT_INTENSITY,
	PID_AMBIENT,
	PID_METALLIC,
	PID_ROUGHNESS,
	PID_BG_ALPHA,

	PID_FFT,
	PID_AUDIO_BAND,
	PID_AUDIO_GAIN,
	PID_AUDIO_SMOOTH,
	PID_AUDIO_SCALE,
	PID_AUDIO_SPIN,
	PID_AUDIO_ANIM,
	PID_AUDIO_DISPLACE,

	PID_COUNT
};

/// Number of FFT bins requested from the host. 128 bins over a 44.1 kHz stream
/// is roughly 172 Hz per bin, enough to separate kick, body and air.
static const unsigned int FFT_BINS = 128;

/// Upper bound of animation clips offered in the Clip dropdown (plus "None").
static const int MAX_ANIM_CLIPS = 32;
