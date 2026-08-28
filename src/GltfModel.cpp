// SPDX-License-Identifier: GPL-3.0-or-later
// GLB Model Source - an FFGL source plugin for Resolume Arena
// Copyright (C) 2026 Elisa Bernard (Elisalien)

#include "GltfModel.h"

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_TGA
#define STBI_ONLY_BMP
#define STBI_NO_STDIO_WRITE
#include "stb_image.h"

#include "meshoptimizer.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

using namespace m3d;

namespace
{
/// Minimal base64 decoder, used for glTF images stored as data: URIs.
std::vector< unsigned char > DecodeBase64( const char* src )
{
	static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	int lut[ 256 ];
	for( int i = 0; i < 256; ++i )
		lut[ i ] = -1;
	for( int i = 0; i < 64; ++i )
		lut[ (unsigned char)table[ i ] ] = i;

	std::vector< unsigned char > out;
	int val = 0, bits = -8;
	for( const char* p = src; *p; ++p )
	{
		int c = lut[ (unsigned char)*p ];
		if( c < 0 )
		{
			if( *p == '=' )
				break;
			continue;//skip whitespace and padding noise
		}
		val = ( val << 6 ) + c;
		bits += 6;
		if( bits >= 0 )
		{
			out.push_back( (unsigned char)( ( val >> bits ) & 0xFF ) );
			bits -= 8;
		}
	}
	return out;
}

std::string DirectoryOf( const std::string& path )
{
	size_t cut = path.find_last_of( "/\\" );
	return cut == std::string::npos ? std::string() : path.substr( 0, cut + 1 );
}

/// Percent-decoding, because glTF uris are URI-escaped ("my%20texture.png").
std::string UriDecode( const std::string& in )
{
	std::string out;
	out.reserve( in.size() );
	for( size_t i = 0; i < in.size(); ++i )
	{
		if( in[ i ] == '%' && i + 2 < in.size() )
		{
			int hi = std::isxdigit( (unsigned char)in[ i + 1 ] ) ? ( std::isdigit( (unsigned char)in[ i + 1 ] ) ? in[ i + 1 ] - '0' : ( std::tolower( in[ i + 1 ] ) - 'a' + 10 ) ) : -1;
			int lo = std::isxdigit( (unsigned char)in[ i + 2 ] ) ? ( std::isdigit( (unsigned char)in[ i + 2 ] ) ? in[ i + 2 ] - '0' : ( std::tolower( in[ i + 2 ] ) - 'a' + 10 ) ) : -1;
			if( hi >= 0 && lo >= 0 )
			{
				out.push_back( (char)( hi * 16 + lo ) );
				i += 2;
				continue;
			}
		}
		out.push_back( in[ i ] );
	}
	return out;
}

int IndexOfNode( const cgltf_data* data, const cgltf_node* node )
{
	if( node == nullptr )
		return -1;
	return (int)( node - data->nodes );
}
}//namespace

GltfModel::~GltfModel()
{
	// GL objects must already be gone; Release() needs a live context.
	primitives.clear();
	textures.clear();
}

void GltfModel::Release()
{
	for( Primitive& p : primitives )
	{
		if( p.vao )
			glDeleteVertexArrays( 1, &p.vao );
		if( p.vbo )
			glDeleteBuffers( 1, &p.vbo );
		if( p.ibo )
			glDeleteBuffers( 1, &p.ibo );
		p.vao = p.vbo = p.ibo = 0;
	}
	for( TextureSlot& t : textures )
	{
		if( t.id )
			glDeleteTextures( 1, &t.id );
		t.id = 0;
	}
	uploaded = false;
}

void GltfModel::Clear()
{
	Release();
	primitives.clear();
	materials.clear();
	textures.clear();
	nodes.clear();
	roots.clear();
	skins.clear();
	animations.clear();
	jointMatrices.clear();
	loaded         = false;
	trianglesFull  = 0;
	currentDensity = 1.0f;
	boundsCenter   = Vec3();
	boundsRadius   = 1.0f;
	sourcePath.clear();
}

GLuint GltfModel::TextureId( int index ) const
{
	if( index < 0 || index >= (int)textures.size() )
		return 0;
	return textures[ index ].id;
}

size_t GltfModel::TriangleCountActive() const
{
	size_t n = 0;
	for( const Primitive& p : primitives )
		n += (size_t)p.activeIndexCount / 3;
	return n;
}

