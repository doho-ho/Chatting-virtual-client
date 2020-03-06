#include "stdafx.h"

__int64 netId = 0;

bool virtualIOCP::Start(const char *_configData)
{
	loadConfig(_configData);

	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
	{
		OnError(0, L"윈속 초기화 에러");
		return false;
	}

	hcp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	if (!hcp)
	{
		OnError(0, L"IOCP HANDLE CREATE ERROR");
		return false;
	}

	// bind()
	ZeroMemory(&addr, sizeof(addr));

	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr(IP);
	addr.sin_port = htons(Port);

	// Linger option -> Port time_wait 
	lingerOption.l_onoff = 1;
	lingerOption.l_linger = 0;

	// 스택생성
	connectStack = new boost::lockfree::stack<unsigned __int64>;
	indexStack = new boost::lockfree::stack<unsigned __int64>;

	// 배열 설정
	sessionArr = new Session[maxClient];
	threadArr = new HANDLE[threadCount];
	for (unsigned int i = 0; i < maxClient; i++)
		indexStack->push(i);
	
	// init
	connectTotal = 0;
	connectFailCount = 0;
	connectTPS = 0;
	sendTPS = 0;
	recvTPS = 0;
	clientCounter = 0;

	ctQuit = false;

	connectStackCount = 0;

	// 스레드 생성
	connectThreadHandle = (HANDLE)_beginthreadex(NULL, 0, connectThread, (LPVOID)this, 0, 0);

	for (int i = 0; i < threadCount; i++)
		threadArr[i] = (HANDLE)_beginthreadex(NULL, 0, workerThread, (LPVOID)this, 0, 0);

	return true;
}

bool virtualIOCP::Stop(void)
{
	// Connect  스레드 종료
	ctQuit = true;

	// 연결된 session 모두 disconnect;
	printf("session disconnet waiting...\n");
	int i = 0;
	for (i = 0; i < maxClient; i++)
	{
		if (sessionArr[i].Sock != NULL)
			shutdown(sessionArr[i].Sock, SD_BOTH);
	}
	printf("session disconnet success\n");

	// worker 스레드 종료
	for (i = 0; i < threadCount; i++)
		PostQueuedCompletionStatus(hcp, 0, 0, 0);

	unsigned __int64 Index;
	while (!connectStack->empty())
		connectStack->pop(Index);
	while (!indexStack->empty())
		indexStack->pop(Index);
	// 대기 : 모든 스레드가 종료 될 때 까지
	WaitForSingleObject(connectThreadHandle, INFINITE);
	printf("Connect thread closing completed.\n");
	WaitForMultipleObjects(threadCount, threadArr, TRUE, INFINITE);
	printf("All worker thread closing completed.\n");

	CloseHandle(hcp);
	delete[] sessionArr;
	delete[] threadArr;
	delete connectStack;
	WSACleanup();

	return true;
}

void virtualIOCP::loadConfig(const char *_configData)
{
	rapidjson::Document Doc;
	Doc.Parse(_configData);

	rapidjson::Value &Value = Doc["IOCP_Client"];
	limitCount = Value["Limite_count"].GetUint();;
	maxClient = Value["Max_client_count"].GetUint();;
	threadCount = Value["Workerthread_count"].GetUint();;

	strcpy_s(IP, 16, Value["Server_ip"].GetString());
	Port = Value["Port"].GetUint();;
	nagleOption = Value["Nagle"].GetBool();

	rapidjson::Value &Key = Doc["Encryption_key"];
	assert(arry.IsArry());
	Code = (char)Key[0].GetUint();
	Key1 = (char)Key[1].GetUint();
	Key2 = (char)Key[2].GetUint();
}

LONG virtualIOCP::GetClientCount(void)
{
	return clientCounter;
}

void virtualIOCP::SendPacket(unsigned __int64 _index, Sbuf *_buf, bool _type)
{
	Session *ss = acquirLock(_index);
	if (!ss)
		return;

	int retval;
	_buf->Encode(Code, Key1,Key2);
	_buf->addRef();
	ss->sendQ.push(_buf);

	if (_type == true)
	{
		InterlockedCompareExchange((LONG*)&ss->sendDisconnectFlag, true, false);
		if (ss->sendQ.empty())
			clientShutdown(ss->Index);
	}

	if (ss->sendFlag == false)
	{
		if (ss->sendPQCS == 0)
		{
			if (0 == InterlockedCompareExchange((LONG*)&ss->sendPQCS, 1, 0))
			{
				PostQueuedCompletionStatus(hcp, 0, (ULONG_PTR)ss, (LPOVERLAPPED)1);
			}
		}
	}

	releaseLock(ss);
	return;
}

// private 

