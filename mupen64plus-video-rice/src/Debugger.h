/*
Copyright (C) 2002 Rice1964

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#if !defined(DEBUGGER_H)
#define DEBUGGER_H

/* Rice's debugger is gone; what its calls were left as does nothing. */

#include <retro_inline.h>

#define DEBUG_DUMP_VERTEXES(str, v0, v1, v2)
#define DEBUGGER_PAUSE_COUNT_N(op)
#define DebuggerPauseCountN DEBUGGER_PAUSE_COUNT_N
#define RSP_RDP_NOIMPL(a,b,c)
static INLINE void DebuggerAppendMsg(const char * Message, ...) { (void)Message; }
#define TXTRBUF_DUMP(dumpfunc)
#define TXTRBUF_DETAIL_DUMP(dumpfunc)
#define TXTRBUF_OR_CI_DUMP(dumpfunc)
#define TXTRBUF_OR_CI_DETAIL_DUMP(dumpfunc)
#define WARNING(dumpfunc)
#define FOG_DUMP(dumpfunc)
#define LOG_TEXTURE(dumpfunc)

#define TRACE0(arg0)        {}
#define TRACE1(arg0,arg1)   {}
#define TRACE2(arg0,arg1,arg2)      {}
#define TRACE3(arg0,arg1,arg2,arg3) {}
#define TRACE4(arg0,arg1,arg2,arg3,arg4)        {}
#define TRACE5(arg0,arg1,arg2,arg3,arg4,arg5)   {}

#define DEBUG_TRIANGLE(arg0)    {}

#endif /* !defined(DEBUGGER_H) */
