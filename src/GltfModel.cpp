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
#include <chrono>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <sys/types.h>

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

namespace
{
typedef GltfModel::Node Node;
typedef GltfModel::Asset Asset;

/// Walks the hierarchy from the roots: nodes are stored in glTF order and
/// parents are not guaranteed to come first.
void ComputeGlobals( std::vector< Node >& nodes, const std::vector< int >& roots )
{
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

void BuildWeldedStreams( GltfModel::Mesh& mesh )
{
	// glTF exports usually split vertices on normal / uv seams. Feeding that
	// straight into the simplifier locks almost every edge and nothing gets
	// removed, so we first weld on position only and simplify that topology.
	const size_t vertexCount = mesh.vertices.size();
	const size_t indexCount  = mesh.indices.size();

	std::vector< unsigned int > remap( vertexCount );
	meshopt_Stream stream;
	stream.data   = &mesh.vertices[ 0 ].px;
	stream.size   = sizeof( float ) * 3;
	stream.stride = sizeof( GltfModel::Vertex );

	const size_t weldedCount = meshopt_generateVertexRemapMulti(
		remap.data(), mesh.indices.data(), indexCount, vertexCount, &stream, 1 );

	mesh.weldedPos.assign( weldedCount * 3, 0.0f );
	mesh.representative.assign( weldedCount, 0 );
	std::vector< unsigned char > seen( weldedCount, 0 );

	for( size_t v = 0; v < vertexCount; ++v )
	{
		const unsigned int w = remap[ v ];
		if( w >= weldedCount )
			continue;
		if( !seen[ w ] )
		{
			seen[ w ]                   = 1;
			mesh.representative[ w ]    = (unsigned int)v;
			mesh.weldedPos[ w * 3 ]     = mesh.vertices[ v ].px;
			mesh.weldedPos[ w * 3 + 1 ] = mesh.vertices[ v ].py;
			mesh.weldedPos[ w * 3 + 2 ] = mesh.vertices[ v ].pz;
		}
	}

	mesh.weldedIdx.resize( indexCount );
	for( size_t i = 0; i < indexCount; ++i )
		mesh.weldedIdx[ i ] = remap[ mesh.indices[ i ] ];
}

void ComputeBounds( Asset& asset )
{
	Vec3 lo( 1e30f, 1e30f, 1e30f );
	Vec3 hi( -1e30f, -1e30f, -1e30f );
	bool any = false;

	for( const GltfModel::Mesh& mesh : asset.meshes )
	{
		const Mat4& world = ( mesh.node >= 0 && mesh.node < (int)asset.nodes.size() ) ? asset.nodes[ mesh.node ].global : Mat4();
		for( const GltfModel::Vertex& v : mesh.vertices )
		{
			Vec3 p = world.transformPoint( Vec3( v.px, v.py, v.pz ) );
			lo.x = std::min( lo.x, p.x ); lo.y = std::min( lo.y, p.y ); lo.z = std::min( lo.z, p.z );
			hi.x = std::max( hi.x, p.x ); hi.y = std::max( hi.y, p.y ); hi.z = std::max( hi.z, p.z );
			any = true;
		}
	}
	if( !any )
	{
		asset.boundsCenter = Vec3();
		asset.boundsRadius = 1.0f;
		return;
	}
	asset.boundsCenter = ( lo + hi ) * 0.5f;
	asset.boundsRadius = std::max( 1e-4f, length( ( hi - lo ) * 0.5f ) );
}

/// Decodes one glTF image to RGBA8. Returns false when the bytes are missing
/// or not a format stb_image understands; the material then falls back to white.
bool DecodeImage( const cgltf_image& img, const std::string& baseDir, GltfModel::Image& out )
{
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

	if( bytes == nullptr || byteCount == 0 )
		return false;

	int w = 0, h = 0, comp = 0;
	unsigned char* pixels = stbi_load_from_memory( bytes, (int)byteCount, &w, &h, &comp, 4 );
	if( pixels == nullptr )
		return false;
	out.levels.resize( 1 );
	out.levels[ 0 ].width  = w;
	out.levels[ 0 ].height = h;
	out.levels[ 0 ].pixels.assign( pixels, pixels + (size_t)w * h * 4 );
	stbi_image_free( pixels );
	return true;
}

/// 2x2 box-filtered mip chain down to 1x1. sRGB textures are averaged in
/// linear light, like the driver would, so dark seams do not creep in.
void BuildMipChain( GltfModel::Image& img, bool srgb )
{
	static float toLinear[ 256 ];
	static unsigned char toSrgb[ 4096 ];
	static std::once_flag tables;
	std::call_once( tables, []() {
		for( int i = 0; i < 256; ++i )
		{
			const float c = i / 255.0f;
			toLinear[ i ] = c <= 0.04045f ? c / 12.92f : std::pow( ( c + 0.055f ) / 1.055f, 2.4f );
		}
		for( int i = 0; i < 4096; ++i )
		{
			const float l = i / 4095.0f;
			const float c = l <= 0.0031308f ? l * 12.92f : 1.055f * std::pow( l, 1.0f / 2.4f ) - 0.055f;
			toSrgb[ i ]   = (unsigned char)clampf( c * 255.0f + 0.5f, 0.0f, 255.0f );
		}
	} );

	if( img.levels.empty() )
		return;
	while( img.levels.back().width > 1 || img.levels.back().height > 1 )
	{
		const GltfModel::Level& src = img.levels.back();
		GltfModel::Level dst;
		dst.width  = std::max( src.width / 2, 1 );
		dst.height = std::max( src.height / 2, 1 );
		dst.pixels.resize( (size_t)dst.width * dst.height * 4 );

		for( int y = 0; y < dst.height; ++y )
		{
			const int y0 = std::min( y * 2, src.height - 1 );
			const int y1 = std::min( y * 2 + 1, src.height - 1 );
			for( int x = 0; x < dst.width; ++x )
			{
				const int x0 = std::min( x * 2, src.width - 1 );
				const int x1 = std::min( x * 2 + 1, src.width - 1 );
				const unsigned char* p[ 4 ] = {
					&src.pixels[ ( (size_t)y0 * src.width + x0 ) * 4 ], &src.pixels[ ( (size_t)y0 * src.width + x1 ) * 4 ],
					&src.pixels[ ( (size_t)y1 * src.width + x0 ) * 4 ], &src.pixels[ ( (size_t)y1 * src.width + x1 ) * 4 ]
				};
				unsigned char* out = &dst.pixels[ ( (size_t)y * dst.width + x ) * 4 ];
				for( int c = 0; c < 3; ++c )
				{
					if( srgb )
					{
						const float l = ( toLinear[ p[ 0 ][ c ] ] + toLinear[ p[ 1 ][ c ] ] + toLinear[ p[ 2 ][ c ] ] + toLinear[ p[ 3 ][ c ] ] ) * 0.25f;
						out[ c ]      = toSrgb[ (int)( l * 4095.0f + 0.5f ) ];
					}
					else
					{
						out[ c ] = (unsigned char)( ( p[ 0 ][ c ] + p[ 1 ][ c ] + p[ 2 ][ c ] + p[ 3 ][ c ] + 2 ) / 4 );
					}
				}
				out[ 3 ] = (unsigned char)( ( p[ 0 ][ 3 ] + p[ 1 ][ 3 ] + p[ 2 ][ 3 ] + p[ 3 ][ 3 ] + 2 ) / 4 );
			}
		}
		img.levels.push_back( std::move( dst ) );
	}
}

/// The actual file parse. Runs on a worker thread, touches no GL.
std::shared_ptr< Asset > ParseFile( const std::string& path, std::string& error )
{
	cgltf_options options;
	std::memset( &options, 0, sizeof( options ) );

	cgltf_data* data = nullptr;
	cgltf_result res = cgltf_parse_file( &options, path.c_str(), &data );
	if( res != cgltf_result_success )
	{
		error = "Could not parse " + path;
		return nullptr;
	}
	res = cgltf_load_buffers( &options, data, path.c_str() );
	if( res != cgltf_result_success )
	{
		cgltf_free( data );
		error = "Could not load buffers (missing .bin next to the .gltf?)";
		return nullptr;
	}
	if( cgltf_validate( data ) != cgltf_result_success )
	{
		cgltf_free( data );
		error = "glTF failed validation";
		return nullptr;
	}

	std::shared_ptr< Asset > asset = std::make_shared< Asset >();
	asset->path                    = path;
	const std::string baseDir      = DirectoryOf( path );

	//------------------------------------------------------------------ images
	asset->images.resize( data->images_count );
	for( size_t i = 0; i < data->images_count; ++i )
		DecodeImage( data->images[ i ], baseDir, asset->images[ i ] );

	//--------------------------------------------------------------- materials
	asset->materials.resize( data->materials_count );
	for( size_t i = 0; i < data->materials_count; ++i )
	{
		const cgltf_material& src     = data->materials[ i ];
		GltfModel::Material& dst      = asset->materials[ i ];
		std::vector< GltfModel::Image >& images = asset->images;

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
				if( dst.texBaseColor >= 0 && dst.texBaseColor < (int)images.size() )
					images[ dst.texBaseColor ].srgb = true;
			}
			if( pbr.metallic_roughness_texture.texture && pbr.metallic_roughness_texture.texture->image )
				dst.texMetalRough = (int)( pbr.metallic_roughness_texture.texture->image - data->images );
		}
		if( src.normal_texture.texture && src.normal_texture.texture->image )
			dst.texNormal = (int)( src.normal_texture.texture->image - data->images );
		if( src.emissive_texture.texture && src.emissive_texture.texture->image )
		{
			dst.texEmissive = (int)( src.emissive_texture.texture->image - data->images );
			if( dst.texEmissive >= 0 && dst.texEmissive < (int)images.size() )
				images[ dst.texEmissive ].srgb = true;
		}
		for( int c = 0; c < 3; ++c )
			dst.emissive[ c ] = src.emissive_factor[ c ];
		dst.alphaCutoff  = src.alpha_cutoff;
		dst.doubleSided  = src.double_sided != 0;
		dst.alphaMode    = src.alpha_mode == cgltf_alpha_mode_mask ? 1 : ( src.alpha_mode == cgltf_alpha_mode_blend ? 2 : 0 );
	}