unsigned __stdcall virtualIOCP::connectThread(LPVOID _data)
{
	virtualIOCP *server = (virtualIOCP*)_data;

	// Session associated variable
	Session *ss = NULL;
	boost::lockfree::stack<unsigned __int64> *connectStack = server->connectStack;
	unsigned __int64 Index = 0;		// Session index number
	int arrIndex = 0;						// Session array index

	// Network associated variable
	SOCKADDR_IN addr = server->addr;
	tcp_keepalive keep = server->tcpKeep;
	LINGER lingerOption = server->lingerOption;
	bool Nagle = server->nagleOption;

	// Local variable
	int retval = 0;
	int count = 0;
	int errorCode = 0;
	int limitCount = server->limitCount;

	while (!server->ctQuit)
	{
		count = 0;
		while (count < limitCount && !connectStack->empty())
		{
			count++;
			Index = 0;
			// stack pop
			connectStack->pop(Index);
			InterlockedDecrement(&server->connectStackCount);
			//socket
			SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
			if (sock == INVALID_SOCKET)
			{
				_SYSLOG(Type::Type_CONSOLE, Level::SYS_ERROR, L"SOCKET ERROR CODE : %d", INVALID_SOCKET);
				break;
			}

			bool optval = TRUE;
			// nagle option
			if (Nagle == true)
				setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char*)&optval, sizeof(optval));

			retval = setsockopt(sock, SOL_SOCKET, SO_LINGER, (char*)&lingerOption, sizeof(lingerOption));
			if (retval == SOCKET_ERROR)
				printf("Linger ERROR : %d", WSAGetLastError());

			// connect
			retval = connect(sock, (SOCKADDR*)&addr, sizeof(addr)); // WSACeept사용합시다.
			if (retval == SOCKET_ERROR)	// closed listensock : 종료
			{
				errorCode = WSAGetLastError();
				_SYSLOG(Type::Type_CONSOLE, Level::SYS_ERROR, L"CONNECT ERROR CODE : %d", errorCode);
				server->connectFailCount++;
				connectStack->push(Index);
				continue;
			}
			server->connectTPS++;
			server->connectTotal++;

			ss = server->insertSession(sock, Index);
			CreateIoCompletionPort((HANDLE)sock, server->hcp, (ULONG_PTR)ss, 0);	// iocp 등록
			InterlockedIncrement(&server->clientCounter);
			ss->recvFlag = true;
			server->recvPost(ss);
			server->OnClientJoin(ss->Index);

		}
		Sleep(150);
	}
	printf("Connect thread closing completed.\n");
	return 0;
}

unsigned __stdcall virtualIOCP::workerThread(LPVOID _data)
{
	virtualIOCP *server = (virtualIOCP*)_data;

	int retval = 1;
	DWORD trans = 0;
	OVERLAPPED *over = NULL;
	Session *_ss = NULL;

	while (1)
	{
		trans = 0;
		over = NULL, _ss = NULL;
		retval = GetQueuedCompletionStatus(server->hcp, &trans, (PULONG_PTR)&_ss, (LPOVERLAPPED*)&over, INFINITE);
		if (!over)
		{
			if (retval == false)
			{
				server->OnError(WSAGetLastError(), L"GQCS error : overlapped is NULL and return false");
				break;
			}
			if (trans == 0 && !_ss)		// 종료 신호
			{
				break;
			}
		}
		else
		{
			if (1 == (int)over)
			{
				server->sendPost(_ss);
				if (_ss->sendFlag == false && _ss->sendQ.empty())
					server->sendPost(_ss);
				InterlockedDecrement((LONG*)&_ss->sendPQCS);
			}

			if (&(_ss->recvOver) == over)
				server->completeRecv(trans, _ss);

			if (&(_ss->sendOver) == over)
				server->completeSend(trans, _ss);

			if (0 == _ss->recvFlag)
			{
				if (_ss->sendFlag == false && _ss->usingFlag == false && _ss->sendPQCS == false)
					server->disconnect(_ss);
			}
		}

	}
	return 0;
}

Session* virtualIOCP::insertSession(SOCKET _sock, unsigned __int64 _Index)
{
	sessionArr[_Index].Index = setID(_Index, netId);
	netId++;
	sessionArr[_Index].Sock = _sock;

	return &sessionArr[_Index];
}

LONG virtualIOCP::getConnectTotal(void)
{
	return connectTotal;
}

LONG virtualIOCP::getConnectFail(void)
{
	return connectFailCount;
}

LONG virtualIOCP::getConnetTPS(void)
{
	return connectTPS;
}

LONG virtualIOCP::getSendTPS(void)
{
	return sendTPS;
}

LONG virtualIOCP::getRecvTPS(void)
{
	return recvTPS;
}

void virtualIOCP::setTPS(void)
{
	connectTPS = 0;
	InterlockedExchange(&sendTPS, 0);
	InterlockedExchange(&recvTPS, 0);
	return;
}

