#ifdef OS_WINDOWS
# include <windows.h>
#else
# include <winlnxdefs.h>
#endif // OS_WINDOWS
#include <assert.h>

#include <PluginAPI.h>

#include <N64.h>
#include <GLideN64.h>
#include <RSP.h>
#include <RDP.h>
#include <VI.h>
#include <Config.h>
#include <FrameBufferInfo.h>
#include <TextureFilterHandler.h>
#include "Graphics/Context.h"
#include <DisplayWindow.h>

extern "C" void Config_LoadConfig();

PluginAPI & PluginAPI::get()
{
	static PluginAPI api;
	return api;
}

#ifdef RSPTHREAD
class RomOpenCommand : public APICommand {
public:
	bool run() {
		gli_RSP_Init();
		gli_GBI_init();
		Config_LoadConfig();
		dwnd().start();
		assert(!gfxContext.isError());
		return true;
	}
};

void PluginAPI::_callAPICommand(APICommand && _command)
{
	m_rspThread.call(_command);
}

class ProcessDListCommand : public APICommand {
public:
	bool run() {
		gli_RSP_ProcessDList();
		return true;
	}
};

class ProcessRDPListCommand : public APICommand {
public:
	bool run() {
		gli_RDP_ProcessRDPList();
		return true;
	}
};

class ProcessUpdateScreenCommand : public APICommand {
public:
	bool run() {
		gli_VI_UpdateScreen();
		return true;
	}
};

class FBReadCommand : public APICommand {
public:
	FBReadCommand(u32 _addr) : m_addr(_addr) {
	}

	bool run() {
		gli_fbinfo_read(m_addr);
		return true;
	}
private:
	u32 m_addr;
};

class ReadScreenCommand : public APICommand {
public:
	ReadScreenCommand(void **_dest, long *_width, long *_height)
		: m_dest(_dest)
		, m_width(_width)
		, m_height(_height) {
	}

	bool run() {
		dwnd().readScreen(m_dest, m_width, m_height);
		return true;
	}

private:
	void ** m_dest;
	long * m_width;
	long * m_height;
};

class RomClosedCommand : public APICommand {
public:
	bool run() {
		TFH.dumpcache();
		dwnd().stop();
		gli_GBI_destroy();
		gli_RSP_StereoDestroy();
		return false;
	}

};
#endif

void PluginAPI::ProcessDList()
{
#ifdef RSPTHREAD
	_callAPICommand(ProcessDListCommand());
#else
	gli_RSP_ProcessDList();
#endif
}

void PluginAPI::ProcessRDPList()
{
#ifdef RSPTHREAD
	_callAPICommand(ProcessRDPListCommand());
#else
	gli_RDP_ProcessRDPList();
#endif
}

void PluginAPI::RomClosed()
{
	if (!m_bRomOpen)
		return;

	m_bRomOpen = false;

#ifdef RSPTHREAD
	_callAPICommand(RomClosedCommand());
	m_rspThread.join();
#else
	TFH.dumpcache();
	dwnd().stop();
	gli_GBI_destroy();
	gli_RSP_StereoDestroy();
#endif
}

void PluginAPI::RomOpen()
{
	if (m_bRomOpen)
		return;

#ifdef RSPTHREAD
	// Resource failure or a target without lock-free atomics runs inline.
	m_rspThread.start();
	_callAPICommand(RomOpenCommand());
#else
	gli_RSP_Init();
	gli_GBI_init();
	Config_LoadConfig();
	dwnd().start();
#endif
	m_bRomOpen = true;
}

void PluginAPI::ShowCFB()
{
	gDP.changed |= CHANGED_CPU_FB_WRITE;
}

void PluginAPI::UpdateScreen()
{
#ifdef RSPTHREAD
	_callAPICommand(ProcessUpdateScreenCommand());
#else
	gli_VI_UpdateScreen();
#endif
}

void PluginAPI::_initiateGFX(const GFX_INFO & _gfxInfo) const {
	HEADER = _gfxInfo.HEADER;
	DMEM = _gfxInfo.DMEM;
	IMEM = _gfxInfo.IMEM;
	RDRAM = _gfxInfo.RDRAM;

	REG.MI_INTR = _gfxInfo.MI_INTR_REG;
	REG.DPC_START = _gfxInfo.DPC_START_REG;
	REG.DPC_END = _gfxInfo.DPC_END_REG;
	REG.DPC_CURRENT = _gfxInfo.DPC_CURRENT_REG;
	REG.DPC_STATUS = _gfxInfo.DPC_STATUS_REG;
	REG.DPC_CLOCK = _gfxInfo.DPC_CLOCK_REG;
	REG.DPC_BUFBUSY = _gfxInfo.DPC_BUFBUSY_REG;
	REG.DPC_PIPEBUSY = _gfxInfo.DPC_PIPEBUSY_REG;
	REG.DPC_TMEM = _gfxInfo.DPC_TMEM_REG;

	REG.VI_STATUS = _gfxInfo.VI_STATUS_REG;
	REG.VI_ORIGIN = _gfxInfo.VI_ORIGIN_REG;
	REG.VI_WIDTH = _gfxInfo.VI_WIDTH_REG;
	REG.VI_INTR = _gfxInfo.VI_INTR_REG;
	REG.VI_V_CURRENT_LINE = _gfxInfo.VI_V_CURRENT_LINE_REG;
	REG.VI_TIMING = _gfxInfo.VI_TIMING_REG;
	REG.VI_V_SYNC = _gfxInfo.VI_V_SYNC_REG;
	REG.VI_H_SYNC = _gfxInfo.VI_H_SYNC_REG;
	REG.VI_LEAP = _gfxInfo.VI_LEAP_REG;
	REG.VI_H_START = _gfxInfo.VI_H_START_REG;
	REG.VI_V_START = _gfxInfo.VI_V_START_REG;
	REG.VI_V_BURST = _gfxInfo.VI_V_BURST_REG;
	REG.VI_X_SCALE = _gfxInfo.VI_X_SCALE_REG;
	REG.VI_Y_SCALE = _gfxInfo.VI_Y_SCALE_REG;

	CheckInterrupts = _gfxInfo.CheckInterrupts;

	REG.SP_STATUS = nullptr;
}

void PluginAPI::ChangeWindow()
{
	dwnd().setToggleFullscreen();
	if (!m_bRomOpen)
		dwnd().closeWindow();
}

void PluginAPI::FBWrite(unsigned int _addr, unsigned int _size)
{
	gli_fbinfo_write(_addr, _size);
}

void PluginAPI::FBRead(unsigned int _addr)
{
#ifdef RSPTHREAD
	_callAPICommand(FBReadCommand(_addr));
#else
	gli_fbinfo_read(_addr);
#endif
}

void PluginAPI::FBGetFrameBufferInfo(void * _pinfo)
{
	gli_fbinfo_get_info(_pinfo);
}