	//------------------------------------------------------------------- nodes
	asset->nodes.resize( data->nodes_count );
	for( size_t i = 0; i < data->nodes_count; ++i )
	{
		const cgltf_node& src = data->nodes[ i ];
		Node& dst             = asset->nodes[ i ];
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
			asset->roots.push_back( (int)i );
	}

	//------------------------------------------------------------------- skins
	asset->skins.resize( data->skins_count );
	for( size_t i = 0; i < data->skins_count; ++i )
	{
		const cgltf_skin& src  = data->skins[ i ];
		GltfModel::Skin& dst   = asset->skins[ i ];
		dst.skeletonRoot       = IndexOfNode( data, src.skeleton );
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

			GltfModel::Mesh mesh;
			mesh.material = src.material ? (int)( src.material - data->materials ) : -1;
			mesh.node     = (int)n;
			mesh.skin     = skinIndex;
			mesh.vertices.resize( aPos->count );

			for( size_t v = 0; v < aPos->count; ++v )
			{
				GltfModel::Vertex& vert = mesh.vertices[ v ];
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
				mesh.indices.resize( src.indices->count );
				for( size_t i = 0; i < src.indices->count; ++i )
					mesh.indices[ i ] = (unsigned int)cgltf_accessor_read_index( src.indices, i );
			}
			else
			{
				mesh.indices.resize( aPos->count );
				for( size_t i = 0; i < aPos->count; ++i )
					mesh.indices[ i ] = (unsigned int)i;
			}
			if( mesh.indices.size() < 3 )
				continue;
			mesh.indices.resize( ( mesh.indices.size() / 3 ) * 3 );

			// Missing normals: accumulate face normals so flat and lit modes still work.
			if( aNrm == nullptr )
			{
				for( GltfModel::Vertex& vert : mesh.vertices )
					vert.nx = vert.ny = vert.nz = 0.0f;
				for( size_t i = 0; i + 2 < mesh.indices.size(); i += 3 )
				{
					GltfModel::Vertex& a = mesh.vertices[ mesh.indices[ i ] ];
					GltfModel::Vertex& b = mesh.vertices[ mesh.indices[ i + 1 ] ];
					GltfModel::Vertex& c = mesh.vertices[ mesh.indices[ i + 2 ] ];
					Vec3 fn = cross( Vec3( b.px - a.px, b.py - a.py, b.pz - a.pz ),
					                 Vec3( c.px - a.px, c.py - a.py, c.pz - a.pz ) );
					a.nx += fn.x; a.ny += fn.y; a.nz += fn.z;
					b.nx += fn.x; b.ny += fn.y; b.nz += fn.z;
					c.nx += fn.x; c.ny += fn.y; c.nz += fn.z;
				}
				for( GltfModel::Vertex& vert : mesh.vertices )
				{
					Vec3 nn = normalize( Vec3( vert.nx, vert.ny, vert.nz ) );
					vert.nx = nn.x;
					vert.ny = nn.y;
					vert.nz = nn.z;
				}
			}

			asset->trianglesFull += mesh.indices.size() / 3;
			asset->gpuBytes += mesh.vertices.size() * sizeof( GltfModel::Vertex ) + mesh.indices.size() * sizeof( unsigned int );
			BuildWeldedStreams( mesh );
			asset->meshes.push_back( std::move( mesh ) );
		}
	}

	//-------------------------------------------------------------- animations
	asset->animations.resize( data->animations_count );
	for( size_t i = 0; i < data->animations_count; ++i )
	{
		const cgltf_animation& src = data->animations[ i ];
		GltfModel::Animation& dst  = asset->animations[ i ];
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

			GltfModel::Channel out;
			out.node = IndexOfNode( data, ch.target_node );
			out.path = ch.target_path == cgltf_animation_path_type_translation ? GltfModel::Path::Translation
			         : ( ch.target_path == cgltf_animation_path_type_rotation ? GltfModel::Path::Rotation : GltfModel::Path::Scale );
			out.interp = ch.sampler->interpolation == cgltf_interpolation_type_step ? GltfModel::Interp::Step
			           : ( ch.sampler->interpolation == cgltf_interpolation_type_cubic_spline ? GltfModel::Interp::CubicSpline : GltfModel::Interp::Linear );

			const cgltf_accessor* in  = ch.sampler->input;
			const cgltf_accessor* val = ch.sampler->output;
			if( in == nullptr || val == nullptr || in->count == 0 )
				continue;

			out.times.resize( in->count );
			for( size_t k = 0; k < in->count; ++k )
				cgltf_accessor_read_float( in, k, &out.times[ k ], 1 );

			const int comps = out.path == GltfModel::Path::Rotation ? 4 : 3;
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

	if( asset->meshes.empty() )
	{
		error = "No triangle geometry found in this file";
		return nullptr;
	}

	// After the materials, which decide which images are sRGB.
	for( GltfModel::Image& img : asset->images )
	{
		BuildMipChain( img, img.srgb );
		for( const GltfModel::Level& level : img.levels )
			asset->gpuBytes += level.pixels.size();
	}

	ComputeGlobals( asset->nodes, asset->roots );
	ComputeBounds( *asset );
	return asset;
}

