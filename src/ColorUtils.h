// SPDX-License-Identifier: GPL-3.0-or-later
// GLB Model Source - an FFGL source plugin for Resolume Arena
// Copyright (C) 2026 Elisa Bernard (Elisalien)

#pragma once
#include <cmath>
#include "Math3D.h"

/// Resolume hands hue / saturation / brightness in display space. Lighting maths
/// wants linear values, so convert and de-gamma in one step.
inline m3d::Vec3 HsbToLinearRgb( float h, float s, float b )
{
	h = h - std::floor( h );
	const float i = std::floor( h * 6.0f );
	const float f = h * 6.0f - i;
	const float p = b * ( 1.0f - s );
	const float q = b * ( 1.0f - f * s );
	const float t = b * ( 1.0f - ( 1.0f - f ) * s );

	float r = b, g = b, bl = b;
	switch( ( (int)i ) % 6 )
	{
	case 0: r = b;  g = t;  bl = p; break;
	case 1: r = q;  g = b;  bl = p; break;
	case 2: r = p;  g = b;  bl = t; break;
	case 3: r = p;  g = q;  bl = b; break;
	case 4: r = t;  g = p;  bl = b; break;
	case 5: r = b;  g = p;  bl = q; break;
	}
	return m3d::Vec3( std::pow( r, 2.2f ), std::pow( g, 2.2f ), std::pow( bl, 2.2f ) );
}
