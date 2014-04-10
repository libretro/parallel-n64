
#include "main.h"

#include <iostream>
#include <string>
#include <vector>


#define VIDEO_TAG(X) angrylion##X


#define ReadScreen2 VIDEO_TAG(ReadScreen2)
#define PluginStartup VIDEO_TAG(PluginStartup)
#define PluginShutdown VIDEO_TAG(PluginShutdown)
#define PluginGetVersion VIDEO_TAG(PluginGetVersion)
#define CaptureScreen VIDEO_TAG(CaptureScreen)
#define ChangeWindow VIDEO_TAG(ChangeWindow)
#define CloseDLL VIDEO_TAG(CloseDLL)
#define DllTest VIDEO_TAG(DllTest)
#define DrawScreen VIDEO_TAG(DrawScreen)
#define GetDllInfo VIDEO_TAG(GetDllInfo)
#define InitiateGFX VIDEO_TAG(InitiateGFX)
#define MoveScreen VIDEO_TAG(MoveScreen)
#define RomClosed VIDEO_TAG(RomClosed)
#define RomOpen VIDEO_TAG(RomOpen)
#define ShowCFB VIDEO_TAG(ShowCFB)
#define SetRenderingCallback VIDEO_TAG(SetRenderingCallback)
#define UpdateScreen VIDEO_TAG(UpdateScreen)
#define ViStatusChanged VIDEO_TAG(ViStatusChanged)
#define ViWidthChanged VIDEO_TAG(ViWidthChanged)
#define ReadScreen VIDEO_TAG(ReadScreen)
#define FBGetFrameBufferInfo VIDEO_TAG(FBGetFrameBufferInfo)
#define FBRead VIDEO_TAG(FBRead)
#define FBWrite VIDEO_TAG(FBWrite)
#define ProcessDList VIDEO_TAG(ProcessDList)
#define ProcessRDPList VIDEO_TAG(ProcessRDPList)
#define ResizeVideoOutput VIDEO_TAG(ResizeVideoOutput)

GFX_INFO gfx;

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static const char* vertex_shader[] = {
        "#version 330\n"
        "attribute vec4 vposition;",
        "attribute vec2 vtexcoord;",
        "out vec2 ftexcoord;",
        "void main() {",
        "   ftexcoord = vtexcoord;",
        "   gl_Position = vposition;",
        "}",
		};
        
 static const char* fragment_shader[] = {
        "#version 330\n"
        "uniform sampler2D tex;", // texture uniform
        "in vec2 ftexcoord;",
        "layout(location = 0) out vec4 FragColor;",
        "void main() {",
        "   FragColor = texture(tex, ftexcoord);",
        "}",
		};
		
 GLfloat vertexData[] = {
    //  X     Y     Z           U     V     
       1.0f, 1.0f, 0.0f,       1.0f, 1.0f, // vertex 0
      -1.0f, 1.0f, 0.0f,       0.0f, 1.0f, // vertex 1
       1.0f,-1.0f, 0.0f,       1.0f, 0.0f, // vertex 2
	   1.0f,-1.0f, 0.0f,       1.0f, 0.0f, // vertex 2
      -1.0f,-1.0f, 0.0f,       0.0f, 0.0f, // vertex 3
	   1.0f, 1.0f, 0.0f,       1.0f, 1.0f, // vertex 0
    }; // 6 vertices with 5 components (floats) each

		
static GLuint prog;
static GLuint vao, vbo;
static GLuint texture;

static void compile_program(void);
static void setup_vao(void);

static void setup_texture()
{
    int width = 256, height = 256;
    glGenTextures(1, &texture);

    // bind the texture
    glBindTexture(GL_TEXTURE_2D, texture);

    // create some image data
    std::vector<GLubyte> image(4*width*height);
    for(int j = 0;j<height;++j)
        for(int i = 0;i<width;++i)
        {
            size_t index = j*width + i;
            image[4*index + 0] = 0xFF*(j/10%2)*(i/10%2); // R
            image[4*index + 1] = 0xFF*(j/13%2)*(i/13%2); // G
            image[4*index + 2] = 0xFF*(j/17%2)*(i/17%2); // B
            image[4*index + 3] = 0xFF;                   // A
        }
    
    // set texture parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    // set texture content
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, &image[0]);
    glBindTexture(GL_TEXTURE_2D,0);


}