//------------------------------------------------------------------- cache

/// Path + size + modification time, so re-exporting a file over the old one
/// is picked up while retriggering an unchanged clip is free.
std::string CacheKey( const std::string& path )
{
#if defined( _WIN32 )
	struct _stat64 st;
	if( _stat64( path.c_str(), &st ) != 0 )
		return std::string();
#else
	struct stat st;
	if( stat( path.c_str(), &st ) != 0 )
		return std::string();
#endif
	char stamp[ 64 ];
	std::snprintf( stamp, sizeof( stamp ), "|%lld|%lld", (long long)st.st_size, (long long)st.st_mtime );
	return path + stamp;
}

// Resolume creates a fresh plugin instance every time a clip is triggered and
// destroys it when the clip stops, so the cache keeps the last few assets
// alive on its own, not only while some instance holds them. Decoded textures
// are big, hence a RAM ceiling on top of the count.
const size_t kKeepRecent      = 6;
const size_t kKeepRecentBytes = (size_t)1536 << 20;

std::mutex& CacheMutex()
{
	static std::mutex m;
	return m;
}
std::map< std::string, std::weak_ptr< const Asset > >& CacheEntries()
{
	static std::map< std::string, std::weak_ptr< const Asset > > entries;
	return entries;
}
std::vector< std::shared_ptr< const Asset > >& CacheRecent()
{
	static std::vector< std::shared_ptr< const Asset > > recent;
	return recent;
}
std::map< std::string, std::shared_ptr< std::mutex > >& CacheBuilding()
{
	static std::map< std::string, std::shared_ptr< std::mutex > > building;
	return building;
}

