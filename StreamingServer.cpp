#include "stdafx.h"
#include "StreamingServer.h"

#include <sstream>

///////////////////////////////////////////////////////////////////////////////
// StreamingServer implementation
///////////////////////////////////////////////////////////////////////////////

StreamingServer::StreamingServer()
{
}

StreamingServer::~StreamingServer()
{
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

			serialIdStr = request.path.substr(startPos + 1, endPos - startPos);

			streamId = atoi(serialIdStr.c_str());

			if (streamId > 0)
			{
				((StreamingServerConnection*)&connection)->_streamIdRequested = streamId;

				NetworkResponse resp("HTTP/1.0", 200, "OK");
				resp.AddHeaderField("Content-Type", "audio/x-mpegurl");

				std::ostringstream body;

				body << "http://" << connection.GetIpAddress() << ":" << _port << "/listen";

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

			serialIdStr = request.path.substr(startPos + 1, endPos - startPos);

			streamId = atoi(serialIdStr.c_str());

			if (streamId > 0)
			{
				((StreamingServerConnection*)&connection)->_streamIdRequested = streamId;

				NetworkResponse resp("HTTP/1.0", 200, "OK");
				resp.AddHeaderField("Content-Type", "audio/mpeg");
				resp.AddHeaderField("Connection", "close");

				if (request.headerFieldMap["ICY-METADATA"] == "1")
					resp.AddHeaderField("icy-metaint", "8192");

				connection.SendResponse(resp);

				//... then start sending data...
			}
		}
	}
}

void StreamingServer::AddStreamData(int streamId, unsigned char* pData, int len)
{
	if (streamId <= 0) return;

	
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

StreamingServerStream::StreamingServerStream(int id)
: _id(id), _buffSize(65536), _pBuff(nullptr), _nBytesInBuff(0)
{
	_pBuff = new unsigned char[_buffSize];
}


StreamingServerStream::~StreamingServerStream()
{
	delete[] _pBuff;
}

void StreamingServerStream::AddData(unsigned char* pData, int len)
{
	if ((len + _nBytesInBuff) > _buffSize)
	{

	}
}