static void draw_texture()
{
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glUseProgram(prog);
	GLint texture_location = glGetUniformLocation(prog, "tex");

	glUniform1i(texture_location, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);


   glBindVertexArray(vao);
   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   int vposition = glGetAttribLocation(prog, "vposition");
   glEnableVertexAttribArray(vposition);
   glVertexAttribPointer(vposition, 3, GL_FLOAT, GL_FALSE, 5*sizeof(GLfloat), (char*)0 + 0*sizeof(GLfloat));
   int vtexcoord = glGetAttribLocation(prog, "vtexcoord");
   glEnableVertexAttribArray(vtexcoord);
   glVertexAttribPointer(vtexcoord, 2, GL_FLOAT, GL_FALSE, 5*sizeof(GLfloat), (char*)0 + 3*sizeof(GLfloat));
   glBindBuffer(GL_ARRAY_BUFFER, 0);
   glDrawArrays(GL_TRIANGLES, 0,6);  
   
   glDisableVertexAttribArray(vposition);
   glDisableVertexAttribArray(vtexcoord);
   glUseProgram(0);
   glBindVertexArray(0);
   glBindTexture(GL_TEXTURE_2D,0);



}
  
static void context_reset(void)
{
	compile_program();
	setup_vao();
	setup_texture();

}

static void setup_vao(void)
{
   glGenVertexArrays(1, &vao);

   glUseProgram(prog);

   glGenBuffers(1, &vbo);
   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(GLfloat)*6*5, vertexData, GL_STATIC_DRAW);
   glBindBuffer(GL_ARRAY_BUFFER, 0);
   glUseProgram(0);
}
		
static void compile_program(void)
{
   prog = glCreateProgram();
   GLuint vert = glCreateShader(GL_VERTEX_SHADER);
   GLuint frag = glCreateShader(GL_FRAGMENT_SHADER);

   glShaderSource(vert, ARRAY_SIZE(vertex_shader), vertex_shader, 0);
   glShaderSource(frag, ARRAY_SIZE(fragment_shader), fragment_shader, 0);
   glCompileShader(vert);
   glCompileShader(frag);

   glAttachShader(prog, vert);
   glAttachShader(prog, frag);
   glLinkProgram(prog);
   glDeleteShader(vert);
   glDeleteShader(frag);
}

static void (*l_DebugCallback)(void *, int, const char *) = NULL;
static void *l_DebugCallContext = NULL;



#ifdef __cplusplus
extern "C" {
#endif


EXPORT m64p_error CALL PluginStartup(m64p_dynlib_handle CoreLibHandle, void *Context,
                                   void (*DebugCallback)(void *, int, const char *))
{
   l_DebugCallback = DebugCallback;
   l_DebugCallContext = Context;
   return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL PluginShutdown(void)
{
   return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL PluginGetVersion(m64p_plugin_type *PluginType, int *PluginVersion, int *APIVersion, const char **PluginNamePtr, int *Capabilities)
{
   /* set version info */
   if (PluginType != NULL)
      *PluginType = M64PLUGIN_GFX;

   if (PluginVersion != NULL)
      *PluginVersion = 0x010000;

   if (APIVersion != NULL)
      *APIVersion = 0x020100;

   if (PluginNamePtr != NULL)
      *PluginNamePtr = "Angrylion's Video Plugin";

   if (Capabilities != NULL)
      *Capabilities = 0;

   return M64ERR_SUCCESS;
}



EXPORT void CALL ChangeWindow (void)
{

}

EXPORT int CALL InitiateGFX (GFX_INFO Gfx_Info)
{
	gfx = Gfx_Info;
	
    return 1;
}

EXPORT void CALL MoveScreen (int xpos, int ypos)
{
}

EXPORT void CALL ProcessDList(void)
{
}

 EXPORT void CALL ProcessRDPList(void)
{
	
	//rdp_process_list();
	*gfx.MI_INTR_REG |= DP_INTERRUPT;
	gfx.CheckInterrupts();
	
}

EXPORT void CALL RomClosed (void)
{
	//rdp_close();
}

EXPORT int CALL RomOpen (void)
{
	context_reset();
    //rdp_init();
    return 1;
}

EXPORT void CALL ShowCFB (void)
{
	draw_texture();
	//rdp_update();
}

EXPORT void CALL UpdateScreen (void)
{
    draw_texture();
	//rdp_update();

}

EXPORT void CALL ViStatusChanged (void)
{

}

EXPORT void CALL ViWidthChanged (void)
{

}

EXPORT void CALL ReadScreen2 (void *dest, int *width, int *height, int front)
{

}

EXPORT void CALL SetRenderingCallback(void (*callback)(int))
{

}

EXPORT void CALL FBRead(unsigned int addr)
{
}

EXPORT void CALL FBWrite(unsigned int addr, unsigned int size)
{
}

EXPORT void CALL FBGetFrameBufferInfo(void *p)
{
}

EXPORT void CALL ResizeVideoOutput(int width, int height)
{
}

#ifdef __cplusplus
}
#endif



