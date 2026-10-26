// SPDX-License-Identifier: GPL-3.0-or-later
// GLB Model Source - an FFGL source plugin for Resolume Arena
// Copyright (C) 2026 Elisa Bernard (Elisalien)

#pragma once
#include "GLHeaders.h"
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "Math3D.h"

/// A glTF model split in two halves:
///
/// - GltfModel::Asset holds everything parsed out of the file (geometry,
///   decoded textures, rest pose, animations). It is immutable once built,
///   lives in a process-wide cache and is shared between plugin instances and
///   worker threads. Building one never touches OpenGL.
/// - GltfModel itself is one instance's view of an asset: the animated pose
///   and the GL objects. Everything that touches OpenGL (UploadStep / Release /
///   ApplyIndices) must be called on the host's GL context.
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

	/// CPU geometry of one glTF triangle primitive.
	struct Mesh
	{
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
	};

	struct Level
	{
		int width = 0, height = 0;
		std::vector< unsigned char > pixels;//RGBA8
	};

	/// A decoded texture with its full mip chain, built on the loading thread:
	/// glGenerateMipmap on a 4K texture can stall the GPU for tens of ms.
	struct Image
	{
		bool srgb = false;
		std::vector< Level > levels;//empty when the image could not be decoded
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

	/// One index buffer per mesh, for a given density.
	typedef std::vector< std::vector< unsigned int > > IndexSet;

	/// Everything parsed out of one file. Read-only once built.
	struct Asset
	{
		std::string path;
		std::vector< Mesh > meshes;
		std::vector< Material > materials;
		std::vector< Image > images;
		std::vector< Node > nodes;//rest pose, globals already computed
		std::vector< int > roots;
		std::vector< Skin > skins;
		std::vector< Animation > animations;

		m3d::Vec3 boundsCenter;
		float boundsRadius   = 1.0f;
		size_t trianglesFull = 0;
		size_t gpuBytes      = 0;//vertex + index + texture bytes, for the log

		/// Simplified index buffers at the given density (]0,1[). Pure CPU and
		/// thread safe; the last few results are memoised so retriggering a
		/// decimated clip does not run the simplifier again.
		std::shared_ptr< const IndexSet > Decimate( float density ) const;

	private:
		mutable std::mutex decimateMutex;
		mutable std::vector< std::pair< int, std::shared_ptr< const IndexSet > > > decimated;
	};

	/// Parses a file, or hands back the cached asset when the same file (same
	/// size and modification time) was already loaded. Thread safe, no GL.
	/// forceReload skips the cache. Returns nullptr and fills error on failure.
	static std::shared_ptr< const Asset > Acquire( const std::string& path, bool forceReload, std::string& error );

	/// Points this instance at an asset and resets the pose. No GL calls; the
	/// GPU side is created afterwards by UploadStep. indices is the initial
	/// density (nullptr = full resolution).
	void Adopt( std::shared_ptr< const Asset > asset, std::shared_ptr< const IndexSet > indices );
	/// Sends pending GPU data in small chunks until about budgetMs of render
	/// thread time is used, so a big model is spread over several frames
	/// instead of stalling one. Returns true once everything is on the GPU.
	bool UploadStep( double budgetMs );
	bool IsUploaded() const { return uploaded; }

	/// Swaps the index buffers (nullptr = full resolution). GL thread only.
	void ApplyIndices( const IndexSet* set );

	/// Frees every GL object. Safe to call more than once.
	void Release();
	/// Frees GL objects and drops the asset.
	void Clear();

	/// Applies the animation clip at the given time and recomputes node
	/// world transforms + joint matrices. clip < 0 leaves the rest pose.
	void Evaluate( int clip, float timeSeconds );

	/// Joint matrices for a primitive's skin, ready for glUniformMatrix4fv.
	/// Returns nullptr when the primitive is not skinned.
	const float* JointData( int skinIndex ) const;
	int JointCount( int skinIndex ) const;

	/// Per-instance GL handles for one mesh.
	struct Primitive
	{
		GLuint vao = 0, vbo = 0, ibo = 0;
		int material = -1;
		int node     = -1;
		int skin     = -1;
		GLsizei activeIndexCount = 0;//index count currently in the GPU index buffer
	};

	bool IsLoaded() const { return asset != nullptr && uploaded; }
	const std::shared_ptr< const Asset >& GetAsset() const { return asset; }

	const std::vector< Primitive >& Primitives() const { return primitives; }
	const std::vector< Material >& Materials() const;
	const std::vector< Node >& Nodes() const { return nodes; }
	const std::vector< Animation >& Animations() const;
	GLuint TextureId( int index ) const;

	m3d::Vec3 BoundsCenter() const { return asset ? asset->boundsCenter : m3d::Vec3(); }
	float BoundsRadius() const { return asset ? asset->boundsRadius : 1.0f; }
	size_t TriangleCountFull() const { return asset ? asset->trianglesFull : 0; }
	size_t TriangleCountActive() const;

private:
	void UpdateGlobalTransforms();
	void UpdateJointMatrices();

	std::shared_ptr< const Asset > asset;
	std::shared_ptr< const IndexSet > pendingIndices;//density to upload with

	std::vector< Primitive > primitives;
	std::vector< GLuint > textures;
	std::vector< Node > nodes;
	std::vector< std::vector< float > > jointMatrices;//per skin, 16 floats per joint

	// upload cursor: primitives first, then images
	size_t uploadItem   = 0;
	size_t uploadOffset = 0;//bytes (buffers) or rows (images) already sent
	int uploadStage     = 0;//per primitive: 0 vertices, 1 indices; per image: mip level
	bool uploaded       = false;
};
