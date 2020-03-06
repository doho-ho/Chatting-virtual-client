#include "stdafx.h"

void virtualClient::Player::initialize()
{
	if (0 > InterlockedDecrement(&waitQ))
		InterlockedIncrement(&waitQ);
	InterlockedExchange(&loginFlag, 0);

	Index = 0;
	playerCode = 0;
	nowType = dataType::None;
	dealyTime = 0;
	sendTime = 0;
	resCounter = -1;
	for (int i = 0; i < resCountSize; i++)
		resTimeArray[i] = 0;
}

void virtualClient::Player::set_resCountSize(int _resCountSize)
{
	resCountSize = _resCountSize;
	resTimeArray = new ULONGLONG[resCountSize];
	for (int i = 0; i < resCountSize; i++)
		resTimeArray[i] = 0;

	initialize();
}

ULONGLONG virtualClient::Player::set_responseTime(ULONGLONG _nowTime)
{
	ULONGLONG resTime = _nowTime - sendTime;

	resCounter += 1;
	resCounter %= resCountSize;
	resTimeArray[resCounter] = resTime;

	return resTime;
}

ULONGLONG virtualClient::Player::get_avrTime(void)
{
	ULONGLONG retVal = 0;
	int i = 0;
	for (i; i < resCountSize; ++i)
	{
		if (resTimeArray[i] == 0)
			break;
		retVal += resTimeArray[i];
	}

	if (i == 0)
		return retVal;
	else
	{
		return retVal / i;
	}
}

virtualClient::Client::Client(const char *_configData)
{
	loadConfigData(_configData);

	const char *chatData = loadFile(chatDataName.c_str());
	loadChatData(chatData);
	delete chatData;

	pauseFlag = false;
	terminateFlag = false;

	waitQ = new boost::lockfree::queue<Player*>;
	Monitor = new monitorClient(monitorConfigName.c_str());

	connectDummyPtr = new Player;
	connectDummyPtr->set_resCountSize(resTimeCountSize);
	InterlockedIncrement(&(connectDummyPtr->waitQ));
	connectDummyPtr->set_Type(dataType::Connect);
	playerArray = new Player[maxClient];

	for (int i = 0; i < maxClient; ++i)
		playerArray[i].set_resCountSize(resTimeCountSize);

	disconnectCount = 0l;
	shutdownCount = 0;
	notRecvCount = 0;
	loginClientCount = 0;
	contentThreadTPS = 0;
	contentFrame = 0;

	HANDLE hThread;

	hThread = (HANDLE)_beginthreadex(NULL, 0, monitorThread, (LPVOID)this, 0, 0);
	CloseHandle(hThread);

	for (int i = 0; i < contentThreadCount; i++)
	{
		hThread = (HANDLE)_beginthreadex(NULL, 0, contentThread, (LPVOID)this, 0, 0);
		CloseHandle(hThread);
	}

	srand(GetTickCount64());
	Start(_configData);

	for (int Count = 0; Count < maxClient; Count++)
		proc_connect();

}

virtualClient::Client::~Client()
{
	terminateFlag = true;

	Stop();

	Player	*dummyUser;
	while (true)
	{
		waitQ->pop(dummyUser);
		if (!dummyUser)
			break;
		dummyUser = nullptr;
	}
	delete waitQ;
	delete Monitor;
	delete connectDummyPtr;
	delete[] chatList;
	delete[] playerArray;
}

