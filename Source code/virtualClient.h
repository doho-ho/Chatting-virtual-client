#pragma once

namespace virtualClient
{

	enum dataType
	{
		None = 0, Wait, Login, Connect, Disconnect, Content,
	};

	class Player
	{
	private:
		unsigned __int64 Index = 0;
		unsigned __int64 playerCode = 0;

		dataType		nowType = dataType::None;
		ULONGLONG	dealyTime = 0;
		ULONGLONG	sendTime = 0;

		volatile LONG	loginFlag = 0;

		ULONGLONG	*resTimeArray;
		int					resCountSize = 0;
		int					resCounter = -1;
	public:
		LONG	waitQ = 0;
		unsigned int sendChatNum = 0;

		//Debug 
		ULONGLONG recentResponseTime;

	public:
		Player() { }
		~Player() { delete[] resTimeArray; }

		void initialize();

		void set_resCountSize(int _resCountSize);
		void set_Index(unsigned __int64 _Index) { Index = _Index; }
		void set_playerCode(unsigned __int64 _playerCode) { playerCode = _playerCode; }
		void set_sendTime(ULONGLONG _sendTime) { sendTime = _sendTime; }
		void set_dealyTime(ULONGLONG _dealyTime) { dealyTime = _dealyTime; }
		void set_Type(dataType _Type) { nowType = _Type; }

		ULONGLONG set_responseTime(ULONGLONG _nowTime);

		volatile LONG set_loginFlag(LONG _Flag) { return InterlockedExchange(&loginFlag, _Flag); }

		unsigned __int64	get_Index(void) { return Index; }
		unsigned __int64	get_playerCode(void) { return playerCode; }

		ULONGLONG		get_avrTime(void);

		ULONGLONG		get_sendTime(void) { return sendTime; }
		ULONGLONG		get_dealyTime(void) { return dealyTime; }
		volatile LONG		get_loginFlag(void) { return loginFlag; }

		dataType	get_Type(void) { return nowType; }
	};

	class Client : protected virtualIOCP
	{
	private:
		monitorClient *Monitor;

		Player *playerArray;

		// Config data
		std::string chatDataName;
		std::string monitorConfigName;
		std::wstring *chatList;
		unsigned int chatSize;

		unsigned int	contentThreadCount;
		unsigned int	contentThreadLoopCount;
		unsigned int	maxClient;
		unsigned int	connectProbability;
		unsigned int	disconnectProbability;
		unsigned int	chatProbability;
		unsigned int	contentProbability;

		unsigned int	actionDealy;
		unsigned int	loginDealy;

		unsigned int	resTimeCountSize;

		// Control variable
		bool pauseFlag;
		bool terminateFlag;

		// Queue
		boost::lockfree::queue<Player*> *waitQ;
		//lockFreeQueue<Player*> *waitQ;	

		Player *connectDummyPtr;			// Q에 Connect 해야함을 알리는 더미 포인터.

		// Count data
		LONG disconnectCount;		// 클라이언트가 연결을 끊은 클라이언트 수 
		LONG shutdownCount;		// 서버가 연결을 끊은 클라이언트 수
		LONG notRecvCount;			// 서버로부터 응답을 받지 못한 클라이언트 수
		LONG loginClientCount;		// 로그인 요청 통과한 클라이언트 수

		LONG contentThreadTPS;
		LONG contentFrame;

		volatile ULONGLONG maxResponseTime;
		ULONGLONG avrResponseTime;

	private:
		// Data load function
		void loadConfigData(const char *_configData);
		void loadChatData(const char *_chatData);

		// Call logic process function
		void			proc_connect();
		void			proc_login(Player *_user);
		void			proc_dataReq(Player *_User);
		void			proc_disconnect(Player *_user);
		void			proc_contents(Player *_user);

		dataType	proc_probability(Player *_user);
		int				proc_rand();
		void			check_disconnect(Player *_user);
		void			check_maxResTime(ULONGLONG _resTime);

		Player*		get_userPtr(unsigned __int64 _Index);

		// Call packet function
		Sbuf*		packet_Login();
		Sbuf*		packet_Chat(unsigned __int64 _playerCode, unsigned int _chatNum);

		// Call recv process function
		void		recv_Login(unsigned __int64 _Index, Sbuf *_buf);
		void		recv_Chatting(unsigned __int64 _Index, Sbuf *_buf);

		// Thread
		static unsigned __stdcall monitorThread(LPVOID _data);
		static unsigned __stdcall contentThread(LPVOID _data);
	public:
		Client(const char *_configData);
		~Client(void);

		// Control functions
		void Pause();
		void Restart();
		void Terminate();
		void set_avrResTime();

	protected:
		// Virtual function override
		virtual void OnClientJoin(unsigned __int64 _Index);
		virtual void OnClientLeave(unsigned __int64 _Index);
		virtual bool OnConnectionRequest(char *_ip, unsigned short _port);
		virtual void OnRecv(unsigned __int64 _Index, Sbuf *_buf);

		virtual void OnError(int _errorCode, WCHAR *_string);
	};

}