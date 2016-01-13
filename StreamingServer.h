#pragma once
#include "NetworkServer.h"

#include <map>
#include <mutex>

class StreamingServer;

class StreamingServerStream
{
public:
	StreamingServerStream(int id, StreamingServer* pServer);
	~StreamingServerStream();

	void AddData(unsigned char* pData, int len);

	static const int MetaInterval = 8192; // metadata interval in bytes

	int _id;
	unsigned char* _pBuff;
	int _buffSize;
	int _nBytesInBuff;
	int _metaCount; // counts 0..MetaInterval then reset

	std::mutex _buffMutex;

	StreamingServer* _pServer;
};

class StreamingServerConnection : public NetworkServerConnection
{
public:
	StreamingServerConnection(NetworkServerInterface* pServerInterface, SOCKET socket, SOCKADDR_IN& remoteAddr);
	~StreamingServerConnection();

	int _streamIdRequested;
	StreamingServer* _pStreamingServer;
};

class StreamingServer : public NetworkServer<StreamingServerConnection>
{
public:
	StreamingServer();
	virtual ~StreamingServer();

	static StreamingServer* GetStreamingServer();

	void CreateStream(int streamId);

	// used by audio producer to push data to the stream
	void AddStreamData(int stream, unsigned char* pData, int len);

	virtual void OnRequest(NetworkServerConnection& connection, NetworkRequest& request);

	// called by Stream to actually transmit the data
	void TransmitStreamData(int streamId, unsigned char* pData, int len);

	std::map<int, StreamingServerStream*> _streamMap;

	static StreamingServer* InstancePtr;

};

