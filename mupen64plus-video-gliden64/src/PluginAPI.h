#ifndef COMMONPLUGINAPI_H
#define COMMONPLUGINAPI_H

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "m64p_plugin.h"

#ifdef RSPTHREAD
#include <thread>
#include <condition_variable>
#endif

class APICommand;

class PluginAPI
{
public:
#ifdef RSPTHREAD
	~PluginAPI()
	{
		delete m_pRspThread;
		m_pRspThread = NULL;
	}
#endif

	// Common
	void MoveScreen(int /*_xpos*/, int /*_ypos*/) {}
	void ViStatusChanged() {}
	void ViWidthChanged() {}

	void ProcessDList();
	void ProcessRDPList();
	void RomClosed();
	void RomOpen();
	void ShowCFB();
	void UpdateScreen();
	int InitiateGFX(const GFX_INFO & _gfxInfo);
	void ChangeWindow();

	void FindPluginPath(wchar_t * _strPath);
	void GetUserDataPath(wchar_t * _strPath);
	void GetUserCachePath(wchar_t * _strPath);
	bool isRomOpen() const { return m_bRomOpen; }

	// MupenPlus
	void ResizeVideoOutput(int _Width, int _Height);
	void ReadScreen2(void * _dest, int * _width, int * _height, int _front);

	m64p_error PluginStartup(m64p_dynlib_handle _CoreLibHandle);
	m64p_error PluginShutdown();
	m64p_error PluginGetVersion(
		m64p_plugin_type * _PluginType,
		int * _PluginVersion,
		int * _APIVersion,
		const char ** _PluginNamePtr,
		int * _Capabilities
	);
	void SetRenderingCallback(void (*callback)());

	// FrameBufferInfo extension
	void FBWrite(unsigned int addr, unsigned int size);
	void FBRead(unsigned int addr);
	void FBGetFrameBufferInfo(void *pinfo);

	static PluginAPI & get();

private:
	PluginAPI()
		: m_bRomOpen(false)
#ifdef RSPTHREAD
		, m_pRspThread(NULL)
		, m_pCommand(nullptr)
#endif
	{}
	PluginAPI(const PluginAPI &) = delete;

	void _initiateGFX(const GFX_INFO & _gfxInfo) const;

	bool m_bRomOpen;
#ifdef RSPTHREAD
	void _callAPICommand(APICommand & _command);
	std::mutex m_rspThreadMtx;
	std::mutex m_pluginThreadMtx;
	std::condition_variable_any m_rspThreadCv;
	std::condition_variable_any m_pluginThreadCv;
	std::thread * m_pRspThread;
	APICommand * m_pCommand;
#endif
};

inline PluginAPI & api()
{
	return PluginAPI::get();
}

#endif // COMMONPLUGINAPI_H