void virtualClient::Client::loadConfigData(const char *_configData)
{
	rapidjson::Document Doc;
	Doc.Parse(_configData);

	rapidjson::Value &Value = Doc["Chat_Client"];

	actionDealy = Value["Action_dealy"].GetUint();
	loginDealy = Value["Login_dealy"].GetUint();

	connectProbability = Value["Connect_probability"].GetUint();
	disconnectProbability = Value["Disconnect_probability"].GetUint();
	chatProbability = Value["Chat_probability"].GetUint();
	contentThreadCount = Value["Contentthread_count"].GetUint();
	contentThreadLoopCount = Value["Contentthread_loopCount"].GetUint();
	resTimeCountSize = Value["resTimeCounter"].GetUint();

	if (disconnectProbability < 50 ? contentProbability = 100 - disconnectProbability : contentProbability = 50);
	
	chatDataName = Value["Chat_data_file_name"].GetString();
	monitorConfigName = Value["Monitor_config_file_name"].GetString();

	rapidjson::Value &Value1 = Doc["IOCP_Client"];
	maxClient = Value1["Max_client_count"].GetUint();
}

void virtualClient::Client::loadChatData(const char *_chatData)
{
	rapidjson::Document Doc;
	Doc.Parse(_chatData);

	const rapidjson::Value &Value = Doc["Chat"];
	assert(Value.IsArry());
	chatSize = Value.Size();
	chatList = new std::wstring[chatSize];

	std::string dummyStr;
	for (rapidjson::SizeType i = 0; i < Value.Size(); i++)
	{
		dummyStr = Value[i].GetString();
		std::wstring_convert<std::codecvt_utf8<wchar_t>,wchar_t> test;
		chatList[i] = test.from_bytes(dummyStr);
		dummyStr.clear();
	}
}

unsigned __stdcall virtualClient::Client::monitorThread(LPVOID _Data)
{
	Client *clientPtr = (Client*)_Data;

	SYSTEMTIME stNowTime;
	GetLocalTime(&stNowTime);
	char startTime[100];
	std::snprintf(startTime, 100, "Start Time : %d.%d.%d %02d:%02d:%02d", stNowTime.wYear, stNowTime.wMonth, stNowTime.wDay,
		stNowTime.wHour, stNowTime.wMinute, stNowTime.wSecond);

	while (!clientPtr->terminateFlag)
	{
		printf("---------------------------------------------------\n");
		printf(" Chatting Virtual Client (Ver1.0)\n");
		printf("---------------------------------------------------\n");
		printf(" Start time : %s \n", startTime);
		printf(" Pause : P | Restart : R | Quit : Q\n");
		printf(" Now status : %s  |  Monitor connection : %s\n", clientPtr->pauseFlag ? "Pause" : "Running", clientPtr->Monitor->serverConnectionFlag ? "On" : "Off");
		printf("---------------------------------------------------\n");
		printf("[Config]--------------------------------------------\n");
		printf("   Limite connect count : %d  | Connect : %d%%\n   content % : %d%%  | disconnect : %d%%\n",
			clientPtr->maxClient, clientPtr->connectProbability, clientPtr->chatProbability, clientPtr->disconnectProbability);
		printf("[Connect]------------------------------------------\n");
		printf("   Connect Total : %d  | Connect Failed : %d \n", clientPtr->getConnectTotal(), clientPtr->getConnectFail());
		printf("   Now connected : %d  | Now disconnected : %d \n", clientPtr->GetClientCount(), clientPtr->disconnectCount);
		printf("   Shut down count : %d | Login count : %d \n", clientPtr->shutdownCount, clientPtr->loginClientCount);
		printf("[TPS]----------------------------------------------\n");
		printf("   Connect TPS : %d | Content TPS : %d\n", clientPtr->getConnetTPS(), clientPtr->contentThreadTPS);
		printf("   Recv TPS : %d | Send TPS %d\n", clientPtr->getRecvTPS(), clientPtr->getSendTPS());
		printf("[Contents]-----------------------------------------\n");
		printf("   No responding yet : %d  | Content Frame : %d \n", clientPtr->notRecvCount, clientPtr->contentFrame);
		printf("   Sbuf alloc : %d | Sbuf used : %d \n", Sbuf::pool->getAllocCount(), Sbuf::pool->getUsedCount());
		printf("   Avr res time : %llu ms  |  Max res time : %llu ms \n", clientPtr->avrResponseTime, clientPtr->maxResponseTime);
		printf("[Test]----------------------------------------------\n");
		printf("   Connect stack count : %d\n", clientPtr->connectStackCount);
		printf("\n\n");
		clientPtr->Monitor->sendData(5, clientPtr->getConnetTPS(), clientPtr->getRecvTPS(), clientPtr->getSendTPS(), clientPtr->contentThreadTPS, clientPtr->avrResponseTime);
		clientPtr->set_avrResTime();
		clientPtr->setTPS();
		clientPtr->contentThreadTPS = 0;
		clientPtr->contentFrame = 0;
		Sleep(999);
	}
	printf("Monitor thread closing completed.\n");
	return 0;
}