bool GltfModel::Load( const std::string& path )
{
	Clear();
	lastError.clear();

	if( path.empty() )
	{
		lastError = "No file selected";
		return false;
	}

	cgltf_options options;
	std::memset( &options, 0, sizeof( options ) );

	cgltf_data* data = nullptr;
	cgltf_result res = cgltf_parse_file( &options, path.c_str(), &data );
	if( res != cgltf_result_success )
	{
		lastError = "Could not parse " + path;
		return false;
	}
	res = cgltf_load_buffers( &options, data, path.c_str() );
	if( res != cgltf_result_success )
	{
		cgltf_free( data );
		lastError = "Could not load buffers (missing .bin next to the .gltf?)";
		return false;
	}
	if( cgltf_validate( data ) != cgltf_result_success )
	{
		cgltf_free( data );
		lastError = "glTF failed validation";
		return false;
	}

	const std::string baseDir = DirectoryOf( path );

	//------------------------------------------------------------------ images
	textures.resize( data->images_count );
	for( size_t i = 0; i < data->images_count; ++i )
	{
		const cgltf_image& img = data->images[ i ];
		TextureSlot& slot      = textures[ i ];

		std::vector< unsigned char > encoded;
		const unsigned char* bytes = nullptr;
		size_t byteCount           = 0;

		if( img.buffer_view && img.buffer_view->buffer && img.buffer_view->buffer->data )
		{
			bytes     = (const unsigned char*)img.buffer_view->buffer->data + img.buffer_view->offset;
			byteCount = img.buffer_view->size;
		}
		else if( img.uri )
		{
			if( std::strncmp( img.uri, "data:", 5 ) == 0 )
			{
				const char* comma = std::strchr( img.uri, ',' );
				if( comma )
				{
					encoded   = DecodeBase64( comma + 1 );
					bytes     = encoded.data();
					byteCount = encoded.size();
				}
			}
			else
			{
				std::string file = baseDir + UriDecode( img.uri );
				FILE* f          = std::fopen( file.c_str(), "rb" );
				if( f )
				{
					std::fseek( f, 0, SEEK_END );
					long size = std::ftell( f );
					std::fseek( f, 0, SEEK_SET );
					if( size > 0 )
					{
						encoded.resize( (size_t)size );
						if( std::fread( encoded.data(), 1, (size_t)size, f ) == (size_t)size )
						{
							bytes     = encoded.data();
							byteCount = encoded.size();
						}
					}
					std::fclose( f );
				}
			}
		}

		if( bytes && byteCount )
		{
			int w = 0, h = 0, comp = 0;
			unsigned char* pixels = stbi_load_from_memory( bytes, (int)byteCount, &w, &h, &comp, 4 );
			if( pixels )
			{
				slot.width    = w;
				slot.height   = h;
				slot.channels = 4;
				slot.pixels.assign( pixels, pixels + (size_t)w * h * 4 );
				stbi_image_free( pixels );
			}
		}
	}

	//--------------------------------------------------------------- materials
	materials.resize( data->materials_count );
	for( size_t i = 0; i < data->materials_count; ++i )
	{
		const cgltf_material& src = data->materials[ i ];
		Material& dst             = materials[ i ];

		if( src.has_pbr_metallic_roughness )
		{
			const cgltf_pbr_metallic_roughness& pbr = src.pbr_metallic_roughness;
			for( int c = 0; c < 4; ++c )
				dst.baseColor[ c ] = pbr.base_color_factor[ c ];
			dst.metallic  = pbr.metallic_factor;
			dst.roughness = pbr.roughness_factor;
			if( pbr.base_color_texture.texture && pbr.base_color_texture.texture->image )
			{
				dst.texBaseColor = (int)( pbr.base_color_texture.texture->image - data->images );
				if( dst.texBaseColor >= 0 && dst.texBaseColor < (int)textures.size() )
					textures[ dst.texBaseColor ].srgb = true;
			}
			if( pbr.metallic_roughness_texture.texture && pbr.metallic_roughness_texture.texture->image )
				dst.texMetalRough = (int)( pbr.metallic_roughness_texture.texture->image - data->images );
		}
		if( src.normal_texture.texture && src.normal_texture.texture->image )
			dst.texNormal = (int)( src.normal_texture.texture->image - data->images );
		if( src.emissive_texture.texture && src.emissive_texture.texture->image )
		{
			dst.texEmissive = (int)( src.emissive_texture.texture->image - data->images );
			if( dst.texEmissive >= 0 && dst.texEmissive < (int)textures.size() )
				textures[ dst.texEmissive ].srgb = true;
		}
		for( int c = 0; c < 3; ++c )
			dst.emissive[ c ] = src.emissive_factor[ c ];
		dst.alphaCutoff  = src.alpha_cutoff;
		dst.doubleSided  = src.double_sided != 0;
		dst.alphaMode    = src.alpha_mode == cgltf_alpha_mode_mask ? 1 : ( src.alpha_mode == cgltf_alpha_mode_blend ? 2 : 0 );
	}

	//------------------------------------------------------------------- nodes
	nodes.resize( data->nodes_count );
	for( size_t i = 0; i < data->nodes_count; ++i )
	{
		const cgltf_node& src = data->nodes[ i ];
		Node& dst             = nodes[ i ];
		dst.name              = src.name ? src.name : "";
		dst.parent            = IndexOfNode( data, src.parent );
		for( size_t c = 0; c < src.children_count; ++c )
			dst.children.push_back( IndexOfNode( data, src.children[ c ] ) );

		if( src.has_matrix )
		{
			dst.hasMatrix = true;
			std::memcpy( dst.local.m, src.matrix, sizeof( float ) * 16 );
		}
		else
		{
			dst.translation = Vec3( src.translation[ 0 ], src.translation[ 1 ], src.translation[ 2 ] );
			dst.rotation    = Quat( src.rotation[ 0 ], src.rotation[ 1 ], src.rotation[ 2 ], src.rotation[ 3 ] );
			dst.scale       = Vec3( src.scale[ 0 ], src.scale[ 1 ], src.scale[ 2 ] );
			dst.local       = trs( dst.translation, dst.rotation, dst.scale );
		}
		if( dst.parent < 0 )
			roots.push_back( (int)i );
	}

	//------------------------------------------------------------------- skins
	skins.resize( data->skins_count );
	for( size_t i = 0; i < data->skins_count; ++i )
	{
		const cgltf_skin& src = data->skins[ i ];
		Skin& dst             = skins[ i ];
		dst.skeletonRoot      = IndexOfNode( data, src.skeleton );
		dst.joints.reserve( src.joints_count );
		for( size_t j = 0; j < src.joints_count; ++j )
			dst.joints.push_back( IndexOfNode( data, src.joints[ j ] ) );

		dst.inverseBind.assign( src.joints_count, Mat4() );
		if( src.inverse_bind_matrices )
		{
			for( size_t j = 0; j < src.joints_count; ++j )
				cgltf_accessor_read_float( src.inverse_bind_matrices, j, dst.inverseBind[ j ].m, 16 );
		}
	}
	jointMatrices.resize( skins.size() );
	for( size_t i = 0; i < skins.size(); ++i )
		jointMatrices[ i ].assign( skins[ i ].joints.size() * 16, 0.0f );

	//-------------------------------------------------------------- primitives
	for( size_t n = 0; n < data->nodes_count; ++n )
	{
		const cgltf_node& node = data->nodes[ n ];
		if( node.mesh == nullptr )
			continue;
		const int skinIndex = node.skin ? (int)( node.skin - data->skins ) : -1;

		for( size_t p = 0; p < node.mesh->primitives_count; ++p )
		{
			const cgltf_primitive& src = node.mesh->primitives[ p ];
			if( src.type != cgltf_primitive_type_triangles )
				continue;

			const cgltf_accessor* aPos = nullptr;
			const cgltf_accessor* aNrm = nullptr;
			const cgltf_accessor* aUV  = nullptr;
			const cgltf_accessor* aJnt = nullptr;
			const cgltf_accessor* aWgt = nullptr;
			for( size_t a = 0; a < src.attributes_count; ++a )
			{
				const cgltf_attribute& at = src.attributes[ a ];
				if( at.type == cgltf_attribute_type_position )
					aPos = at.data;
				else if( at.type == cgltf_attribute_type_normal )
					aNrm = at.data;
				else if( at.type == cgltf_attribute_type_texcoord && at.index == 0 )
					aUV = at.data;
				else if( at.type == cgltf_attribute_type_joints && at.index == 0 )
					aJnt = at.data;
				else if( at.type == cgltf_attribute_type_weights && at.index == 0 )
					aWgt = at.data;
			}
			if( aPos == nullptr || aPos->count == 0 )
				continue;

			Primitive prim;
			prim.material = src.material ? (int)( src.material - data->materials ) : -1;
			prim.node     = (int)n;
			prim.skin     = skinIndex;
			prim.vertices.resize( aPos->count );

			for( size_t v = 0; v < aPos->count; ++v )
			{
				Vertex& vert = prim.vertices[ v ];
				float tmp[ 4 ] = { 0.0f, 0.0f, 0.0f, 0.0f };

				cgltf_accessor_read_float( aPos, v, tmp, 3 );
				vert.px = tmp[ 0 ];
				vert.py = tmp[ 1 ];
				vert.pz = tmp[ 2 ];

				if( aNrm )
				{
					cgltf_accessor_read_float( aNrm, v, tmp, 3 );
					vert.nx = tmp[ 0 ];
					vert.ny = tmp[ 1 ];
					vert.nz = tmp[ 2 ];
				}
				if( aUV )
				{
					cgltf_accessor_read_float( aUV, v, tmp, 2 );
					vert.u = tmp[ 0 ];
					vert.v = tmp[ 1 ];
				}
				if( aJnt )
				{
					cgltf_uint j[ 4 ] = { 0, 0, 0, 0 };
					cgltf_accessor_read_uint( aJnt, v, j, 4 );
					vert.j0 = (float)j[ 0 ];
					vert.j1 = (float)j[ 1 ];
					vert.j2 = (float)j[ 2 ];
					vert.j3 = (float)j[ 3 ];
				}
				if( aWgt )
				{
					cgltf_accessor_read_float( aWgt, v, tmp, 4 );
					vert.w0 = tmp[ 0 ];
					vert.w1 = tmp[ 1 ];
					vert.w2 = tmp[ 2 ];
					vert.w3 = tmp[ 3 ];
				}
			}

			if( src.indices && src.indices->count )
			{
				prim.indices.resize( src.indices->count );
				for( size_t i = 0; i < src.indices->count; ++i )
					prim.indices[ i ] = (unsigned int)cgltf_accessor_read_index( src.indices, i );
			}
			else
			{
				prim.indices.resize( aPos->count );
				for( size_t i = 0; i < aPos->count; ++i )
					prim.indices[ i ] = (unsigned int)i;
			}
			if( prim.indices.size() < 3 )
				continue;
			prim.indices.resize( ( prim.indices.size() / 3 ) * 3 );

			// Missing normals: accumulate face normals so flat and lit modes still work.
			if( aNrm == nullptr )
			{
				for( Vertex& vert : prim.vertices )
					vert.nx = vert.ny = vert.nz = 0.0f;
				for( size_t i = 0; i + 2 < prim.indices.size(); i += 3 )
				{
					Vertex& a = prim.vertices[ prim.indices[ i ] ];
					Vertex& b = prim.vertices[ prim.indices[ i + 1 ] ];
					Vertex& c = prim.vertices[ prim.indices[ i + 2 ] ];
					Vec3 fn   = cross( Vec3( b.px - a.px, b.py - a.py, b.pz - a.pz ),
					                   Vec3( c.px - a.px, c.py - a.py, c.pz - a.pz ) );
					a.nx += fn.x; a.ny += fn.y; a.nz += fn.z;
					b.nx += fn.x; b.ny += fn.y; b.nz += fn.z;
					c.nx += fn.x; c.ny += fn.y; c.nz += fn.z;
				}
				for( Vertex& vert : prim.vertices )
				{
					Vec3 nn = normalize( Vec3( vert.nx, vert.ny, vert.nz ) );
					vert.nx = nn.x;
					vert.ny = nn.y;
					vert.nz = nn.z;
				}
			}

			trianglesFull += prim.indices.size() / 3;
			BuildWeldedStreams( prim );
			prim.activeIndexCount = (GLsizei)prim.indices.size();
			primitives.push_back( std::move( prim ) );
		}
	}

	//-------------------------------------------------------------- animations
	animations.resize( data->animations_count );
	for( size_t i = 0; i < data->animations_count; ++i )
	{
		const cgltf_animation& src = data->animations[ i ];
		Animation& dst             = animations[ i ];
		char fallback[ 32 ];
		std::snprintf( fallback, sizeof( fallback ), "Clip %d", (int)i + 1 );
		dst.name = src.name && src.name[ 0 ] ? src.name : fallback;

		for( size_t c = 0; c < src.channels_count; ++c )
		{
			const cgltf_animation_channel& ch = src.channels[ c ];
			if( ch.sampler == nullptr || ch.target_node == nullptr )
				continue;
			if( ch.target_path != cgltf_animation_path_type_translation &&
			    ch.target_path != cgltf_animation_path_type_rotation &&
			    ch.target_path != cgltf_animation_path_type_scale )
				continue;//weights (morph targets) are not supported yet

			Channel out;
			out.node = IndexOfNode( data, ch.target_node );
			out.path = ch.target_path == cgltf_animation_path_type_translation ? Path::Translation
			         : ( ch.target_path == cgltf_animation_path_type_rotation ? Path::Rotation : Path::Scale );
			out.interp = ch.sampler->interpolation == cgltf_interpolation_type_step ? Interp::Step
			           : ( ch.sampler->interpolation == cgltf_interpolation_type_cubic_spline ? Interp::CubicSpline : Interp::Linear );

			const cgltf_accessor* in  = ch.sampler->input;
			const cgltf_accessor* val = ch.sampler->output;
			if( in == nullptr || val == nullptr || in->count == 0 )
				continue;

			out.times.resize( in->count );
			for( size_t k = 0; k < in->count; ++k )
				cgltf_accessor_read_float( in, k, &out.times[ k ], 1 );

			const int comps = out.path == Path::Rotation ? 4 : 3;
			out.values.resize( val->count * comps );
			for( size_t k = 0; k < val->count; ++k )
				cgltf_accessor_read_float( val, k, &out.values[ k * comps ], comps );

			dst.duration = std::max( dst.duration, out.times.back() );
			dst.channels.push_back( std::move( out ) );
		}
		if( dst.duration <= 0.0f )
			dst.duration = 1.0f;
	}

	cgltf_free( data );

	if( primitives.empty() )
	{
		lastError = "No triangle geometry found in this file";
		return false;
	}

	UpdateGlobalTransforms();
	ComputeBounds();
	UpdateJointMatrices();
	loaded         = true;
	sourcePath     = path;
	currentDensity = 1.0f;
	return true;
}