bool virtualIOCP::callConnect(void)
{
	unsigned __int64 Index;
	if (!indexStack->pop(Index))
		return false;
	if(connectStack->push(Index))
		InterlockedIncrement(&connectStackCount);
	return true;
}

void virtualIOCP::recvPost(Session *_ss)
{

	DWORD recvVal, flag;
	int retval, err;
	winBuffer *_recv = &_ss->recvQ;
	ZeroMemory(&(_ss->recvOver), sizeof(_ss->recvOver));
	WSABUF wbuf[2];
	ZeroMemory(&wbuf, sizeof(WSABUF) * 2);
	wbuf[0].buf = _recv->getRearPosPtr();
	wbuf[0].len = _recv->getNotBrokenFreeSize();

	if (_recv->getFreeSize() == 0)
		clientShutdown(_ss);

	if (_recv->getFreeSize() > _recv->getNotBrokenFreeSize())
	{
		wbuf[1].buf = _recv->getBufferPtr();
		wbuf[1].len = (_recv->getFreeSize() - _recv->getNotBrokenFreeSize());
	}
	// RECV 공간이 없는 경우 예외 처리 해주세요. 패킷자체가 문제가 있는것. 헤더의 길이가 잘못 됨
	// 클라가 깨진 패킷을 쐈거나 누군가 공격을 하는 것 이므로 연결을 끊으세요. 중대한 에러입니다. 
	recvVal = 0, flag = 0;
	retval = WSARecv(_ss->Sock, wbuf, 2, &recvVal, &flag, &(_ss->recvOver), NULL);

	if (retval == SOCKET_ERROR)
	{
		// PENDING 인 경우 나중에 처리된다는 의미
		err = WSAGetLastError();
		if (err != WSA_IO_PENDING)
		{
			_ss->recvFlag = false;
			if (err != WSAECONNRESET && err != WSAESHUTDOWN && err != WSAECONNABORTED)
			{
				_SYSLOG(Type::Type_CONSOLE, Level::SYS_ERROR, L"RECV POST ERROR CODE : %d", err);
				WCHAR errString[512] = L"";
				wsprintf(errString, L"RECV ERROR [SESSION_ID : %d] : %d", _ss->Index, err);
				OnError(0, errString);
			}
			clientShutdown(_ss);
		}
	}
	return;
}

void virtualIOCP::sendPost(Session *_ss)
{
	DWORD sendVal = 0;

	int count = 0;
	int result = 0;
	int size = 0;
	WSABUF wbuf[maxWSABUF];
	wbuf[count].buf = 0;
	wbuf[count].len = 0;
	boost::lockfree::queue<Sbuf*> *_send = &_ss->sendQ;
	boost::lockfree::queue<Sbuf*> *completeSend = &_ss->completeSendQ;
	if (_send->empty()) return;
	if (0 == InterlockedCompareExchange((LONG*)&(_ss->sendFlag), 1, 0))
	{
		_ss->sendCount = 0;
		count = 0;
		int retval = 0;
		int count = 0;
		Sbuf *buf;
		ZeroMemory(&_ss->sendOver, sizeof(_ss->sendOver));
		do
		{
			for (count; count < maxWSABUF; )
			{
				buf = NULL;
				retval = _send->pop(buf);
				if (retval == false || !buf) break;
				wbuf[count].buf = buf->getHeaderPtr();
				wbuf[count].len = buf->getPacketSize();
				completeSend->push(buf);
				count++;
			}
			if (count >= maxWSABUF)
				break;
		} while (!_send->empty());

		_ss->sendCount = count;
		if (count == 0)
		{
			InterlockedExchange((LONG*)&(_ss->sendFlag), 0);
			return;
		}
		retval = WSASend(_ss->Sock, wbuf, _ss->sendCount, &sendVal, 0, &_ss->sendOver, NULL);
		if (retval == SOCKET_ERROR)
		{
			int err = WSAGetLastError();
			if (err != WSA_IO_PENDING)
			{
				if (err != WSAECONNRESET && err != WSAESHUTDOWN && err != WSAECONNABORTED)
				{
					_SYSLOG(Type::Type_CONSOLE, Level::SYS_ERROR, L"SEND POST ERROR CODE : %d", err);
					WCHAR errString[512] = L"";
					wsprintf(errString, L"SEND ERROR [SESSION_ID : %d] : %d", _ss->Index, err);
					OnError(0, errString);
				}
				_ss->sendCount = 0;
				clientShutdown(_ss);
				InterlockedExchange((LONG*)&(_ss->sendFlag), 0);
				// 다른 스레드에서  sendPost를 호출하는 것을 막아보기 위해 shutdown함수 호출 다음에 sendFlag 변경
			}
		}
	}

	return;
}

