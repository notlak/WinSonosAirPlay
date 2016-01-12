#pragma once
#include "NetworkServer.h"

class HttpServer : public NetworkServer<NetworkServerConnection>
{
public:
	HttpServer();
	virtual ~HttpServer();

	virtual void OnRequest(NetworkServerConnection& connection, NetworkRequest& request);
};