void GltfModel::BuildWeldedStreams( Primitive& prim )
{
	// glTF exports usually split vertices on normal / uv seams. Feeding that
	// straight into the simplifier locks almost every edge and nothing gets
	// removed, so we first weld on position only and simplify that topology.
	const size_t vertexCount = prim.vertices.size();
	const size_t indexCount  = prim.indices.size();

	std::vector< unsigned int > remap( vertexCount );
	meshopt_Stream stream;
	stream.data   = &prim.vertices[ 0 ].px;
	stream.size   = sizeof( float ) * 3;
	stream.stride = sizeof( Vertex );

	const size_t weldedCount = meshopt_generateVertexRemapMulti(
		remap.data(), prim.indices.data(), indexCount, vertexCount, &stream, 1 );

	prim.weldedPos.assign( weldedCount * 3, 0.0f );
	prim.representative.assign( weldedCount, 0 );
	std::vector< unsigned char > seen( weldedCount, 0 );

	for( size_t v = 0; v < vertexCount; ++v )
	{
		const unsigned int w = remap[ v ];
		if( w >= weldedCount )
			continue;
		if( !seen[ w ] )
		{
			seen[ w ]                 = 1;
			prim.representative[ w ]  = (unsigned int)v;
			prim.weldedPos[ w * 3 ]     = prim.vertices[ v ].px;
			prim.weldedPos[ w * 3 + 1 ] = prim.vertices[ v ].py;
			prim.weldedPos[ w * 3 + 2 ] = prim.vertices[ v ].pz;
		}
	}

	prim.weldedIdx.resize( indexCount );
	for( size_t i = 0; i < indexCount; ++i )
		prim.weldedIdx[ i ] = remap[ prim.indices[ i ] ];
}

