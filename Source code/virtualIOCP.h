#pragma pack(push,1)
struct netHeader
{
	BYTE code;					// 검증 코드
	WORD len;					// 패킷의 길이 (헤더 제외)
	BYTE randCode;		// XOR code
	BYTE checkSum;		// checkSum
};
#pragma pack(pop)

struct Session
{
	Session()
	{
		Sock = INVALID_SOCKET;
		Index = 0;

		recvFlag = 0;
		sendFlag = 0;
		usingFlag = 0;
		disconnectFlag = 0;
		sendCount = 0;
		sendDisconnectFlag = 0;
		sendPQCS = 0;

	}
	SOCKET  Sock;
	unsigned __int64 Index;

	// IOCP 작업 관련 변수들

	volatile LONG recvFlag;				// Recv 중인지 체크. 
	volatile LONG sendFlag;				// send 중인 지 체크.
	volatile LONG usingFlag;				// 어떤 스레드에서 세션을 사용하고 있는지 체크.
	volatile LONG disconnectFlag;		// disconnect 체크

	LONG sendCount;				// send한 직렬화 버퍼의 수 
	volatile LONG sendDisconnectFlag;	// 보내고 끊기 체크
	volatile LONG sendPQCS;				// PQCS를 이용해서 SEND 함수 호출시 호출여부 체크

	winBuffer recvQ;
	boost::lockfree::queue<Sbuf*> sendQ;
	boost::lockfree::queue<Sbuf*> completeSendQ;
	OVERLAPPED recvOver, sendOver;
};


class virtualIOCP
{
private:
	Session *sessionArr;		// session 관리 배열. 생성자에서 동적할당

	// Counter
	LONG clientCounter;		// 현재 Connect한 Client 수
	int	limitCount;		// 초당 Connect 제한
	int maxClient;					// Max client count.
	int threadCount;				// worker 수
	LONG connectFailCount;

	// Handle
	HANDLE hcp;					// IOCP HANDLE
	HANDLE *threadArr;
	HANDLE connectThreadHandle;
	
	// Connect 
	char	IP[16];
	unsigned short	Port;
	SOCKADDR_IN addr;
	LINGER lingerOption;
	bool nagleOption;

	// Encryption Key
	BYTE Code, Key1, Key2;

private:
	LONG connectTPS;				// 초당 Connect 횟수 
	LONG sendTPS;					// 초당 Send 횟수
	LONG recvTPS;					// 초당 Recv 횟수

public:
	boost::lockfree::stack<unsigned __int64> *connectStack;
	boost::lockfree::stack<unsigned __int64> *indexStack;

	// Test
	LONG connectStackCount;

private:
	static unsigned __stdcall connectThread(LPVOID _data);			// ACCEPT 작업만 하는 스레드
	static unsigned __stdcall workerThread(LPVOID _data);		// IOCP 스레드

	Session* insertSession(SOCKET _sock, unsigned __int64 _Index);

	void loadConfig(const char *_configData);

private:
	void recvPost(Session *_ss);
	void sendPost(Session *_ss);
	void completeRecv(LONG _trnas, Session *_ss);
	void completeSend(LONG _trnas, Session *_ss);

	void clientShutdown(Session *_ss);
	void disconnect(Session *_ss);
	void disconnect(SOCKET _sock);

	Session* acquirLock(unsigned __int64 _index);
	void releaseLock(Session *_ss);

protected:
	bool quit;
	bool ctQuit;
	LONG connectTotal;
	LONG pconnectTPS;
	LONG psendTPS;
	LONG precvTPS;

protected:
	LONG getConnectTotal(void);
	LONG getConnectFail(void);
	LONG getConnetTPS(void);
	LONG getSendTPS(void);
	LONG getRecvTPS(void);
	void setTPS(void);

	bool callConnect(void);

protected:
	bool		Start(const char *_configData);
	bool		Stop(void);					
	LONG	GetClientCount(void);
	void		SendPacket(unsigned __int64 _index, Sbuf *_buf, bool _type = false);

	void clientShutdown(unsigned __int64 _Index);		// 자식 클래스에서 호출하는 함수 

	virtual void OnClientJoin(unsigned __int64 _Index) = 0;	// accept -> 접속처리 완료 후 호출
	virtual void OnClientLeave(unsigned __int64 _Index) = 0;		// disconnect 후 호출
	virtual bool OnConnectionRequest(char *_ip, unsigned short _port) = 0; // accept 후 [false : 클라이언트 거부 / true : 접속 허용]
	virtual void OnRecv(unsigned __int64 _Index, Sbuf *_buf) = 0;		// 수신 완료 후

	virtual void OnError(int _errorCode, WCHAR *_string) = 0;		// 오류메세지 전송
};

