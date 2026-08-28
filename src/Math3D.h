// SPDX-License-Identifier: GPL-3.0-or-later
// GLB Model Source - an FFGL source plugin for Resolume Arena
// Copyright (C) 2026 Elisa Bernard (Elisalien)

#pragma once
// Minimal linear algebra for the GLB source plugin. Column-major, GL convention.
#include <cmath>
#include <cstring>

namespace m3d
{
static const float PI = 3.14159265358979323846f;

inline float radians( float deg ) { return deg * ( PI / 180.0f ); }
inline float clampf( float v, float lo, float hi ) { return v < lo ? lo : ( v > hi ? hi : v ); }
inline float mixf( float a, float b, float t ) { return a + ( b - a ) * t; }

struct Vec3
{
	float x = 0.0f, y = 0.0f, z = 0.0f;
	Vec3() = default;
	Vec3( float x, float y, float z ) : x( x ), y( y ), z( z ) {}
	Vec3 operator+( const Vec3& o ) const { return Vec3( x + o.x, y + o.y, z + o.z ); }
	Vec3 operator-( const Vec3& o ) const { return Vec3( x - o.x, y - o.y, z - o.z ); }
	Vec3 operator*( float s ) const { return Vec3( x * s, y * s, z * s ); }
};
inline float dot( const Vec3& a, const Vec3& b ) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross( const Vec3& a, const Vec3& b )
{
	return Vec3( a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x );
}
inline float length( const Vec3& v ) { return std::sqrt( dot( v, v ) ); }
inline Vec3 normalize( const Vec3& v )
{
	float l = length( v );
	return l > 1e-8f ? v * ( 1.0f / l ) : Vec3( 0.0f, 0.0f, 1.0f );
}

struct Quat
{
	float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;
	Quat() = default;
	Quat( float x, float y, float z, float w ) : x( x ), y( y ), z( z ), w( w ) {}
};

inline Quat normalize( const Quat& q )
{
	float l = std::sqrt( q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w );
	if( l < 1e-8f )
		return Quat();
	float i = 1.0f / l;
	return Quat( q.x * i, q.y * i, q.z * i, q.w * i );
}

/// Shortest-path spherical interpolation. Falls back to lerp for near-parallel quaternions.
inline Quat slerp( Quat a, const Quat& b, float t )
{
	float cosom = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
	if( cosom < 0.0f )
	{
		cosom = -cosom;
		a     = Quat( -a.x, -a.y, -a.z, -a.w );
	}
	float sa, sb;
	if( 1.0f - cosom > 1e-5f )
	{
		float omega = std::acos( cosom );
		float sinom = std::sin( omega );
		sa          = std::sin( ( 1.0f - t ) * omega ) / sinom;
		sb          = std::sin( t * omega ) / sinom;
	}
	else
	{
		sa = 1.0f - t;
		sb = t;
	}
	return normalize( Quat( a.x * sa + b.x * sb, a.y * sa + b.y * sb, a.z * sa + b.z * sb, a.w * sa + b.w * sb ) );
}

/// Column-major 4x4, m[col*4+row] — directly uploadable with glUniformMatrix4fv( .., GL_FALSE, .. ).
struct Mat4
{
	float m[ 16 ];
	Mat4() { identity(); }
	void identity()
	{
		std::memset( m, 0, sizeof( m ) );
		m[ 0 ] = m[ 5 ] = m[ 10 ] = m[ 15 ] = 1.0f;
	}
	static Mat4 zero()
	{
		Mat4 r;
		std::memset( r.m, 0, sizeof( r.m ) );
		return r;
	}
	Mat4 operator*( const Mat4& o ) const
	{
		Mat4 r = zero();
		for( int c = 0; c < 4; ++c )
			for( int rw = 0; rw < 4; ++rw )
			{
				float s = 0.0f;
				for( int k = 0; k < 4; ++k )
					s += m[ k * 4 + rw ] * o.m[ c * 4 + k ];
				r.m[ c * 4 + rw ] = s;
			}
		return r;
	}
	Vec3 transformPoint( const Vec3& p ) const
	{
		return Vec3(
			m[ 0 ] * p.x + m[ 4 ] * p.y + m[ 8 ] * p.z + m[ 12 ],
			m[ 1 ] * p.x + m[ 5 ] * p.y + m[ 9 ] * p.z + m[ 13 ],
			m[ 2 ] * p.x + m[ 6 ] * p.y + m[ 10 ] * p.z + m[ 14 ] );
	}
	Mat4 operator+( const Mat4& o ) const
	{
		Mat4 r;
		for( int i = 0; i < 16; ++i )
			r.m[ i ] = m[ i ] + o.m[ i ];
		return r;
	}
	Mat4 operator*( float s ) const
	{
		Mat4 r;
		for( int i = 0; i < 16; ++i )
			r.m[ i ] = m[ i ] * s;
		return r;
	}
};

inline Mat4 translate( const Vec3& t )
{
	Mat4 r;
	r.m[ 12 ] = t.x;
	r.m[ 13 ] = t.y;
	r.m[ 14 ] = t.z;
	return r;
}
inline Mat4 scale( const Vec3& s )
{
	Mat4 r;
	r.m[ 0 ]  = s.x;
	r.m[ 5 ]  = s.y;
	r.m[ 10 ] = s.z;
	return r;
}
inline Mat4 fromQuat( const Quat& q )
{
	Mat4 r;
	float x = q.x, y = q.y, z = q.z, w = q.w;
	r.m[ 0 ] = 1.0f - 2.0f * ( y * y + z * z );
	r.m[ 1 ] = 2.0f * ( x * y + z * w );
	r.m[ 2 ] = 2.0f * ( x * z - y * w );
	r.m[ 4 ] = 2.0f * ( x * y - z * w );
	r.m[ 5 ] = 1.0f - 2.0f * ( x * x + z * z );
	r.m[ 6 ] = 2.0f * ( y * z + x * w );
	r.m[ 8 ]  = 2.0f * ( x * z + y * w );
	r.m[ 9 ]  = 2.0f * ( y * z - x * w );
	r.m[ 10 ] = 1.0f - 2.0f * ( x * x + y * y );
	return r;
}
inline Mat4 rotateX( float rad )
{
	Mat4 r;
	float c = std::cos( rad ), s = std::sin( rad );
	r.m[ 5 ] = c; r.m[ 6 ] = s; r.m[ 9 ] = -s; r.m[ 10 ] = c;
	return r;
}
inline Mat4 rotateY( float rad )
{
	Mat4 r;
	float c = std::cos( rad ), s = std::sin( rad );
	r.m[ 0 ] = c; r.m[ 2 ] = -s; r.m[ 8 ] = s; r.m[ 10 ] = c;
	return r;
}
inline Mat4 rotateZ( float rad )
{
	Mat4 r;
	float c = std::cos( rad ), s = std::sin( rad );
	r.m[ 0 ] = c; r.m[ 1 ] = s; r.m[ 4 ] = -s; r.m[ 5 ] = c;
	return r;
}
inline Mat4 trs( const Vec3& t, const Quat& q, const Vec3& s )
{
	return translate( t ) * fromQuat( q ) * scale( s );
}
inline Mat4 perspective( float fovYRad, float aspect, float zn, float zf )
{
	Mat4 r    = Mat4::zero();
	float f   = 1.0f / std::tan( fovYRad * 0.5f );
	r.m[ 0 ]  = f / ( aspect > 1e-6f ? aspect : 1.0f );
	r.m[ 5 ]  = f;
	r.m[ 10 ] = ( zf + zn ) / ( zn - zf );
	r.m[ 11 ] = -1.0f;
	r.m[ 14 ] = ( 2.0f * zf * zn ) / ( zn - zf );
	return r;
}
inline Mat4 lookAt( const Vec3& eye, const Vec3& center, const Vec3& up )
{
	Vec3 f = normalize( center - eye );
	Vec3 s = normalize( cross( f, up ) );
	Vec3 u = cross( s, f );
	Mat4 r;
	r.m[ 0 ] = s.x; r.m[ 4 ] = s.y; r.m[ 8 ]  = s.z;
	r.m[ 1 ] = u.x; r.m[ 5 ] = u.y; r.m[ 9 ]  = u.z;
	r.m[ 2 ] = -f.x; r.m[ 6 ] = -f.y; r.m[ 10 ] = -f.z;
	r.m[ 12 ] = -dot( s, eye );
	r.m[ 13 ] = -dot( u, eye );
	r.m[ 14 ] = dot( f, eye );
	return r;
}

/// General 4x4 inverse (Cramer). Returns identity for singular input.
inline Mat4 inverse( const Mat4& src )
{
	const float* m = src.m;
	float inv[ 16 ];
	inv[ 0 ] = m[ 5 ] * m[ 10 ] * m[ 15 ] - m[ 5 ] * m[ 11 ] * m[ 14 ] - m[ 9 ] * m[ 6 ] * m[ 15 ] + m[ 9 ] * m[ 7 ] * m[ 14 ] + m[ 13 ] * m[ 6 ] * m[ 11 ] - m[ 13 ] * m[ 7 ] * m[ 10 ];
	inv[ 4 ] = -m[ 4 ] * m[ 10 ] * m[ 15 ] + m[ 4 ] * m[ 11 ] * m[ 14 ] + m[ 8 ] * m[ 6 ] * m[ 15 ] - m[ 8 ] * m[ 7 ] * m[ 14 ] - m[ 12 ] * m[ 6 ] * m[ 11 ] + m[ 12 ] * m[ 7 ] * m[ 10 ];
	inv[ 8 ] = m[ 4 ] * m[ 9 ] * m[ 15 ] - m[ 4 ] * m[ 11 ] * m[ 13 ] - m[ 8 ] * m[ 5 ] * m[ 15 ] + m[ 8 ] * m[ 7 ] * m[ 13 ] + m[ 12 ] * m[ 5 ] * m[ 11 ] - m[ 12 ] * m[ 7 ] * m[ 9 ];
	inv[ 12 ] = -m[ 4 ] * m[ 9 ] * m[ 14 ] + m[ 4 ] * m[ 10 ] * m[ 13 ] + m[ 8 ] * m[ 5 ] * m[ 14 ] - m[ 8 ] * m[ 6 ] * m[ 13 ] - m[ 12 ] * m[ 5 ] * m[ 10 ] + m[ 12 ] * m[ 6 ] * m[ 9 ];
	inv[ 1 ] = -m[ 1 ] * m[ 10 ] * m[ 15 ] + m[ 1 ] * m[ 11 ] * m[ 14 ] + m[ 9 ] * m[ 2 ] * m[ 15 ] - m[ 9 ] * m[ 3 ] * m[ 14 ] - m[ 13 ] * m[ 2 ] * m[ 11 ] + m[ 13 ] * m[ 3 ] * m[ 10 ];
	inv[ 5 ] = m[ 0 ] * m[ 10 ] * m[ 15 ] - m[ 0 ] * m[ 11 ] * m[ 14 ] - m[ 8 ] * m[ 2 ] * m[ 15 ] + m[ 8 ] * m[ 3 ] * m[ 14 ] + m[ 12 ] * m[ 2 ] * m[ 11 ] - m[ 12 ] * m[ 3 ] * m[ 10 ];
	inv[ 9 ] = -m[ 0 ] * m[ 9 ] * m[ 15 ] + m[ 0 ] * m[ 11 ] * m[ 13 ] + m[ 8 ] * m[ 1 ] * m[ 15 ] - m[ 8 ] * m[ 3 ] * m[ 13 ] - m[ 12 ] * m[ 1 ] * m[ 11 ] + m[ 12 ] * m[ 3 ] * m[ 9 ];
	inv[ 13 ] = m[ 0 ] * m[ 9 ] * m[ 14 ] - m[ 0 ] * m[ 10 ] * m[ 13 ] - m[ 8 ] * m[ 1 ] * m[ 14 ] + m[ 8 ] * m[ 2 ] * m[ 13 ] + m[ 12 ] * m[ 1 ] * m[ 10 ] - m[ 12 ] * m[ 2 ] * m[ 9 ];
	inv[ 2 ] = m[ 1 ] * m[ 6 ] * m[ 15 ] - m[ 1 ] * m[ 7 ] * m[ 14 ] - m[ 5 ] * m[ 2 ] * m[ 15 ] + m[ 5 ] * m[ 3 ] * m[ 14 ] + m[ 13 ] * m[ 2 ] * m[ 7 ] - m[ 13 ] * m[ 3 ] * m[ 6 ];
	inv[ 6 ] = -m[ 0 ] * m[ 6 ] * m[ 15 ] + m[ 0 ] * m[ 7 ] * m[ 14 ] + m[ 4 ] * m[ 2 ] * m[ 15 ] - m[ 4 ] * m[ 3 ] * m[ 14 ] - m[ 12 ] * m[ 2 ] * m[ 7 ] + m[ 12 ] * m[ 3 ] * m[ 6 ];
	inv[ 10 ] = m[ 0 ] * m[ 5 ] * m[ 15 ] - m[ 0 ] * m[ 7 ] * m[ 13 ] - m[ 4 ] * m[ 1 ] * m[ 15 ] + m[ 4 ] * m[ 3 ] * m[ 13 ] + m[ 12 ] * m[ 1 ] * m[ 7 ] - m[ 12 ] * m[ 3 ] * m[ 5 ];
	inv[ 14 ] = -m[ 0 ] * m[ 5 ] * m[ 14 ] + m[ 0 ] * m[ 6 ] * m[ 13 ] + m[ 4 ] * m[ 1 ] * m[ 14 ] - m[ 4 ] * m[ 2 ] * m[ 13 ] - m[ 12 ] * m[ 1 ] * m[ 6 ] + m[ 12 ] * m[ 2 ] * m[ 5 ];
	inv[ 3 ] = -m[ 1 ] * m[ 6 ] * m[ 11 ] + m[ 1 ] * m[ 7 ] * m[ 10 ] + m[ 5 ] * m[ 2 ] * m[ 11 ] - m[ 5 ] * m[ 3 ] * m[ 10 ] - m[ 9 ] * m[ 2 ] * m[ 7 ] + m[ 9 ] * m[ 3 ] * m[ 6 ];
	inv[ 7 ] = m[ 0 ] * m[ 6 ] * m[ 11 ] - m[ 0 ] * m[ 7 ] * m[ 10 ] - m[ 4 ] * m[ 2 ] * m[ 11 ] + m[ 4 ] * m[ 3 ] * m[ 10 ] + m[ 8 ] * m[ 2 ] * m[ 7 ] - m[ 8 ] * m[ 3 ] * m[ 6 ];
	inv[ 11 ] = -m[ 0 ] * m[ 5 ] * m[ 11 ] + m[ 0 ] * m[ 7 ] * m[ 9 ] + m[ 4 ] * m[ 1 ] * m[ 11 ] - m[ 4 ] * m[ 3 ] * m[ 9 ] - m[ 8 ] * m[ 1 ] * m[ 7 ] + m[ 8 ] * m[ 3 ] * m[ 5 ];
	inv[ 15 ] = m[ 0 ] * m[ 5 ] * m[ 10 ] - m[ 0 ] * m[ 6 ] * m[ 9 ] - m[ 4 ] * m[ 1 ] * m[ 10 ] + m[ 4 ] * m[ 2 ] * m[ 9 ] + m[ 8 ] * m[ 1 ] * m[ 6 ] - m[ 8 ] * m[ 2 ] * m[ 5 ];

	float det = m[ 0 ] * inv[ 0 ] + m[ 1 ] * inv[ 4 ] + m[ 2 ] * inv[ 8 ] + m[ 3 ] * inv[ 12 ];
	Mat4 out;
	if( std::fabs( det ) < 1e-20f )
		return out;
	det = 1.0f / det;
	for( int i = 0; i < 16; ++i )
		out.m[ i ] = inv[ i ] * det;
	return out;
}
}//namespace m3d
