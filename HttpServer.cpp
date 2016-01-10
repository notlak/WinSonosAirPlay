#include "stdafx.h"
#include "HttpServer.h"


HttpServer::HttpServer()
{
}


HttpServer::~HttpServer()
{
}

void HttpServer::OnReceive(NetworkServerConnection& connection, char* buff, int len)
{
	TRACE("%s", buff);
}

// example request:
//GET / HTTP/1.1
//Host: localhost
//User-Agent: Mozilla/5.0 (Windows NT 10.0; WOW64; rv:43.0) Gecko/20100101 Firefox/43.0
//Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8
//Accept-Language: en-GB,en;q=0.5
//Accept-Encoding: gzip, deflate
//Connection: keep-alive