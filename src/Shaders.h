// SPDX-License-Identifier: GPL-3.0-or-later
// GLB Model Source - an FFGL source plugin for Resolume Arena
// Copyright (C) 2026 Elisa Bernard (Elisalien)

#pragma once

// All shaders target GLSL 4.10 core, which is what Resolume's context provides.

namespace shaders
{
// --------------------------------------------------------------------------
// Shared vertex stage: optional GPU skinning, normal-driven displacement.
// --------------------------------------------------------------------------
static const char* kModelVertex = R"(#version 410 core
layout( location = 0 ) in vec3 aPos;
layout( location = 1 ) in vec3 aNormal;
layout( location = 2 ) in vec2 aUV;
layout( location = 3 ) in vec4 aJoints;
layout( location = 4 ) in vec4 aWeights;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
uniform mat4 uJoints[ 128 ];
uniform int uSkinned;
uniform float uDisplace;
uniform float uPointSize;

out vec3 vWorld;
out vec3 vNormal;
out vec2 vUV;

void main()
{
	vec4 position = vec4( aPos, 1.0 );
	vec3 normal   = aNormal;

	if( uSkinned == 1 )
	{
		float total = aWeights.x + aWeights.y + aWeights.z + aWeights.w;
		if( total > 0.0001 )
		{
			vec4 w = aWeights / total;
			mat4 skin = w.x * uJoints[ int( aJoints.x ) ]
			          + w.y * uJoints[ int( aJoints.y ) ]
			          + w.z * uJoints[ int( aJoints.z ) ]
			          + w.w * uJoints[ int( aJoints.w ) ];
			position = skin * position;
			normal   = mat3( skin ) * normal;
		}
	}

	normal = normalize( normal );
	position.xyz += normal * uDisplace;

	vec4 world = uModel * position;
	vWorld     = world.xyz;
	vNormal    = normalize( mat3( uModel ) * normal );
	vUV        = aUV;

	gl_Position  = uProj * uView * world;
	gl_PointSize = uPointSize;
}
)";

// --------------------------------------------------------------------------
// Surface stage. uMode 0 = lit PBR, 1 = flat facetted, 2 = unlit colour.
// --------------------------------------------------------------------------
static const char* kModelFragment = R"(#version 410 core
in vec3 vWorld;
in vec3 vNormal;
in vec2 vUV;

uniform int uMode;
uniform sampler2D uBaseColorMap;
uniform sampler2D uNormalMap;
uniform sampler2D uMetalRoughMap;
uniform sampler2D uEmissiveMap;
uniform int uHasBaseColor;
uniform int uHasNormalMap;
uniform int uHasMetalRough;
uniform int uHasEmissive;

uniform vec4 uBaseFactor;
uniform vec3 uEmissiveFactor;
uniform float uMetallic;
uniform float uRoughness;
uniform int uAlphaMode;
uniform float uAlphaCutoff;

uniform vec4 uTint;
uniform vec3 uLightDir;
uniform vec3 uLightColor;
uniform vec3 uAmbientTop;
uniform vec3 uAmbientBottom;
uniform vec3 uCameraPos;

out vec4 fragColor;

// Tangent frame from screen-space derivatives: no TANGENT attribute needed.
vec3 perturbNormal( vec3 N, vec2 uv )
{
	vec3 dp1 = dFdx( vWorld );
	vec3 dp2 = dFdy( vWorld );
	vec2 duv1 = dFdx( uv );
	vec2 duv2 = dFdy( uv );

	vec3 dp2perp = cross( dp2, N );
	vec3 dp1perp = cross( N, dp1 );
	vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
	vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;

	float invmax = inversesqrt( max( dot( T, T ), dot( B, B ) ) );
	mat3 TBN = mat3( T * invmax, B * invmax, N );

	vec3 sampled = texture( uNormalMap, uv ).xyz * 2.0 - 1.0;
	return normalize( TBN * sampled );
}

float distributionGGX( float NdotH, float roughness )
{
	float a  = roughness * roughness;
	float a2 = a * a;
	float d  = NdotH * NdotH * ( a2 - 1.0 ) + 1.0;
	return a2 / max( 3.14159265 * d * d, 1e-5 );
}

float geometrySmith( float NdotV, float NdotL, float roughness )
{
	float r = roughness + 1.0;
	float k = ( r * r ) / 8.0;
	float gv = NdotV / ( NdotV * ( 1.0 - k ) + k );
	float gl = NdotL / ( NdotL * ( 1.0 - k ) + k );
	return gv * gl;
}

