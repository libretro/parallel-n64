#ifndef OPENGL_GRAPHICS_DRAWER_H
#define OPENGL_GRAPHICS_DRAWER_H
#include <Graphics/Context.h>

namespace opengl {

	class GraphicsDrawer {
	public:
		virtual ~GraphicsDrawer() {}

		virtual void drawTriangles(const gli_draw_triangles_params & _params) = 0;

		virtual void drawRects(const gli_draw_rects_params & _params) = 0;

		virtual void drawLine(f32 _width, SPVertex * _vertices) = 0;
	};
}


#endif // OPENGL_GRAPHICS_DRAWER_H