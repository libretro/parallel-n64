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
class APICommand {
   public:
      virtual bool run() = 0;
};

void RSP_ThreadProc(std::mutex * _pRspThreadMtx, std::mutex * _pPluginThreadMtx, std::condition_variable_any * _pRspThreadCv, std::condition_variable_any * _pPluginThreadCv, APICommand ** _pCommand)
{
	_pRspThreadMtx->lock();
	RSP_Init();
	GBI.init();
	Config_LoadConfig();
	video().start();
	assert(!isGLError());

	while (true) {
		_pPluginThreadMtx->lock();
		_pPluginThreadCv->notify_one();
		_pPluginThreadMtx->unlock();
		_pRspThreadCv->wait(*_pRspThreadMtx);
      if (*_pCommand != NULL && !(*_pCommand)->run())
         return;
		assert(!isGLError());
		*_pCommand = acNone;
	}
}

void PluginAPI::_callAPICommand(APICommand &_command)
{
	m_pCommand = &_command;
	m_pluginThreadMtx.lock();
	m_rspThreadMtx.lock();
	m_rspThreadCv.notify_one();
	m_rspThreadMtx.unlock();
	m_pluginThreadCv.wait(m_pluginThreadMtx);
	m_pluginThreadMtx.unlock();
   m_pCommand = NULL;
}

class ProcessDListCommand : public APICommand {
public:
	bool run() {
		RSP_ProcessDList();
		return true;
	}
};

class ProcessRDPListCommand : public APICommand {
public:
	bool run() {
		RDP_ProcessRDPList();
		return true;
	}
};

class ProcessUpdateScreenCommand : public APICommand {
public:
	bool run() {
		VI_UpdateScreen();
		return true;
	}
};

class FBReadCommand : public APICommand {
public:
	FBReadCommand(uint32_t _addr) : m_addr(_addr) {
	}

	bool run() {
		FrameBufferRead(m_addr);
		return true;
	}
private:
	uint32_t m_addr;
};

class RomClosedCommand : public APICommand {
public:
	RomClosedCommand(std::mutex * _pRspThreadMtx,
		std::mutex * _pPluginThreadMtx,
		std::condition_variable_any * _pRspThreadCv,
		std::condition_variable_any * _pPluginThreadCv)
		: m_pRspThreadMtx(_pRspThreadMtx)
		, m_pPluginThreadMtx(_pPluginThreadMtx)
		, m_pRspThreadCv(_pRspThreadCv)
		, m_pPluginThreadCv(_pPluginThreadCv) {
	}

	bool run() {
		TFH.shutdown();
		video().stop();
		GBI.destroy();
		m_pRspThreadMtx->unlock();
		m_pPluginThreadMtx->lock();
		m_pPluginThreadCv->notify_one();
		m_pPluginThreadMtx->unlock();
		return false;
	}

private:
	std::mutex * m_pRspThreadMtx;
	std::mutex * m_pPluginThreadMtx;
	std::condition_variable_any * m_pRspThreadCv;
	std::condition_variable_any * m_pPluginThreadCv;
};
#endif

void PluginAPI::ProcessDList()
{
	//LOG(LOG_APIFUNC, "ProcessDList\n");
#ifdef RSPTHREAD
	_callAPICommand(ProcessDListCOommand());
#else
	RSP_ProcessDList();
#endif
}

void PluginAPI::ProcessRDPList()
{
	//LOG(LOG_APIFUNC, "ProcessRDPList\n");
#ifdef RSPTHREAD
	_callAPICommand(ProcessRDPListCommand());
#else
	RDP_ProcessRDPList();
#endif
}

void PluginAPI::RomClosed()
{
	//LOG(LOG_APIFUNC, "RomClosed\n");
#ifdef RSPTHREAD
   _callAPICommand(RomClosedCommand(
					&m_rspThreadMtx,
					&m_pluginThreadMtx,
					&m_rspThreadCv,
					&m_pluginThreadCv)
	);
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
   m_pRspThread = new std::thread(RSP_ThreadProc, &m_rspThreadMtx, &m_pluginThreadMtx, &m_rspThreadCv, &m_pluginThreadCv, &m_pCommand);
	m_pRspThread->detach();
	m_pluginThreadCv.wait(m_pluginThreadMtx);
	m_pluginThreadMtx.unlock();
#else
	RSP_Init();
	GBI.init();
	Config_LoadConfig();
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
   _callAPICommand(ProcessUpdateScreenCommand());
#else
	VI_UpdateScreen();
#endif
}

void PluginAPI::_initiateGFX(const GFX_INFO & _gfxInfo) const {
	HEADER = gfx_info.HEADER;
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
#ifdef RSPTHREAD
   _callAPICommand(FBReadCommand(_addr));
#else
	FrameBufferRead(addr);
#endif
}

void PluginAPI::FBGetFrameBufferInfo(void *pinfo)
{
	FrameBufferGetInfo(pinfo);
}