void main()
{
	vec4 base = uBaseFactor;
	if( uHasBaseColor == 1 )
		base *= texture( uBaseColorMap, vUV );
	base *= uTint;

	if( uAlphaMode == 1 && base.a < uAlphaCutoff )
		discard;

	if( uMode == 2 )
	{
		fragColor = vec4( pow( max( base.rgb, vec3( 0.0 ) ), vec3( 1.0 / 2.2 ) ), base.a );
		return;
	}

	vec3 V = normalize( uCameraPos - vWorld );
	vec3 N;
	if( uMode == 1 )
	{
		// Facetted: rebuild the geometric normal from the world position.
		N = normalize( cross( dFdx( vWorld ), dFdy( vWorld ) ) );
		if( dot( N, V ) < 0.0 )
			N = -N;
	}
	else
	{
		N = normalize( vNormal );
		if( !gl_FrontFacing )
			N = -N;
		if( uHasNormalMap == 1 )
			N = perturbNormal( N, vUV );
	}

	float metallic  = uMetallic;
	float roughness = uRoughness;
	if( uHasMetalRough == 1 )
	{
		vec3 mr    = texture( uMetalRoughMap, vUV ).rgb;
		roughness *= mr.g;
		metallic  *= mr.b;
	}
	roughness = clamp( roughness, 0.045, 1.0 );
	metallic  = clamp( metallic, 0.0, 1.0 );

	vec3 L = normalize( -uLightDir );
	vec3 H = normalize( V + L );
	float NdotL = max( dot( N, L ), 0.0 );
	float NdotV = max( dot( N, V ), 1e-4 );
	float NdotH = max( dot( N, H ), 0.0 );
	float VdotH = max( dot( V, H ), 0.0 );

	vec3 F0 = mix( vec3( 0.04 ), base.rgb, metallic );
	vec3 F  = F0 + ( 1.0 - F0 ) * pow( 1.0 - VdotH, 5.0 );

	vec3 spec = ( distributionGGX( NdotH, roughness ) * geometrySmith( NdotV, NdotL, roughness ) * F )
	          / max( 4.0 * NdotV * NdotL, 1e-4 );
	vec3 kD = ( vec3( 1.0 ) - F ) * ( 1.0 - metallic );

	vec3 direct = ( kD * base.rgb / 3.14159265 + spec ) * uLightColor * NdotL;

	// Hemisphere ambient stands in for an environment map.
	float hemi = N.y * 0.5 + 0.5;
	vec3 ambientColor = mix( uAmbientBottom, uAmbientTop, hemi );
	vec3 ambient = ambientColor * base.rgb * mix( 1.0, 0.35, metallic );

	vec3 emissive = uEmissiveFactor;
	if( uHasEmissive == 1 )
		emissive *= texture( uEmissiveMap, vUV ).rgb;

	vec3 linear = max( direct + ambient + emissive, vec3( 0.0 ) );
	fragColor = vec4( pow( linear, vec3( 1.0 / 2.2 ) ), base.a );
}
)";

// --------------------------------------------------------------------------
// Wireframe: barycentric coordinates from a geometry shader give real,
// resolution-independent line thickness (glLineWidth is capped at 1 in core).
// --------------------------------------------------------------------------
static const char* kWireGeometry = R"(#version 410 core
layout( triangles ) in;
layout( triangle_strip, max_vertices = 3 ) out;

in vec3 vWorld[];
in vec3 vNormal[];
in vec2 vUV[];

out vec3 gBary;

void main()
{
	for( int i = 0; i < 3; ++i )
	{
		gBary = vec3( i == 0 ? 1.0 : 0.0, i == 1 ? 1.0 : 0.0, i == 2 ? 1.0 : 0.0 );
		gl_Position = gl_in[ i ].gl_Position;
		EmitVertex();
	}
	EndPrimitive();
}
)";

static const char* kWireFragment = R"(#version 410 core
in vec3 gBary;

uniform vec4 uWireColor;
uniform float uWireWidth;

out vec4 fragColor;

void main()
{
	vec3 d = fwidth( gBary );
	vec3 a = smoothstep( vec3( 0.0 ), d * uWireWidth, gBary );
	float edge = 1.0 - min( min( a.x, a.y ), a.z );
	if( edge < 0.01 )
		discard;
	fragColor = vec4( uWireColor.rgb, uWireColor.a * edge );
}
)";

// --------------------------------------------------------------------------
// Point cloud: round sprites with a soft edge.
// --------------------------------------------------------------------------
static const char* kPointFragment = R"(#version 410 core
in vec3 vWorld;
in vec3 vNormal;
in vec2 vUV;

uniform vec4 uPointColor;
uniform sampler2D uBaseColorMap;
uniform int uHasBaseColor;
uniform vec4 uBaseFactor;

out vec4 fragColor;

void main()
{
	vec2 c = gl_PointCoord - vec2( 0.5 );
	float r = length( c ) * 2.0;
	float alpha = 1.0 - smoothstep( 0.75, 1.0, r );
	if( alpha <= 0.002 )
		discard;

	vec4 base = uBaseFactor;
	if( uHasBaseColor == 1 )
		base *= texture( uBaseColorMap, vUV );
	base *= uPointColor;

	fragColor = vec4( pow( max( base.rgb, vec3( 0.0 ) ), vec3( 1.0 / 2.2 ) ), base.a * alpha );
}
)";

// --------------------------------------------------------------------------
// Blit of the offscreen render into the host's framebuffer.
// --------------------------------------------------------------------------
static const char* kQuadVertex = R"(#version 410 core
layout( location = 0 ) in vec2 aPos;
out vec2 vUV;
void main()
{
	vUV = aPos * 0.5 + 0.5;
	gl_Position = vec4( aPos, 0.0, 1.0 );
}
)";

static const char* kQuadFragment = R"(#version 410 core
in vec2 vUV;
uniform sampler2D uTexture;
uniform float uOpacity;
out vec4 fragColor;
void main()
{
	fragColor = texture( uTexture, vUV ) * uOpacity;
}
)";
}//namespace shaders
