#pragma once
#include <Graphics/ObjectHandle.h>
#include <Graphics/Parameter.h>
#include <Graphics/Context.h>
#include "opengl_GLInfo.h"

namespace opengl {

	class CachedFunctions;

	class Create2DTexture
	{
	public:
		virtual ~Create2DTexture() {};
		virtual graphics::ObjectHandle createTexture(graphics::Parameter _target) = 0;
	};

	class Init2DTexture
	{
	public:
		virtual ~Init2DTexture() {};
		virtual void init2DTexture(const gli_init_texture_params & _params) = 0;
		virtual void reset(graphics::ObjectHandle _deleted) = 0;
	};

	class Update2DTexture
	{
	public:
		virtual ~Update2DTexture() {};
		virtual void update2DTexture(const gli_update_texture_params & _params) = 0;
	};

	class Set2DTextureParameters
	{
	public:
		virtual ~Set2DTextureParameters() {}
		virtual void setTextureParameters(const gli_tex_parameters & _parameters) = 0;
	};

	class TextureManipulationObjectFactory
	{
	public:
		TextureManipulationObjectFactory(const GLInfo & _glinfo, CachedFunctions & _cachedFunctions);
		~TextureManipulationObjectFactory();

		Create2DTexture * getCreate2DTexture() const;

		Init2DTexture * getInit2DTexture() const;

		Update2DTexture * getUpdate2DTexture() const;

		Set2DTextureParameters * getSet2DTextureParameters() const;

	private:
		const GLInfo & m_glInfo;
		CachedFunctions & m_cachedFunctions;
	};

}
