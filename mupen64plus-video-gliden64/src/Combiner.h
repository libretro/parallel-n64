#ifndef COMBINER_H
#define COMBINER_H

#include <map>
#include <memory>

#include "GLideN64.h"
#include "GraphicsDrawer.h"
#include "gDP.h"
#include "CombinerKey.h"
#include "Graphics/CombinerProgram.h"
#include "Graphics/ShaderProgram.h"

#include "CombinerMux.h"

#include "gli_combiner.h"

/* the program drawn with (gli_combiner.h) */
inline graphics::CombinerProgram * currentCombiner()
{
	return static_cast<graphics::CombinerProgram *>(gli_cmb.current);
}

inline graphics::ShaderProgram * combinerTexrectCopyProgram()
{
	return static_cast<graphics::ShaderProgram *>(graphics::combinerProgram(gli_cmb.texrectCopy));
}

inline void combinerSetPolygonMode(DrawingState _drawingState)
{
	gli_combiner_set_rect_mode(_drawingState == DrawingState::Rect || _drawingState == DrawingState::TexRect);
}

#endif

