#pragma once
#include "NetworkServer.h"

class HttpServer : public NetworkServer
{
public:
	HttpServer();
	virtual ~HttpServer();

	virtual void OnReceive(NetworkServerConnection& connection, char* buff, int len);
};

