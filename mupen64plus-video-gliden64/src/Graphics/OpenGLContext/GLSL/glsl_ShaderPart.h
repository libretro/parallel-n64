#pragma once
#include <string>
#include <sstream>

namespace glsl {

	class ShaderPart
	{
	public:
		virtual ~ShaderPart() {}
		virtual void write(std::stringstream & shader) const
		{
			shader << m_part;
		}
		/* its text, for a part written as it is */
		const char * text() const { return m_part.c_str(); }

	protected:
		std::string m_part;
	};

}
