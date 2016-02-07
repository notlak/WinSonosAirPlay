#pragma once
#include "NetworkServer.h"

#include <map>
#include <list>
#include <mutex>

class StreamingServer;

struct MetaData
{
	MetaData() {}
	MetaData(const MetaData& m) { *this = m; }
	MetaData& operator=(const MetaData& m) {album = m.album; artist = m.artist; title = m.title; return *this;}

	std::string album;
	std::string artist;
	std::string title;
};

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

	void TransmitStreamData(unsigned char* pData, int len);
	void TransmitMetaData();
	void MetaDataUpdate(const MetaData& meta);

	StreamingServer* _pStreamingServer;
	int _streamIdRequested;
	bool _sendAudio; // mustn't send audio data until we've responded to the request
	static const int MetaDataInterval = 8192;
	int _metaCount; // count of bytes before next metadata block
	MetaData _metaData;
	bool _newMetaData;
	std::mutex _metaMutex;
};

class StreamBuffer
{
public:
	StreamBuffer(unsigned char* pData, int len) : _len(len)
	{
		_pData = new unsigned char[len];
		memcpy(_pData, pData, len);
		_time = GetTickCount();
	}

	~StreamBuffer() { _len = 0;  delete _pData; }
	unsigned char* _pData;
	int _len;
	DWORD _time;
};

class StreamCache
{
public:
	StreamCache() : _lastPrune(0) {}
	~StreamCache()
	{
		while (_bufferList.size() > 0)
		{
			delete _bufferList.front();
			_bufferList.pop_front();
		}
	}

	void AddData(unsigned char* pData, int len)
	{
		_bufferList.push_back(new StreamBuffer(pData,len));
	}

	std::list<StreamBuffer*> _bufferList;
	unsigned int _lastPrune;
};

class StreamingServer : public NetworkServer<StreamingServerConnection>
{
public:
	StreamingServer();
	virtual ~StreamingServer();

	static StreamingServer* GetStreamingServer();
	static void Delete();
	static int GetStreamId();
	static int GetPort() { return GetStreamingServer()->_port; }

	void CreateStream(int streamId);

	// used by audio producer to push data to the stream
	void AddStreamData(int streamId, unsigned char* pData, int len);

	void CacheData(int streamId, unsigned char* pData, int len);
	void TransmitCache();

	void MetaDataUpdate(int streamId, const MetaData& meta);

	virtual void OnRequest(NetworkServerConnection& connection, NetworkRequest& request);

	// called by Stream to actually transmit the data
	//void TransmitStreamData(int streamId, unsigned char* pData, int len);

	std::map<int, StreamingServerStream*> _streamMap;
	std::mutex _streamMapMutex;

	std::map<int, MetaData> _metaCacheMap; // often get metadata before stream is set up so store here then forward
	std::mutex _metaCacheMutex;

	std::map<int, StreamCache*> _streamCacheMap;
	std::mutex _streamCacheMutex;

	static StreamingServer* InstancePtr;
	static int NextStreamId;

};