/// Caller holds CacheMutex.
void TouchRecent( const std::shared_ptr< const Asset >& asset )
{
	std::vector< std::shared_ptr< const Asset > >& recent = CacheRecent();
	recent.erase( std::remove( recent.begin(), recent.end(), asset ), recent.end() );
	recent.push_back( asset );

	size_t bytes = 0;
	for( const std::shared_ptr< const Asset >& a : recent )
		bytes += a->gpuBytes;
	// The newest one always stays, however big.
	while( recent.size() > 1 && ( recent.size() > kKeepRecent || bytes > kKeepRecentBytes ) )
	{
		bytes -= recent.front()->gpuBytes;
		recent.erase( recent.begin() );
	}
}

/// Caller holds CacheMutex.
std::shared_ptr< const Asset > FindCached( const std::string& key )
{
	std::map< std::string, std::weak_ptr< const Asset > >& entries = CacheEntries();
	std::map< std::string, std::weak_ptr< const Asset > >::iterator it = entries.find( key );
	if( it == entries.end() )
		return nullptr;
	std::shared_ptr< const Asset > asset = it->second.lock();
	if( asset )
		TouchRecent( asset );
	else
		entries.erase( it );
	return asset;
}
}//namespace

std::shared_ptr< const GltfModel::Asset > GltfModel::Acquire( const std::string& path, bool forceReload, std::string& error )
{
	error.clear();
	if( path.empty() )
	{
		error = "No file selected";
		return nullptr;
	}
	const std::string key = CacheKey( path );
	if( key.empty() )
	{
		error = "File not found: " + path;
		return nullptr;
	}

	std::shared_ptr< std::mutex > building;
	{
		std::lock_guard< std::mutex > lock( CacheMutex() );
		if( !forceReload )
		{
			std::shared_ptr< const Asset > hit = FindCached( key );
			if( hit )
				return hit;
		}
		std::shared_ptr< std::mutex >& slot = CacheBuilding()[ key ];
		if( !slot )
			slot = std::make_shared< std::mutex >();
		building = slot;
	}

	// Several clips pointing at the same file: the first one parses, the
	// others wait here and then pick its result up from the cache.
	std::lock_guard< std::mutex > buildLock( *building );
	if( !forceReload )
	{
		std::lock_guard< std::mutex > lock( CacheMutex() );
		std::shared_ptr< const Asset > hit = FindCached( key );
		if( hit )
			return hit;
	}

	std::shared_ptr< const Asset > asset = ParseFile( path, error );

	std::lock_guard< std::mutex > lock( CacheMutex() );
	CacheBuilding().erase( key );
	if( asset )
	{
		CacheEntries()[ key ] = asset;
		TouchRecent( asset );
	}
	return asset;
}