unsigned __stdcall virtualClient::Client::contentThread(LPVOID _Data)
{
	Client *clientPtr = (Client*)_Data;
	Player *User = NULL;
	int retCount = 0;
	dataType retType = dataType::None;
	int loopCount = 0;
	unsigned int limitLoopCount = clientPtr->contentThreadLoopCount;

	while (!clientPtr->terminateFlag)
	{
		while (clientPtr->pauseFlag)
		{
			if (clientPtr->terminateFlag)
				break;
			Sleep(1000);
		}
		loopCount = 0;
		// Wait Q dequeue
		while (loopCount < limitLoopCount)
		{
			User = NULL;
			clientPtr->waitQ->pop(User);
			if (User)
			{
				retCount = InterlockedDecrement(&(User->waitQ));
				if (retCount < 0)
					InterlockedIncrement(&(User->waitQ));
				else
				{
					retType = clientPtr->proc_probability(User);
					if (retType == dataType::Wait)
					{
						clientPtr->waitQ->push(User);
						InterlockedIncrement(&(User->waitQ));
					}
					else if (retType != dataType::None)
					{
						switch (retType)
						{
						case Login:
							clientPtr->proc_login(User);
							break;
						case Connect:
							clientPtr->proc_connect();
							break;
						case Disconnect:
							clientPtr->proc_disconnect(User);
							break;
						case Content:
							clientPtr->proc_contents(User);
							break;
						default:
							CCrashDump::Crash();
							break;
						}
						InterlockedIncrement(&(clientPtr->contentThreadTPS));
					}
				}
				loopCount++;
			}
			else
				break;
		}
		InterlockedIncrement(&(clientPtr->contentFrame));
		Sleep(15);
	}
	return 0;
}

// Login function
void virtualClient::Client::proc_connect()
{
	if (!callConnect())
	{
		InterlockedIncrement(&(connectDummyPtr->waitQ));
		waitQ->push(connectDummyPtr);
	}
}

void virtualClient::Client::proc_login(Player *_User)
{
	Sbuf *buf = packet_Login();

	_User->set_sendTime(GetTickCount64());
	_User->set_dealyTime(0);

	InterlockedIncrement(&notRecvCount);
	SendPacket(_User->get_Index(), buf);
	buf->Free();
}

void virtualClient::Client::proc_dataReq(Player *_User)
{
	Sbuf *buf = Sbuf::Alloc();
	*buf << (short)chatProtocol::Protocol::c2s_playerData_Req;
	SendPacket(_User->get_Index(), buf);
	buf->Free();
}

void virtualClient::Client::proc_disconnect(Player *_User)
{
	clientShutdown(_User->get_Index());
}

void virtualClient::Client::proc_contents(Player *_User)
{
	unsigned int chatNum = rand() % chatSize;
	Sbuf *buf = packet_Chat(_User->get_playerCode(), chatNum);
	_User->sendChatNum = chatNum;
	_User->set_sendTime(GetTickCount64());
	_User->set_dealyTime(0);
	InterlockedIncrement(&notRecvCount);
	SendPacket(_User->get_Index(), buf);
	buf->Free();
}