void GltfModel::SetDensity( float density )
{
	density = clampf( density, 0.001f, 1.0f );
	if( std::fabs( density - currentDensity ) < 1e-4f )
		return;
	currentDensity = density;
	if( !uploaded )
		return;

	std::vector< unsigned int > simplified;
	std::vector< unsigned int > mapped;

	for( Primitive& prim : primitives )
	{
		if( density >= 0.999f || prim.weldedIdx.size() < 6 )
		{
			glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, prim.ibo );
			glBufferData( GL_ELEMENT_ARRAY_BUFFER,
			              (GLsizeiptr)( prim.indices.size() * sizeof( unsigned int ) ),
			              prim.indices.data(), GL_DYNAMIC_DRAW );
			prim.activeIndexCount = (GLsizei)prim.indices.size();
			continue;
		}

		size_t targetIndexCount = (size_t)( ( prim.weldedIdx.size() / 3 ) * density ) * 3;
		if( targetIndexCount < 3 )
			targetIndexCount = 3;

		simplified.resize( prim.weldedIdx.size() );
		float resultError = 0.0f;
		const size_t count = meshopt_simplify(
			simplified.data(),
			prim.weldedIdx.data(), prim.weldedIdx.size(),
			prim.weldedPos.data(), prim.weldedPos.size() / 3, sizeof( float ) * 3,
			targetIndexCount, 1.0f /*target error: let it go as far as it needs*/,
			meshopt_SimplifyLockBorder, &resultError );
		simplified.resize( count );

		// Back to the original vertex ids so normals, uvs and skin weights survive.
		mapped.resize( count );
		for( size_t i = 0; i < count; ++i )
			mapped[ i ] = prim.representative[ simplified[ i ] ];

		if( !mapped.empty() )
			meshopt_optimizeVertexCache( mapped.data(), mapped.data(), mapped.size(), prim.vertices.size() );

		glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, prim.ibo );
		glBufferData( GL_ELEMENT_ARRAY_BUFFER,
		              (GLsizeiptr)( mapped.size() * sizeof( unsigned int ) ),
		              mapped.empty() ? nullptr : mapped.data(), GL_DYNAMIC_DRAW );
		prim.activeIndexCount = (GLsizei)mapped.size();
	}
	glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, 0 );
}

