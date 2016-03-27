#include <stdint.h>
#include <assert.h>

#include "../PluginAPI.h"

#include "../N64.h"
#include "../OpenGL.h"
#include "../RSP.h"
#include "../RDP.h"
#include "../VI.h"
#include "../Config.h"
#include "../Debug.h"
#include "../FrameBufferInfo.h"

PluginAPI & PluginAPI::get()
{
	static PluginAPI api;
	return api;
}

#ifdef RSPTHREAD
void RSP_ThreadProc(std::mutex * _pRspThreadMtx, std::mutex * _pPluginThreadMtx, std::condition_variable_any * _pRspThreadCv, std::condition_variable_any * _pPluginThreadCv, API_COMMAND * _pCommand)
{
	_pRspThreadMtx->lock();
	RSP_Init();
	GBI.init();
	//Config_LoadConfig();
	video().start();
	assert(!isGLError());

	while (true) {
		_pPluginThreadMtx->lock();
		_pPluginThreadCv->notify_one();
		_pPluginThreadMtx->unlock();
		_pRspThreadCv->wait(*_pRspThreadMtx);
		switch (*_pCommand) {
		case acProcessDList:
			RSP_ProcessDList();
			break;
		case acProcessRDPList:
			RDP_ProcessRDPList();
			break;
		case acUpdateScreen:
			VI_UpdateScreen();
			break;
		case acRomClosed:
			TFH.shutdown();
			video().stop();
			GBI.destroy();
			*_pCommand = acNone;
			_pRspThreadMtx->unlock();
			_pPluginThreadMtx->lock();
			_pPluginThreadCv->notify_one();
			_pPluginThreadMtx->unlock();
			return;
		}
		assert(!isGLError());
		*_pCommand = acNone;
	}
}

void PluginAPI::_callAPICommand(API_COMMAND _command)
{
	m_command = _command;
	m_pluginThreadMtx.lock();
	m_rspThreadMtx.lock();
	m_rspThreadCv.notify_one();
	m_rspThreadMtx.unlock();
	m_pluginThreadCv.wait(m_pluginThreadMtx);
	m_pluginThreadMtx.unlock();
}
#endif

void PluginAPI::ProcessDList()
{
	//LOG(LOG_APIFUNC, "ProcessDList\n");
#ifdef RSPTHREAD
	_callAPICommand(acProcessDList);
#else
	RSP_ProcessDList();
#endif
}

void PluginAPI::ProcessRDPList()
{
	//LOG(LOG_APIFUNC, "ProcessRDPList\n");
#ifdef RSPTHREAD
	_callAPICommand(acProcessRDPList);
#else
	RDP_ProcessRDPList();
#endif
}

void PluginAPI::RomClosed()
{
	//LOG(LOG_APIFUNC, "RomClosed\n");
#ifdef RSPTHREAD
	_callAPICommand(acRomClosed);
	delete m_pRspThread;
	m_pRspThread = NULL;
#else
	TFH.shutdown();
	video().stop();
	GBI.destroy();
#endif

#ifdef DEBUG
	CloseDebugDlg();
#endif
}

void PluginAPI::RomOpen()
{
	//LOG(LOG_APIFUNC, "RomOpen\n");
#ifdef RSPTHREAD
	m_pluginThreadMtx.lock();
	m_pRspThread = new std::thread(RSP_ThreadProc, &m_rspThreadMtx, &m_pluginThreadMtx, &m_rspThreadCv, &m_pluginThreadCv, &m_command);
	m_pRspThread->detach();
	m_pluginThreadCv.wait(m_pluginThreadMtx);
	m_pluginThreadMtx.unlock();
#else
	RSP_Init();
	GBI.init();
	//Config_LoadConfig();
	video().start();
#endif

#ifdef DEBUG
	OpenDebugDlg();
#endif
}

void PluginAPI::ShowCFB()
{
	gDP.changed |= CHANGED_CPU_FB_WRITE;
}

void PluginAPI::UpdateScreen()
{
	//LOG(LOG_APIFUNC, "UpdateScreen\n");
#ifdef RSPTHREAD
	_callAPICommand(acUpdateScreen);
#else
	VI_UpdateScreen();
#endif
}

void PluginAPI::_initiateGFX(const GFX_INFO & _gfxInfo) const {
	HEADER = gfx_info.HEADER;
	DMEM   = gfx_info.DMEM;
	IMEM   = gfx_info.IMEM;
	RDRAM  = gfx_info.RDRAM;
}

void PluginAPI::ChangeWindow()
{
	video().setToggleFullscreen();
}

void PluginAPI::FBWrite(unsigned int addr, unsigned int size)
{
	FrameBufferWrite(addr, size);
}

void PluginAPI::FBWList(FrameBufferModifyEntry *plist, unsigned int size)
{
	FrameBufferWriteList(plist, size);
}

void PluginAPI::FBRead(unsigned int addr)
{
	FrameBufferRead(addr);
}

void PluginAPI::FBGetFrameBufferInfo(void *pinfo)
{
	FrameBufferGetInfo(pinfo);
}