virtualClient::dataType virtualClient::Client::proc_probability(Player *_User)
{

	dataType Result = dataType::Wait;
	dataType nowType = _User->get_Type();
	unsigned int randResult = 0;

	if (nowType == dataType::Connect)
	{
		randResult = proc_rand();
		if (randResult <= connectProbability)
			Result = dataType::Connect;
	}
	else if (nowType == dataType::Login)
	{
		if (1 == _User->get_loginFlag())
			goto content;
		else if (GetTickCount64() - _User->get_dealyTime() >= loginDealy)
		{
			Result = dataType::Login;
		}
	}
	else if (nowType == dataType::Content)
	{
		content:
		if (GetTickCount64() - _User->get_dealyTime() >= actionDealy)
		{
			randResult = proc_rand();
			if (randResult <= contentProbability)	// Chatting
			{
				randResult = proc_rand();
				if (randResult <= chatProbability)
				{
					_User->set_Type(dataType::Content);
					Result = dataType::Content;
				}
			}
			else
			{
				randResult = proc_rand();
				if (randResult <= disconnectProbability)
				{
					_User->set_Type(dataType::Disconnect);
					Result = dataType::Disconnect;
				}
			}
		}
	}
	else if (nowType == dataType::None)
	{
		Result = dataType::None;
	}
	else
		CCrashDump::Crash();

	return Result;

}

int virtualClient::Client::proc_rand()
{
	return rand() % 100;
}

void virtualClient::Client::check_disconnect(Player *_User)
{
	if (_User->get_Type() == dataType::Disconnect)
		InterlockedIncrement(&disconnectCount);
	else
		InterlockedIncrement(&shutdownCount);

	if (_User->get_sendTime() != 0)
		InterlockedDecrement(&notRecvCount);
}

void virtualClient::Client::check_maxResTime(ULONGLONG _resTime)
{
	if (maxResponseTime < _resTime)
		maxResponseTime = _resTime;
}

virtualClient::Player* virtualClient::Client::get_userPtr(unsigned __int64 _Index)
{
	unsigned int Index = getIndex(_Index);
	if (Index >= maxClient) return NULL;
	return &playerArray[Index];
}

Sbuf* virtualClient::Client::packet_Login(void)
{
	Sbuf *buf = Sbuf::Alloc();
	*buf << (short)chatProtocol::Protocol::c2s_Login_Req;
	*buf << (short)chatProtocol::chatClientType::virtualClient;
	return buf;
}

Sbuf* virtualClient::Client::packet_Chat(unsigned __int64 _playerCode, unsigned int _chatNum)
{
	// unsigned __int64 playerCode
	// unsigned int		dataSize
	//	WCHAR				Data[50]
	Sbuf *buf = Sbuf::Alloc();
	*buf << (short)chatProtocol::c2s_Chatting;
	*buf << _playerCode;
	*buf << (int)(chatList[_chatNum].length() * sizeof(wchar_t));
	buf->push((char*)chatList[_chatNum].data(), (chatList[_chatNum].length()*sizeof(wchar_t)));
	return buf;
}

// Recv function
void virtualClient::Client::recv_Login(unsigned __int64 _Index, Sbuf *_buf)
{
	Player *User = get_userPtr(_Index);
	if (!User)
	{
		clientShutdown(_Index);
		CCrashDump::Crash();
		return;
	}

	char	Result;
	*_buf >> Result;
	if (Result == false)
		CCrashDump::Crash();

	unsigned __int64 playerCode;
	*_buf >> playerCode;
	User->set_playerCode(playerCode);
	check_maxResTime(User->set_responseTime(GetTickCount64()));
	User->recentResponseTime = GetTickCount64();
	User->set_sendTime(0);
	User->set_dealyTime(GetTickCount64());
	InterlockedDecrement(&notRecvCount);
	User->set_loginFlag(true);
	InterlockedIncrement(&User->waitQ);
	InterlockedIncrement(&loginClientCount);
	proc_dataReq(User);
	waitQ->push(User);
}

