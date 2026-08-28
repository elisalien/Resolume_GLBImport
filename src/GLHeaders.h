// SPDX-License-Identifier: GPL-3.0-or-later
// GLB Model Source - an FFGL source plugin for Resolume Arena
// Copyright (C) 2026 Elisa Bernard (Elisalien)

#pragma once

// One place to pull in the OpenGL entry points, matching what the FFGL SDK does
// per platform: Apple ships GL 4.1 core directly in its own header and needs no
// loader, everywhere else we go through GLEW.
#if defined( __APPLE__ )
#	define GL_SILENCE_DEPRECATION
#	include <OpenGL/gl3.h>
#else
#	include <GL/glew.h>
#endif
