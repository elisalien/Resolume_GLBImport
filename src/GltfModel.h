// SPDX-License-Identifier: GPL-3.0-or-later
// GLB Model Source - an FFGL source plugin for Resolume Arena
// Copyright (C) 2026 Elisa Bernard (Elisalien)

#pragma once
#include "GLHeaders.h"
#include <string>
#include <vector>
#include "Math3D.h"

/// Loads a .glb / .gltf file, keeps a CPU copy for on-demand decimation,
/// samples node animations, and owns the GPU buffers used for drawing.
///
/// Everything that touches OpenGL (Upload / Release / vao ids) must be called
/// on the host's GL context, i.e. from InitGL / ProcessOpenGL / DeInitGL.
class GltfModel
{
public:
	// An enum rather than a static const int: it gets passed by reference to
	// std::min, which would otherwise need an out-of-line definition.
	enum : int
	{
		MAX_JOINTS = 128
	};

	struct Vertex
	{
		float px = 0.0f, py = 0.0f, pz = 0.0f;
		float nx = 0.0f, ny = 0.0f, nz = 1.0f;
		float u = 0.0f, v = 0.0f;
		float j0 = 0.0f, j1 = 0.0f, j2 = 0.0f, j3 = 0.0f;
		float w0 = 0.0f, w1 = 0.0f, w2 = 0.0f, w3 = 0.0f;
	};

	struct Material
	{
		float baseColor[ 4 ] = { 1.0f, 1.0f, 1.0f, 1.0f };
		float emissive[ 3 ]  = { 0.0f, 0.0f, 0.0f };
		float metallic       = 1.0f;
		float roughness      = 1.0f;
		float alphaCutoff    = 0.5f;
		int alphaMode        = 0;//0 opaque, 1 mask, 2 blend
		bool doubleSided     = false;
		int texBaseColor     = -1;
		int texNormal        = -1;
		int texMetalRough    = -1;
		int texEmissive      = -1;
	};

	struct Primitive
	{
		GLuint vao = 0, vbo = 0, ibo = 0;
		int material = -1;
		int node     = -1;//node this primitive is attached to
		int skin     = -1;//index into skins, -1 when not skinned

		std::vector< Vertex > vertices;
		std::vector< unsigned int > indices;//full resolution

		// Position-welded copy, prepared once at load time so that decimation
		// is not defeated by normal / uv seams splitting the vertices.
		std::vector< float > weldedPos;
		std::vector< unsigned int > weldedIdx;
		std::vector< unsigned int > representative;//welded index -> original vertex index

		GLsizei activeIndexCount = 0;//index count currently in the GPU index buffer
	};

	struct Node
	{
		std::string name;
		int parent = -1;
		std::vector< int > children;
		m3d::Vec3 translation;
		m3d::Quat rotation;
		m3d::Vec3 scale = m3d::Vec3( 1.0f, 1.0f, 1.0f );
		m3d::Mat4 local;  //composed from TRS (or the node's matrix when it has one)
		m3d::Mat4 global; //world transform, recomputed each frame
		bool hasMatrix = false;
	};

	struct Skin
	{
		std::vector< int > joints;
		std::vector< m3d::Mat4 > inverseBind;
		int skeletonRoot = -1;
	};

	enum class Interp
	{
		Linear,
		Step,
		CubicSpline
	};
	enum class Path
	{
		Translation,
		Rotation,
		Scale
	};

	struct Channel
	{
		int node = -1;
		Path path = Path::Translation;
		Interp interp = Interp::Linear;
		std::vector< float > times;
		std::vector< float > values;//3 floats per key (T/S) or 4 (R); x3 when cubic spline
	};

	struct Animation
	{
		std::string name;
		std::vector< Channel > channels;
		float duration = 0.0f;
	};

	GltfModel() = default;
	~GltfModel();

	/// Parses the file and prepares CPU-side geometry. No GL calls, safe to fail.
	/// Returns false and fills lastError() when the file cannot be used.
	bool Load( const std::string& path );
	/// Creates the GL objects for the geometry currently held on the CPU.
	bool Upload();
	/// Frees every GL object. Safe to call more than once.
	void Release();
	/// Drops CPU and GPU data entirely.
	void Clear();

	/// Rebuilds every primitive's index buffer at the requested density.
	/// density is in ]0,1]; 1 keeps the original triangles.
	void SetDensity( float density );
	float Density() const { return currentDensity; }

	/// Applies the animation clip at the given time and recomputes node
	/// world transforms + joint matrices. clip < 0 leaves the rest pose.
	void Evaluate( int clip, float timeSeconds );

	/// Joint matrices for a primitive's skin, ready for glUniformMatrix4fv.
	/// Returns nullptr when the primitive is not skinned.
	const float* JointData( int skinIndex ) const;
	int JointCount( int skinIndex ) const;

	bool IsLoaded() const { return loaded; }
	const std::string& LastError() const { return lastError; }
	const std::string& SourcePath() const { return sourcePath; }

	const std::vector< Primitive >& Primitives() const { return primitives; }
	const std::vector< Material >& Materials() const { return materials; }
	const std::vector< Node >& Nodes() const { return nodes; }
	const std::vector< Animation >& Animations() const { return animations; }
	GLuint TextureId( int index ) const;

	m3d::Vec3 BoundsCenter() const { return boundsCenter; }
	float BoundsRadius() const { return boundsRadius; }
	size_t TriangleCountFull() const { return trianglesFull; }
	size_t TriangleCountActive() const;

private:
	struct TextureSlot
	{
		GLuint id = 0;
		int width = 0, height = 0, channels = 0;
		std::vector< unsigned char > pixels;//kept until Upload()
		bool srgb = false;
	};

	void BuildWeldedStreams( Primitive& prim );
	void ComputeBounds();
	void UpdateGlobalTransforms();
	void UpdateJointMatrices();

	bool loaded = false;
	std::string lastError;
	std::string sourcePath;

	std::vector< Primitive > primitives;
	std::vector< Material > materials;
	std::vector< TextureSlot > textures;
	std::vector< Node > nodes;
	std::vector< int > roots;
	std::vector< Skin > skins;
	std::vector< Animation > animations;
	std::vector< std::vector< float > > jointMatrices;//per skin, 16 floats per joint

	m3d::Vec3 boundsCenter;
	float boundsRadius   = 1.0f;
	size_t trianglesFull = 0;
	float currentDensity = 1.0f;
	bool uploaded        = false;
};
