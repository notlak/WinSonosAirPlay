#include "stdafx.h"
#include "StreamingServer.h"

#include <sstream>
#include <mutex>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

///////////////////////////////////////////////////////////////////////////////
// StreamingServer implementation - this is a Singleton
///////////////////////////////////////////////////////////////////////////////

StreamingServer* StreamingServer::InstancePtr = nullptr;
int StreamingServer::NextStreamId = 0;

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
	return NextStreamId++;
}

StreamingServer::StreamingServer()
{
}

StreamingServer::~StreamingServer()
{
	// remove all the streams

	std::lock_guard<std::mutex> lock(_streamMapMutex);

	for (auto it = _streamMap.begin(); it != _streamMap.end(); ++it)
		delete it->second;

	_streamMap.clear();
}

void StreamingServer::CreateStream(int streamId)
{
	// only create the stream if it doesn't already exist

	std::lock_guard<std::mutex> lock(_streamMapMutex);

	if (_streamMap.find(streamId) == _streamMap.end())
	{
		_streamMap[streamId] = new StreamingServerStream(streamId, this);
		TRACE("Created stream %d\n", streamId);
	}
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

				NetworkResponse resp("HTTP/1.1", 200, "OK");
				resp.AddHeaderField("Content-Type", "audio/x-mpegurl");

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

				CreateStream(streamId);

				((StreamingServerConnection*)&connection)->_streamIdRequested = streamId;

				NetworkResponse resp("HTTP/1.1", 200, "OK");
				resp.AddHeaderField("Content-Type", "audio/mpeg");
				//resp.AddHeaderField("Connection", "close");

				if (request.headerFieldMap["ICY-METADATA"] == "1")
					//resp.AddHeaderField("icy-metaint", "8192");
					resp.AddHeaderField("icy-metaint", "0");

				connection.SendResponse(resp);

				//... then start sending data...
			}
		}
	}
}

// called by audio provider
void StreamingServer::AddStreamData(int streamId, unsigned char* pData, int len)
{

	std::lock_guard<std::mutex> lock(_streamMapMutex);

	// keep the latency short, don't add stream data if the stream doesn't exist
	if ((_streamMap.find(streamId)) == _streamMap.end())
		return;

	_streamMap[streamId]->AddData(pData, len);

}

// called from a StreamingServerStream instance
void StreamingServer::TransmitStreamData(int streamId, unsigned char* pData, int len)
{
	std::lock_guard<std::mutex> lock (_connectionListMutex);

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
	_streamIdRequested(-1)
{

}

StreamingServerConnection::~StreamingServerConnection()
{

}


///////////////////////////////////////////////////////////////////////////////
// StreamingServerStream implementation
///////////////////////////////////////////////////////////////////////////////

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