std::shared_ptr< const GltfModel::IndexSet > GltfModel::Asset::Decimate( float density ) const
{
	const int key = (int)( clampf( density, 0.001f, 1.0f ) * 1000.0f + 0.5f );
	{
		std::lock_guard< std::mutex > lock( decimateMutex );
		for( const auto& entry : decimated )
			if( entry.first == key )
				return entry.second;
	}

	const float target = (float)key / 1000.0f;
	std::shared_ptr< IndexSet > set = std::make_shared< IndexSet >( meshes.size() );
	std::vector< unsigned int > simplified;

	for( size_t m = 0; m < meshes.size(); ++m )
	{
		const Mesh& mesh = meshes[ m ];
		std::vector< unsigned int >& out = ( *set )[ m ];
		if( target >= 0.999f || mesh.weldedIdx.size() < 6 )
		{
			out = mesh.indices;
			continue;
		}

		size_t targetIndexCount = (size_t)( ( mesh.weldedIdx.size() / 3 ) * target ) * 3;
		if( targetIndexCount < 3 )
			targetIndexCount = 3;

		simplified.resize( mesh.weldedIdx.size() );
		float resultError = 0.0f;
		const size_t count = meshopt_simplify(
			simplified.data(),
			mesh.weldedIdx.data(), mesh.weldedIdx.size(),
			mesh.weldedPos.data(), mesh.weldedPos.size() / 3, sizeof( float ) * 3,
			targetIndexCount, 1.0f /*target error: let it go as far as it needs*/,
			meshopt_SimplifyLockBorder, &resultError );

		// Back to the original vertex ids so normals, uvs and skin weights survive.
		out.resize( count );
		for( size_t i = 0; i < count; ++i )
			out[ i ] = mesh.representative[ simplified[ i ] ];

		if( !out.empty() )
			meshopt_optimizeVertexCache( out.data(), out.data(), out.size(), mesh.vertices.size() );
	}

	std::lock_guard< std::mutex > lock( decimateMutex );
	decimated.emplace_back( key, set );
	if( decimated.size() > 4 )
		decimated.erase( decimated.begin() );
	return set;
}

