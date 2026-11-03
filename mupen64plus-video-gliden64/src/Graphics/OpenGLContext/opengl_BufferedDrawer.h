#pragma once
#include <vector>
#include <unordered_map>
#include "opengl_GLInfo.h"
#include "opengl_GraphicsDrawer.h"
#include "opengl_CachedFunctions.h"
#include "opengl_RingSegments.h"

namespace opengl {

	class BufferedDrawer : public GraphicsDrawer
	{
	public:
		BufferedDrawer(const GLInfo & _glinfo, CachedVertexAttribArray * _cachedAttribArray,
			CachedBindBuffer * _bindBuffer);
		~BufferedDrawer();

		void drawTriangles(const graphics::Context::DrawTriangleParameters & _params) override;

		void drawRects(const graphics::Context::DrawRectParameters & _params) override;

		void drawLine(f32 _width, SPVertex * _vertices) override;

	private:
		void _updateRectBuffer(const graphics::Context::DrawRectParameters & _params);
		void _updateTrianglesBuffers(const graphics::Context::DrawTriangleParameters & _params);

		enum class BuffersType {
			none,
			rects,
			triangles
		};

		struct Buffer {
			Buffer(GLenum _type) : type(_type) {}

			GLenum type;
			GLuint handle = 0;
			GLintptr offset = 0;
			GLint pos = 0;
			GLuint size = 0;
			GLubyte * data = nullptr;
			RingSegments<3> ring;
			GLsync fences[3] = {};
		};

		struct RectBuffers {
			GLuint vao = 0;
			Buffer vbo = Buffer(GL_ARRAY_BUFFER);
		};

		struct TrisBuffers {
			GLuint vao = 0;
			Buffer vbo = Buffer(GL_ARRAY_BUFFER);
			Buffer ebo = Buffer(GL_ELEMENT_ARRAY_BUFFER);
		};

		struct Vertex
		{
			f32 x, y, z, w;
			f32 r, g, b, a;
			f32 s, t;
			u32 modify;
		};

		void _initBuffer(Buffer & _buffer, GLuint _bufSize, GLuint _stride);
		void _updateBuffer(Buffer & _buffer, u32 _count, u32 _dataSize, const void * _data);
		GLubyte * _reserve(Buffer & _buffer, u32 _dataSize);
		void _commit(Buffer & _buffer, u32 _count, u32 _dataSize);
		static void _convertFromSPVertex(Vertex * _dst, bool _flatColors, u32 _count, const SPVertex * _data);
		void _updateVertices(bool _flatColors, u32 _count, const SPVertex * _data);

		const GLInfo & m_glInfo;
		CachedVertexAttribArray * m_cachedAttribArray;
		CachedBindBuffer * m_bindBuffer;

		RectBuffers m_rectsBuffers;
		TrisBuffers m_trisBuffers;
		BuffersType m_type = BuffersType::none;

		std::vector<Vertex> m_vertices;

		typedef std::unordered_map<u64, u32> BufferOffsets;
		BufferOffsets m_rectBufferOffsets;

		static const u32 m_bufMaxSize;
		static const u32 m_bufMinSize;
		static u32 _ringSize();
		static const GLbitfield m_bufAccessBits;
		static const GLbitfield m_bufMapBits;
	};

}