bool GltfModel::Upload()
{
	if( !loaded )
		return false;
	Release();

	for( Primitive& prim : primitives )
	{
		glGenVertexArrays( 1, &prim.vao );
		glGenBuffers( 1, &prim.vbo );
		glGenBuffers( 1, &prim.ibo );

		glBindVertexArray( prim.vao );
		glBindBuffer( GL_ARRAY_BUFFER, prim.vbo );
		glBufferData( GL_ARRAY_BUFFER,
		              (GLsizeiptr)( prim.vertices.size() * sizeof( Vertex ) ),
		              prim.vertices.data(), GL_STATIC_DRAW );

		const GLsizei stride = (GLsizei)sizeof( Vertex );
		glEnableVertexAttribArray( 0 );
		glVertexAttribPointer( 0, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof( Vertex, px ) );
		glEnableVertexAttribArray( 1 );
		glVertexAttribPointer( 1, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof( Vertex, nx ) );
		glEnableVertexAttribArray( 2 );
		glVertexAttribPointer( 2, 2, GL_FLOAT, GL_FALSE, stride, (void*)offsetof( Vertex, u ) );
		glEnableVertexAttribArray( 3 );
		glVertexAttribPointer( 3, 4, GL_FLOAT, GL_FALSE, stride, (void*)offsetof( Vertex, j0 ) );
		glEnableVertexAttribArray( 4 );
		glVertexAttribPointer( 4, 4, GL_FLOAT, GL_FALSE, stride, (void*)offsetof( Vertex, w0 ) );

		glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, prim.ibo );
		glBufferData( GL_ELEMENT_ARRAY_BUFFER,
		              (GLsizeiptr)( prim.indices.size() * sizeof( unsigned int ) ),
		              prim.indices.data(), GL_DYNAMIC_DRAW );
		prim.activeIndexCount = (GLsizei)prim.indices.size();

		glBindVertexArray( 0 );
		glBindBuffer( GL_ARRAY_BUFFER, 0 );
		glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, 0 );
	}

	for( TextureSlot& slot : textures )
	{
		if( slot.pixels.empty() )
			continue;
		glGenTextures( 1, &slot.id );
		glBindTexture( GL_TEXTURE_2D, slot.id );
		glTexImage2D( GL_TEXTURE_2D, 0, slot.srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8,
		              slot.width, slot.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, slot.pixels.data() );
		glGenerateMipmap( GL_TEXTURE_2D );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT );
		glBindTexture( GL_TEXTURE_2D, 0 );
		slot.pixels.clear();
		slot.pixels.shrink_to_fit();
	}

	uploaded       = true;
	currentDensity = 1.0f;
	return true;
}

