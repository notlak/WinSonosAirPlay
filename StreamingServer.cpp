#include "stdafx.h"
#include "StreamingServer.h"

#include <sstream>
#include <mutex>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

#define ONE_STREAM

///////////////////////////////////////////////////////////////////////////////
// StreamingServer implementation - this is a Singleton
///////////////////////////////////////////////////////////////////////////////

StreamingServer* StreamingServer::InstancePtr = nullptr;
int StreamingServer::NextStreamId = 1; // start at 1

StreamingServer* StreamingServer::GetStreamingServer()
{
	if (InstancePtr == nullptr)
		InstancePtr = new StreamingServer();

	return InstancePtr;
}

void StreamingServer::Delete()
{
	delete InstancePtr;
}

int StreamingServer::GetStreamId()
{
#ifdef ONE_STREAM
	return 1;
#else
	return NextStreamId++;
#endif
}

StreamingServer::StreamingServer()
{
}

StreamingServer::~StreamingServer()
{
	// remove all the streams
#if 0
	std::lock_guard<std::mutex> lock(_streamMapMutex);

	for (auto it = _streamMap.begin(); it != _streamMap.end(); ++it)
		delete it->second;

	_streamMap.clear();
#endif
}

void StreamingServer::CreateStream(int streamId)
{
#if 0
	// only create the stream if it doesn't already exist

	std::lock_guard<std::mutex> lock(_streamMapMutex);

	if (_streamMap.find(streamId) == _streamMap.end())
	{
		_streamMap[streamId] = new StreamingServerStream(streamId, this);
		TRACE("Created stream %d\n", streamId);
	}
#endif
}

void StreamingServer::OnRequest(NetworkServerConnection& connection, NetworkRequest& request)
{
	// assume path is of the form: /<stream id>/ or /<stream id>/listen.m3u

	int startPos, endPos;
	int streamId = -1;
	std::string serialIdStr("");

	endPos = request.path.find("/listen.m3u");

	if (endPos != std::string::npos)
	{
		startPos = request.path.rfind('/', endPos-1);

		if (startPos != std::string::npos)
		{
			startPos++; // don't want '/'

			serialIdStr = request.path.substr(startPos, endPos - startPos);

			streamId = atoi(serialIdStr.c_str());

			if (streamId > 0)
			{
				((StreamingServerConnection*)&connection)->_streamIdRequested = streamId;

				NetworkResponse resp("HTTP/1.0", 200, "OK");
				resp.AddHeaderField("Content-Type", "audio/x-mpegurl");
				resp.AddHeaderField("Connection", "close");

				std::ostringstream body;

				body << "http://" << connection.GetIpAddress() << ":" << _port << "/" << streamId << "/listen";

				resp.AddContent(body.str().c_str(), body.str().length());

				connection.SendResponse(resp);
			}
		}
	}
	else if ((endPos = request.path.find("/listen")) != std::string::npos)
	{
		startPos = request.path.rfind('/', endPos - 1);

		if (startPos != std::string::npos)
		{
			startPos++; // don't want '/'

			serialIdStr = request.path.substr(startPos, endPos - startPos);

			streamId = atoi(serialIdStr.c_str());

			if (streamId > 0)
			{
				// create a corresponding stream object in the server if one doesn't exist

				//CreateStream(streamId);

				((StreamingServerConnection*)&connection)->_streamIdRequested = streamId;

				NetworkResponse resp("HTTP/1.0", 200, "OK");
				resp.AddHeaderField("Content-Type", "audio/mpeg");
				resp.AddHeaderField("Connection", "close");

				if (request.headerFieldMap["ICY-METADATA"] == "1")
					resp.AddHeaderField("icy-metaint", "8192");
					//resp.AddHeaderField("icy-metaint", "0");

				connection.SendResponse(resp);

				//... then start sending data...
			}
		}
	}
}

// called by audio provider
void StreamingServer::AddStreamData(int streamId, unsigned char* pData, int len)
{
	/* Don't use the Stream object, instead handle in the StreamingServerConnection
	std::lock_guard<std::mutex> lock(_streamMapMutex);

	// keep the latency short, don't add stream data if the stream doesn't exist
	if ((_streamMap.find(streamId)) == _streamMap.end())
		return;

	_streamMap[streamId]->AddData(pData, len);
	*/

	std::lock_guard<std::mutex> lock(_connectionListMutex);

	for (auto it = _connectionList.begin(); it != _connectionList.end(); ++it)
	{
		StreamingServerConnection* pConn = dynamic_cast<StreamingServerConnection*>(*it);
		if (pConn->_streamIdRequested == streamId)
		{
			pConn->TransmitStreamData(pData, len);
		}
	}
}

