#pragma once

struct monitorData
{
	unsigned int	dataNameSize;
	std::string		dataName;
};

class monitorClient : LanClient
{
private:
	char IP[16];
	unsigned short Port;
	unsigned short workerThreadCount;
	bool	nagleOption;

	resourceGetter *Getter;

	unsigned char authorizedClientCode;
	unsigned char dataSize;

	std::vector<std::string> dataName;
public:
	bool	serverConnectionFlag;
private:
	void loadConfig(const char *_configData);

	void sendLoginMsg(void);
	void responseLoginRequest(Sbuf *_buf);

	Sbuf*		makeMsgLogin();
	Sbuf*		makeMsgData(const std::vector<ULONGLONG> _Data);
public:
	monitorClient(const char *_configFileName);
	~monitorClient();

	void sendData(int arg, ...);

	virtual void onClientJoin(void);
	virtual void onClientLeave(void);
	virtual void onRecv(Sbuf *_buf);
	virtual void onError(int _errorCode, WCHAR *_string);
	virtual void onTPS();
};