void GltfModel::ComputeBounds()
{
	Vec3 lo( 1e30f, 1e30f, 1e30f );
	Vec3 hi( -1e30f, -1e30f, -1e30f );
	bool any = false;

	for( const Primitive& prim : primitives )
	{
		const Mat4& world = ( prim.node >= 0 && prim.node < (int)nodes.size() ) ? nodes[ prim.node ].global : Mat4();
		for( const Vertex& v : prim.vertices )
		{
			Vec3 p = world.transformPoint( Vec3( v.px, v.py, v.pz ) );
			lo.x = std::min( lo.x, p.x ); lo.y = std::min( lo.y, p.y ); lo.z = std::min( lo.z, p.z );
			hi.x = std::max( hi.x, p.x ); hi.y = std::max( hi.y, p.y ); hi.z = std::max( hi.z, p.z );
			any = true;
		}
	}
	if( !any )
	{
		boundsCenter = Vec3();
		boundsRadius = 1.0f;
		return;
	}
	boundsCenter = ( lo + hi ) * 0.5f;
	boundsRadius = std::max( 1e-4f, length( ( hi - lo ) * 0.5f ) );
}

void GltfModel::UpdateGlobalTransforms()
{
	// nodes are stored in glTF order; parents are not guaranteed to come first,
	// so walk the hierarchy from the roots.
	std::vector< int > stack( roots.begin(), roots.end() );
	while( !stack.empty() )
	{
		const int index = stack.back();
		stack.pop_back();
		if( index < 0 || index >= (int)nodes.size() )
			continue;
		Node& node  = nodes[ index ];
		node.global = node.parent >= 0 ? nodes[ node.parent ].global * node.local : node.local;
		for( int child : node.children )
			stack.push_back( child );
	}
}