// called from a StreamingServerStream instance
void StreamingServer::TransmitStreamData(int streamId, unsigned char* pData, int len)
{
	std::lock_guard<std::mutex> lock(_connectionListMutex);

	// send data out on each connection listening to this stream
	for (std::list<NetworkServerConnection*>::iterator it = _connectionList.begin(); it != _connectionList.end(); ++it)
	{
		StreamingServerConnection* pConn = dynamic_cast<StreamingServerConnection*>(*it);
		pConn->Transmit((char*)pData, len);
	}

}


///////////////////////////////////////////////////////////////////////////////
// StreamingServerConnection implementation
///////////////////////////////////////////////////////////////////////////////

StreamingServerConnection::StreamingServerConnection(NetworkServerInterface* pServerInterface, SOCKET socket, SOCKADDR_IN& remoteAddr)
	: NetworkServerConnection(pServerInterface, socket, remoteAddr),
	_streamIdRequested(-1), _metaCount(0)
{

}

StreamingServerConnection::~StreamingServerConnection()
{

}

void StreamingServerConnection::TransmitMetaData(const char* title, const char* url)
{
	std::ostringstream meta;

	meta << "StreamTitle='" << title << "';StreamURL='" << url << "';";

	int nBytes = meta.str().length();

	if (nBytes % 16)
		nBytes += 16 - (nBytes % 16);

	char* pBuff = new char[nBytes + 1];
	memset(pBuff, 0, nBytes+1);
	pBuff[0] = nBytes / 16;

	memcpy(pBuff+1, meta.str().c_str(), nBytes);

	Transmit(pBuff, nBytes+1);

	delete[] pBuff;
}

void StreamingServerConnection::TransmitStreamData(unsigned char* pData, int len)
{
	// just in case we ever get massive packets, or have a small metadata interval
	while (len + _metaCount > MetaDataInterval)
	{
		int preMetaBytes = MetaDataInterval - _metaCount;

		Transmit((char*)pData, preMetaBytes);

		// Transmit metadata - none for now
		//char c = 0;
		//Transmit(&c, 1);

		TransmitMetaData("AirPlay - Test", "http://localhost/1/listen.m3u");

		_metaCount = 0;
		pData += preMetaBytes;
		len -= preMetaBytes;
	}

	if (len > 0)
	{
		Transmit((char*)pData, len);
		_metaCount += len;
	}
}


///////////////////////////////////////////////////////////////////////////////
// StreamingServerStream implementation
///////////////////////////////////////////////////////////////////////////////
#if 0 // unused now
StreamingServerStream::StreamingServerStream(int id, StreamingServer* pServer)
	: _id(id), _buffSize(65536), _pBuff(nullptr), _nBytesInBuff(0),
		_pServer(pServer), _metaCount(0)
{
	_pBuff = new unsigned char[_buffSize];
}


StreamingServerStream::~StreamingServerStream()
{
	delete[] _pBuff;
}

void StreamingServerStream::AddData(unsigned char* pData, int len)
{
	// increase the buffer size if it's too small

	while ((len + _nBytesInBuff) > _buffSize)
	{
		unsigned char* pOldBuff = _pBuff;

		_buffSize <<= 1; // double the size

		TRACE("Increasing stream buffer size: %d\n", _buffSize);

		_pBuff = new unsigned char[_buffSize];

		memcpy(_pBuff, pOldBuff, _nBytesInBuff);

		delete[] pOldBuff;
	}

	// add data to the buffer

	memcpy(_pBuff + _nBytesInBuff, pData, len);
	_nBytesInBuff += len;

	// transmit the data

	while (_nBytesInBuff > 0)
	{
		int nBytesToSend = 0;
		bool sendMeta = false;

		if (_nBytesInBuff + _metaCount < MetaInterval)
		{
			nBytesToSend = _nBytesInBuff; // send all
		}
		else
		{
			nBytesToSend = MetaInterval - _metaCount;
			sendMeta = true;
		}

		_pServer->TransmitStreamData(_id, _pBuff, nBytesToSend);

		// for now send no metadata
		if (sendMeta)
		{
			unsigned char zero = 0;
			//_pServer->TransmitStreamData(_id, &zero, 1);
		}

		// only shift the data if we've still got bytes to send
		if (nBytesToSend < _nBytesInBuff)
			memmove(_pBuff, _pBuff + nBytesToSend, _nBytesInBuff - nBytesToSend);

		_nBytesInBuff -= nBytesToSend;
		_metaCount = (_metaCount + nBytesToSend) % MetaInterval;
	}

}
#endif