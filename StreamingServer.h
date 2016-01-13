#pragma once
#include "NetworkServer.h"

#include <map>
#include <mutex>

class StreamingServerStream
{
public:
	StreamingServerStream(int id);
	~StreamingServerStream();

	void AddData(unsigned char* pData, int len);

	int _id;
	unsigned char* _pBuff;
	int _buffSize;
	int _nBytesInBuff;

	std::mutex _buffMutex;
};

class StreamingServerConnection : public NetworkServerConnection
{
public:
	StreamingServerConnection(NetworkServerInterface* pServerInterface, SOCKET socket, SOCKADDR_IN& remoteAddr);
	~StreamingServerConnection();

	int _streamIdRequested;
};

class StreamingServer : public NetworkServer<StreamingServerConnection>
{
public:
	StreamingServer();
	virtual ~StreamingServer();

	void AddStreamData(int stream, unsigned char* pData, int len);

	virtual void OnRequest(NetworkServerConnection& connection, NetworkRequest& request);

	std::map<int, StreamingServerStream> _streamMap;
};