void GltfModel::UpdateJointMatrices()
{
	for( size_t s = 0; s < skins.size(); ++s )
	{
		const Skin& skin = skins[ s ];
		std::vector< float >& out = jointMatrices[ s ];
		const size_t count = std::min( skin.joints.size(), (size_t)MAX_JOINTS );
		out.assign( count * 16, 0.0f );
		for( size_t j = 0; j < count; ++j )
		{
			const int nodeIndex = skin.joints[ j ];
			Mat4 g;
			if( nodeIndex >= 0 && nodeIndex < (int)nodes.size() )
				g = nodes[ nodeIndex ].global;
			const Mat4 jm = g * skin.inverseBind[ j ];
			std::memcpy( &out[ j * 16 ], jm.m, sizeof( float ) * 16 );
		}
	}
}

const float* GltfModel::JointData( int skinIndex ) const
{
	if( skinIndex < 0 || skinIndex >= (int)jointMatrices.size() || jointMatrices[ skinIndex ].empty() )
		return nullptr;
	return jointMatrices[ skinIndex ].data();
}

int GltfModel::JointCount( int skinIndex ) const
{
	if( skinIndex < 0 || skinIndex >= (int)jointMatrices.size() )
		return 0;
	return (int)( jointMatrices[ skinIndex ].size() / 16 );
}

namespace
{
/// Finds the key index k such that times[k] <= t < times[k+1].
size_t FindKey( const std::vector< float >& times, float t )
{
	if( times.size() < 2 || t <= times.front() )
		return 0;
	if( t >= times.back() )
		return times.size() - 2;
	size_t lo = 0, hi = times.size() - 1;
	while( hi - lo > 1 )
	{
		const size_t mid = ( lo + hi ) / 2;
		if( times[ mid ] <= t )
			lo = mid;
		else
			hi = mid;
	}
	return lo;
}

float CubicHermite( float p0, float m0, float p1, float m1, float s, float dt )
{
	const float s2 = s * s;
	const float s3 = s2 * s;
	return ( 2.0f * s3 - 3.0f * s2 + 1.0f ) * p0
	     + dt * ( s3 - 2.0f * s2 + s ) * m0
	     + ( -2.0f * s3 + 3.0f * s2 ) * p1
	     + dt * ( s3 - s2 ) * m1;
}
}//namespace

