// stdafx.h : 자주 사용하지만 자주 변경되지는 않는
// 표준 시스템 포함 파일 또는 프로젝트 관련 포함 파일이
// 들어 있는 포함 파일입니다.
//

#pragma comment(lib,"ws2_32")
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include "targetver.h"

#include <stdio.h>
#include <tchar.h>
#include <WinSock2.h>
#include <Windows.h>
#include <process.h>
#include <mstcpip.h>
#include <locale>
#include <codecvt>
#include <vector>
#include <cstdarg>


// TODO: 프로그램에 필요한 추가 헤더는 여기에서 참조합니다.

#include "edhConfig.h"
#include "ChattingProtocol.h"
#include "MonitorProtocol.h"

#include "APIHook.h"
#include "CCrashDump.h"
#include "log.h"
#include "resourceGetter.h"

#include "boost\lockfree\queue.hpp"
#include "boost\lockfree\stack.hpp"

#include "winQueue.h"
#include "Sbuf.h"
#include "memoryPool.h"

#include "LanClient.h"
#include "MonitorClient.h"

#include "VirtualIOCP.h"
#include "virtualClient.h"