void virtualClient::Client::recv_Chatting(unsigned __int64 _Index, Sbuf *_buf)
{
	Player *User = get_userPtr(_Index);
	if (!User)
	{
		clientShutdown(_Index);
		CCrashDump::Crash();
		return;
	}

	unsigned __int64 playerCode;
	std::wstring Str;

	*_buf >> playerCode;

	if (playerCode != User->get_playerCode()) return;

	int dataSize;
	*_buf >> dataSize;

	WCHAR chatData[50];
	memset(chatData, 0, 100);
	_buf->pop((char*)&chatData, dataSize);
	
	User->recentResponseTime = GetTickCount64();

	if(0 !=wcscmp(chatList[User->sendChatNum].c_str(), chatData))
		CCrashDump::Crash();

	check_maxResTime(User->set_responseTime(GetTickCount64()));
	User->set_sendTime(0);
	User->set_dealyTime(GetTickCount64());
	User->sendChatNum = 0;
	InterlockedDecrement(&notRecvCount);
	InterlockedIncrement(&User->waitQ);
	waitQ->push(User);
}

// Control function

void virtualClient::Client::Pause(void)
{
	pauseFlag = true;
}

void virtualClient::Client::Restart(void)
{
	pauseFlag = false;
}

void virtualClient::Client::Terminate(void)
{
	terminateFlag = true;
	delete this;
}

void virtualClient::Client:: set_avrResTime()
{
	ULONGLONG sumVal = 0;
	int					sumCount = 0;
	for (int i = 0; i < maxClient; ++i)
	{
		if (playerArray[i].get_Type() == dataType::Login || playerArray[i].get_Type() == dataType::Content)
		{
			sumVal += playerArray[i].get_avrTime();
			sumCount++;
		}
	}
	if (sumCount == 0)
		avrResponseTime = sumVal;
	else
		avrResponseTime = sumVal / sumCount;
}

void virtualClient::Client::OnClientJoin(unsigned __int64 _Index)
{
	Player *User = get_userPtr(_Index);
	if (!User) CCrashDump::Crash();

	if(0 > InterlockedDecrement(&disconnectCount))
		InterlockedIncrement(&disconnectCount);

	User->set_Index(_Index);
	User->set_Type(dataType::Login);
	User->set_dealyTime(GetTickCount64());

	InterlockedIncrement(&User->waitQ);
	waitQ->push(User);

}

void virtualClient::Client::OnClientLeave(unsigned __int64 _Index)
{
	Player *user = get_userPtr(_Index);
	if (!user)  CCrashDump::Crash();

	check_disconnect(user);

	if (1 == user->set_loginFlag(0))
		InterlockedDecrement(&loginClientCount);

	user->initialize();
	InterlockedIncrement(&(connectDummyPtr->waitQ));
	if (terminateFlag)
		return;
	waitQ->push(connectDummyPtr);
}

bool virtualClient::Client::OnConnectionRequest(char *_ip, unsigned short _port)
{
	return true;
}

void virtualClient::Client::OnRecv(unsigned __int64 _Index, Sbuf *_buf)
{
	short Type;
	*_buf >> Type;

	switch (Type)
	{
	case chatProtocol::s2c_Login_Res:
		recv_Login(_Index, _buf);
		break;
	case chatProtocol::s2c_Chatting:
		recv_Chatting(_Index, _buf);
		break;
	case chatProtocol::s2c_playerData_Res:
		break;
	case chatProtocol::s2c_createPlayer:
		break;
	case chatProtocol::s2c_deletePlayer:
		break;
	case chatProtocol::s2c_playerMove:
		break;
	case chatProtocol::s2c_playerCHChange:
		break;
	default:
		CCrashDump::Crash();
		break;
	}
}

void virtualClient::Client::OnError(int _errorCode, WCHAR *_string)
{
	printf("Error code : %d] %s\n", _errorCode, _string);
}