void virtualIOCP::completeRecv(LONG _trans, Session *_ss)
{
	int usedSize;
	int retval = 0;
	if (_trans == 0)
	{
		clientShutdown(_ss);
		_ss->recvFlag = false;
		return;
	}
	else
	{
		winBuffer *_recv = &_ss->recvQ;
		_recv->moveRearPos(_trans);
		while (usedSize = _recv->getUsedSize())
		{
			netHeader head;
			retval = _recv->peek((char*)&head, sizeof(netHeader));
			if (retval == 0 || (usedSize - sizeof(netHeader)) < head.len)
				break;
			InterlockedIncrement(&recvTPS);
			try
			{
				Sbuf *buf = Sbuf::Alloc();
				retval = _recv->dequeue(buf->getBufPtr(), sizeof(netHeader) + head.len);
				buf->moveRearPos(head.len);
				if (buf->Decode(Code,Key1,Key2))
					OnRecv(_ss->Index, buf);
				else
					throw 4900;
				buf->Free();
			}
			catch (int num)
			{
				if (num != 4900)
					_SYSLOG(Type::Type_CONSOLE, Level::SYS_ERROR, L"에러코드 : %d", num);
			}
		}
		recvPost(_ss);
	}
	return;
}

void virtualIOCP::completeSend(LONG _trans, Session *_ss)
{
	if (_trans == 0)
	{
		InterlockedDecrement((LONG*)&(_ss->sendFlag));
		clientShutdown(_ss);
		return;
	}
	else
	{
		Sbuf *buf;
		boost::lockfree::queue<Sbuf*> *completeSend = &_ss->completeSendQ;
		for (int i = 0; i < _ss->sendCount;)
		{
			buf = NULL;
			completeSend->pop(buf);
			if (!buf) continue;
			buf->Free();
			i++;
			InterlockedIncrement(&sendTPS);
		}
		InterlockedDecrement((LONG*)&(_ss->sendFlag));
		_ss->sendCount = 0;
	}
	if (_ss->sendQ.empty())
	{
		if (1 == InterlockedCompareExchange((LONG*)&(_ss->sendDisconnectFlag), 1, 1))
			clientShutdown(_ss->Index);
	}
	sendPost(_ss);
}

void virtualIOCP::clientShutdown(Session *_ss)
{
	InterlockedExchange((LONG*)&_ss->disconnectFlag, 1);
	shutdown(_ss->Sock, SD_SEND);
	return;
}

void virtualIOCP::disconnect(Session *_ss)
{
	ULONG64 dummyIndex = 0;
	if (_ss->disconnectFlag == true && _ss->recvFlag == false
		&& _ss->sendFlag == false && _ss->usingFlag == false)
	{
		if (1 == InterlockedCompareExchange((LONG*)&_ss->disconnectFlag, 0, 1))
		{
			Sbuf *buf = NULL;
			InterlockedDecrement(&clientCounter);
			while (1)
			{
				buf = NULL;
				_ss->sendQ.pop(buf);
				if (!buf) break;
				buf->Free();
			}
			while (1)
			{
				buf = NULL;
				_ss->completeSendQ.pop(buf);
				if (!buf) break;
				buf->Free();
			}
			dummyIndex = _ss->Index;
			OnClientLeave(_ss->Index);
			_ss->Index = 0;
			indexStack->push(getIndex(dummyIndex));

			closesocket(_ss->Sock);
			_ss->Sock = INVALID_SOCKET;
		}
	}
	return;
}

void virtualIOCP::disconnect(SOCKET _sock)
{
	shutdown(_sock, SD_SEND);
	closesocket(_sock);
	return;
}

Session* virtualIOCP::acquirLock(unsigned __int64 _id)
{
	__int64 index = getIndex(_id);
	Session *ss = &sessionArr[index];

	if (1 == InterlockedCompareExchange((LONG*)&ss->usingFlag, 0, 1))
	{
		if (true == ss->disconnectFlag)
			disconnect(ss);
		return NULL;
	}

	if (ss->Index != _id)
	{
		if (true == ss->disconnectFlag)
			disconnect(ss);
		return NULL;
	}

	if (true == ss->disconnectFlag)
	{
		disconnect(ss);
		return NULL;
	}

	else
		return &sessionArr[index];
	return NULL;
}

void virtualIOCP::releaseLock(Session *_ss)
{
	InterlockedExchange((LONG*)&_ss->usingFlag, 0);
	if (true == _ss->disconnectFlag)
		disconnect(_ss);
	return;
}

void virtualIOCP::clientShutdown(unsigned __int64 _index)
{
	Session *_ss = acquirLock(_index);
	if (_ss)
	{
		InterlockedExchange((LONG*)&_ss->disconnectFlag, 1);
		shutdown(_ss->Sock, SD_SEND);
		releaseLock(_ss);
		return;
	}
	CCrashDump::Crash();
	return;
}