//------------------------------------------------------------------ instance

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
		p.activeIndexCount = 0;
	}
	for( GLuint& t : textures )
	{
		if( t )
			glDeleteTextures( 1, &t );
		t = 0;
	}
	uploaded     = false;
	uploadItem   = 0;
	uploadOffset = 0;
	uploadStage  = 0;
}

void GltfModel::Clear()
{
	Release();
	asset.reset();
	pendingIndices.reset();
	primitives.clear();
	textures.clear();
	nodes.clear();
	jointMatrices.clear();
}

void GltfModel::Adopt( std::shared_ptr< const Asset > newAsset, std::shared_ptr< const IndexSet > indices )
{
	Clear();
	if( !newAsset )
		return;
	asset          = std::move( newAsset );
	pendingIndices = std::move( indices );

	primitives.resize( asset->meshes.size() );
	for( size_t i = 0; i < primitives.size(); ++i )
	{
		primitives[ i ].material = asset->meshes[ i ].material;
		primitives[ i ].node     = asset->meshes[ i ].node;
		primitives[ i ].skin     = asset->meshes[ i ].skin;
	}
	textures.assign( asset->images.size(), 0 );
	nodes = asset->nodes;

	jointMatrices.resize( asset->skins.size() );
	UpdateJointMatrices();
}

bool GltfModel::UploadStep( double budgetMs )
{
	if( !asset )
		return false;
	if( uploaded )
		return true;

	const size_t meshCount  = primitives.size();
	const size_t totalItems = meshCount + textures.size();
	// Chunks small enough that overshooting the budget by one stays harmless.
	const size_t room = (size_t)4 << 20;
	const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
	auto elapsedMs = [ & ]() {
		return std::chrono::duration< double, std::milli >( std::chrono::steady_clock::now() - start ).count();
	};

	while( uploadItem < totalItems && elapsedMs() < budgetMs )
	{
		if( uploadItem < meshCount )
		{
			Primitive& prim  = primitives[ uploadItem ];
			const Mesh& mesh = asset->meshes[ uploadItem ];
			const std::vector< unsigned int >& idx =
				pendingIndices && uploadItem < pendingIndices->size() ? ( *pendingIndices )[ uploadItem ] : mesh.indices;

			if( prim.vao == 0 )
			{
				glGenVertexArrays( 1, &prim.vao );
				glGenBuffers( 1, &prim.vbo );
				glGenBuffers( 1, &prim.ibo );
			}

			// Buffers are allocated once, then filled chunk by chunk.
			const bool vertices      = uploadStage == 0;
			const GLenum targetType  = vertices ? GL_ARRAY_BUFFER : GL_ELEMENT_ARRAY_BUFFER;
			const unsigned char* src = vertices ? (const unsigned char*)mesh.vertices.data() : (const unsigned char*)idx.data();
			const size_t total       = vertices ? mesh.vertices.size() * sizeof( Vertex ) : idx.size() * sizeof( unsigned int );

			// The element buffer binding is VAO state, so it goes through the VAO.
			glBindVertexArray( prim.vao );
			glBindBuffer( targetType, vertices ? prim.vbo : prim.ibo );
			if( uploadOffset == 0 )
				glBufferData( targetType, (GLsizeiptr)total, nullptr, vertices ? GL_STATIC_DRAW : GL_DYNAMIC_DRAW );
			const size_t chunk = std::min( total - uploadOffset, room );
			if( chunk > 0 )
				glBufferSubData( targetType, (GLintptr)uploadOffset, (GLsizeiptr)chunk, src + uploadOffset );
			uploadOffset += chunk;

			if( uploadOffset >= total )
			{
				if( vertices )
				{
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
					uploadStage = 1;
				}
				else
				{
					prim.activeIndexCount = (GLsizei)idx.size();
					uploadStage           = 0;
					++uploadItem;
				}
				uploadOffset = 0;
			}

			glBindVertexArray( 0 );
			glBindBuffer( GL_ARRAY_BUFFER, 0 );
			continue;
		}

		const size_t t   = uploadItem - meshCount;
		const Image& img = asset->images[ t ];
		if( img.levels.empty() )
		{
			++uploadItem;
			continue;
		}

		if( textures[ t ] == 0 )
		{
			glGenTextures( 1, &textures[ t ] );
			glBindTexture( GL_TEXTURE_2D, textures[ t ] );
			const GLint format = img.srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;
#if !defined( __APPLE__ )
			// Immutable storage allocates the whole chain in one go, which is
			// noticeably cheaper than a glTexImage2D per level on some drivers.
			if( glTexStorage2D != nullptr )
				glTexStorage2D( GL_TEXTURE_2D, (GLsizei)img.levels.size(), format,
				                img.levels[ 0 ].width, img.levels[ 0 ].height );
			else
#endif
			for( size_t l = 0; l < img.levels.size(); ++l )
				glTexImage2D( GL_TEXTURE_2D, (GLint)l, format, img.levels[ l ].width, img.levels[ l ].height,
				              0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
			glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0 );
			glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, (GLint)img.levels.size() - 1 );
			glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR );
			glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
			glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT );
			glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT );
			uploadStage = 0;
		}
		else
		{
			glBindTexture( GL_TEXTURE_2D, textures[ t ] );
		}

		// Row bands, level after level, so a 4K texture does not land in a
		// single frame. Small levels are batched together until the budget runs out.
		const Level& level    = img.levels[ uploadStage ];
		const size_t rowBytes = (size_t)level.width * 4;
		size_t rows           = std::max( room / rowBytes, (size_t)1 );
		rows                  = std::min( rows, (size_t)level.height - uploadOffset );
		glTexSubImage2D( GL_TEXTURE_2D, uploadStage, 0, (GLint)uploadOffset, level.width, (GLsizei)rows,
		                 GL_RGBA, GL_UNSIGNED_BYTE, level.pixels.data() + uploadOffset * rowBytes );
		uploadOffset += rows;

		if( uploadOffset >= (size_t)level.height )
		{
			uploadOffset = 0;
			if( ++uploadStage >= (int)img.levels.size() )
			{
				uploadStage = 0;
				++uploadItem;
			}
		}
		glBindTexture( GL_TEXTURE_2D, 0 );
	}

	if( uploadItem >= totalItems )
	{
		uploaded = true;
		pendingIndices.reset();
	}
	return uploaded;
}

