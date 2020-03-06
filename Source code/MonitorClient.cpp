#include "stdafx.h"

monitorClient::monitorClient(const char *_configFileName)
{
	const char *configData = loadFile(_configFileName);

	loadConfig(configData);

	Getter = new resourceGetter();
	serverConnectionFlag = false;

	Start(IP, Port, workerThreadCount, nagleOption);
}

monitorClient::~monitorClient()
{
	Stop();
	delete Getter;
}

void monitorClient::loadConfig(const char *_configData)
{
	rapidjson::Document Doc;
	Doc.Parse(_configData);

	rapidjson::Value &Val = Doc["Monitor"];

	strcpy_s(IP, 16, Val["Server_ip"].GetString());
	Port = Val["Server_port"].GetUint();
	workerThreadCount = Val["Workerthread_count"].GetUint();
	nagleOption = Val["Nagle_option"].GetBool();

	rapidjson::Value &dataList = Doc["MonitorDataList"];
	assert(dataList.IsArry());
	dataSize = dataList.Size();
	std::string Name;
	for (rapidjson::SizeType i = 0; i < dataSize; i++)
	{
		Name = dataList[i].GetString();
		dataName.push_back(Name);
	}
}

void monitorClient::sendLoginMsg(void)
{
	Sbuf *buf = makeMsgLogin();
	SendPacket(buf);
	buf->Free();
}

Sbuf* monitorClient::makeMsgLogin()
{
	//	short					protocolType
	// short					clientType
	//	unsigned char	clientNameSize
	// char*					clientName
	//	unsigned char	dataSize
	// monitorData		Data[]

	Sbuf *buf = Sbuf::lanAlloc();
	
	*buf << (short)monitorProtocol::requestClientLogin;
	*buf << (short)monitorClientType::Client;
	std::string clientName = "Chatting virtual client";
	*buf << (unsigned char)clientName.size();
	buf->push(clientName.data(), clientName.size());
	*buf << (unsigned char)dataSize;

	for (auto iter : dataName)
	{
		*buf << (unsigned int)iter.size();
		buf->push(iter.c_str(), iter.size());
	}

	return buf;
}

Sbuf* monitorClient::makeMsgData(const std::vector<ULONGLONG> _Data)
{
	Sbuf *buf = Sbuf::lanAlloc();
	*buf << (short)monitorProtocol::requestSetMonitorData;
	*buf << authorizedClientCode;
	*buf << dataSize;
	for (auto iter : _Data)
		*buf << iter;
	return buf;
}

void monitorClient::responseLoginRequest(Sbuf *_buf)
{
	unsigned char	loginResult, authorizedDataSize;
	*_buf >> loginResult;
	*_buf >> authorizedClientCode;
	*_buf >> authorizedDataSize;

	if (loginResult == 0)
		return;
	if (authorizedDataSize != dataSize)
		return;

	serverConnectionFlag = true;
}

void monitorClient::onClientJoin(void) 
{
	sendLoginMsg();
}

void monitorClient::onClientLeave(void) 
{
	serverConnectionFlag = false;
}

void monitorClient::onRecv(Sbuf *_buf)
{
	short Type;
	*_buf >> Type;
	if (Type == (short)monitorProtocol::responseClientLogin)
		responseLoginRequest(_buf);
	else
		CCrashDump::Crash();
}

void monitorClient::onError(int _errorCode, WCHAR *_String)
{

}

void monitorClient::onTPS()
{
	if (!serverConnectionFlag)
		return;

	Getter->calProcessResourceValue();
}

void monitorClient::sendData(int arg, ...)
{
	if (!serverConnectionFlag) return;

	std::vector<ULONGLONG> Data;

	Data.push_back(Getter->getProcessCPU());
	Data.push_back(Getter->getProcessMem());
	va_list ap;
	va_start(ap, arg);
	for (int i = 0; i < arg; i++)
		Data.push_back(va_arg(ap, ULONGLONG));
	va_end(ap);

	Sbuf *buf = makeMsgData(Data);
	SendPacket(buf);
	buf->Free();

	Data.clear();
}