void GltfModel::Evaluate( int clip, float timeSeconds )
{
	// Rest pose first: channels only touch the nodes they animate.
	for( Node& node : nodes )
	{
		if( !node.hasMatrix )
			node.local = trs( node.translation, node.rotation, node.scale );
	}

	if( clip >= 0 && clip < (int)animations.size() )
	{
		const Animation& anim = animations[ clip ];
		const float duration  = anim.duration > 1e-5f ? anim.duration : 1.0f;
		float t               = std::fmod( timeSeconds, duration );
		if( t < 0.0f )
			t += duration;

		struct Pose
		{
			Vec3 translation;
			Quat rotation;
			Vec3 scale;
			bool hasT = false, hasR = false, hasS = false;
		};
		std::vector< Pose > pose( nodes.size() );

		for( const Channel& ch : anim.channels )
		{
			if( ch.node < 0 || ch.node >= (int)nodes.size() || ch.times.empty() )
				continue;
			const int comps    = ch.path == Path::Rotation ? 4 : 3;
			const bool cubic   = ch.interp == Interp::CubicSpline;
			const int perKey   = cubic ? comps * 3 : comps;
			const size_t keys  = ch.values.size() / ( size_t )perKey;
			if( keys == 0 )
				continue;

			float out[ 4 ] = { 0.0f, 0.0f, 0.0f, 1.0f };

			if( keys == 1 || ch.times.size() == 1 )
			{
				const float* v = &ch.values[ cubic ? comps : 0 ];
				for( int c = 0; c < comps; ++c )
					out[ c ] = v[ c ];
			}
			else
			{
				const size_t k  = FindKey( ch.times, t );
				const size_t k1 = std::min( k + 1, ch.times.size() - 1 );
				const float t0  = ch.times[ k ];
				const float t1  = ch.times[ k1 ];
				const float dt  = t1 - t0;
				float s         = dt > 1e-8f ? clampf( ( t - t0 ) / dt, 0.0f, 1.0f ) : 0.0f;

				if( ch.interp == Interp::Step )
				{
					const float* v = &ch.values[ k * perKey + ( cubic ? comps : 0 ) ];
					for( int c = 0; c < comps; ++c )
						out[ c ] = v[ c ];
				}
				else if( cubic )
				{
					const float* a = &ch.values[ k * perKey ];  //in, value, out
					const float* b = &ch.values[ k1 * perKey ];
					for( int c = 0; c < comps; ++c )
						out[ c ] = CubicHermite( a[ comps + c ], a[ comps * 2 + c ],
						                         b[ comps + c ], b[ c ], s, dt );
				}
				else if( ch.path == Path::Rotation )
				{
					const float* a = &ch.values[ k * perKey ];
					const float* b = &ch.values[ k1 * perKey ];
					const Quat q   = slerp( Quat( a[ 0 ], a[ 1 ], a[ 2 ], a[ 3 ] ),
					                        Quat( b[ 0 ], b[ 1 ], b[ 2 ], b[ 3 ] ), s );
					out[ 0 ] = q.x; out[ 1 ] = q.y; out[ 2 ] = q.z; out[ 3 ] = q.w;
				}
				else
				{
					const float* a = &ch.values[ k * perKey ];
					const float* b = &ch.values[ k1 * perKey ];
					for( int c = 0; c < comps; ++c )
						out[ c ] = mixf( a[ c ], b[ c ], s );
				}
			}

			Pose& p = pose[ ch.node ];
			if( ch.path == Path::Translation )
			{
				p.translation = Vec3( out[ 0 ], out[ 1 ], out[ 2 ] );
				p.hasT        = true;
			}
			else if( ch.path == Path::Rotation )
			{
				p.rotation = normalize( Quat( out[ 0 ], out[ 1 ], out[ 2 ], out[ 3 ] ) );
				p.hasR     = true;
			}
			else
			{
				p.scale = Vec3( out[ 0 ], out[ 1 ], out[ 2 ] );
				p.hasS  = true;
			}
		}

		for( size_t i = 0; i < nodes.size(); ++i )
		{
			const Pose& p = pose[ i ];
			if( !p.hasT && !p.hasR && !p.hasS )
				continue;
			Node& node = nodes[ i ];
			node.local = trs( p.hasT ? p.translation : node.translation,
			                  p.hasR ? p.rotation : node.rotation,
			                  p.hasS ? p.scale : node.scale );
			node.hasMatrix = false;
		}
	}

	UpdateGlobalTransforms();
	UpdateJointMatrices();
}