void GltfModel::ApplyIndices( const IndexSet* set )
{
	if( !asset || !uploaded )
		return;

	for( size_t i = 0; i < primitives.size(); ++i )
	{
		Primitive& prim = primitives[ i ];
		const std::vector< unsigned int >& idx =
			set && i < set->size() ? ( *set )[ i ] : asset->meshes[ i ].indices;

		glBindVertexArray( prim.vao );
		glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, prim.ibo );
		glBufferData( GL_ELEMENT_ARRAY_BUFFER,
		              (GLsizeiptr)( idx.size() * sizeof( unsigned int ) ),
		              idx.empty() ? nullptr : idx.data(), GL_DYNAMIC_DRAW );
		prim.activeIndexCount = (GLsizei)idx.size();
	}
	glBindVertexArray( 0 );
}

const std::vector< GltfModel::Material >& GltfModel::Materials() const
{
	static const std::vector< Material > none;
	return asset ? asset->materials : none;
}

const std::vector< GltfModel::Animation >& GltfModel::Animations() const
{
	static const std::vector< Animation > none;
	return asset ? asset->animations : none;
}

GLuint GltfModel::TextureId( int index ) const
{
	if( index < 0 || index >= (int)textures.size() )
		return 0;
	return textures[ index ];
}

size_t GltfModel::TriangleCountActive() const
{
	size_t n = 0;
	for( const Primitive& p : primitives )
		n += (size_t)p.activeIndexCount / 3;
	return n;
}

void GltfModel::UpdateGlobalTransforms()
{
	if( asset )
		ComputeGlobals( nodes, asset->roots );
}

void GltfModel::UpdateJointMatrices()
{
	if( !asset )
		return;
	for( size_t s = 0; s < asset->skins.size(); ++s )
	{
		const Skin& skin = asset->skins[ s ];
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
	if( !asset )
		return;

	// Rest pose first: channels only touch the nodes they animate.
	for( Node& node : nodes )
	{
		if( !node.hasMatrix )
			node.local = trs( node.translation, node.rotation, node.scale );
	}

	const std::vector< Animation >& animations = asset->animations;
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
