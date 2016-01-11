#include "stdafx.h"
#include "HttpServer.h"

#include <sstream>


HttpServer::HttpServer()
{
}


HttpServer::~HttpServer()
{
}

// example response:
/*
HTTP/1.0 200 OK
Date: Fri, 31 Dec 1999 23:59:59 GMT
Connection: close
Content-Type: text/html
Content-Length: 41
<html><body>HttpServer test</body></html>
*/

void HttpServer::OnRequest(NetworkServerConnection& connection, NetworkRequest& request)
{
	TRACE("type:[%s] path:[%s] protocol:[%s]\n", 
		request.type.c_str(), request.path.c_str(), request.protocol.c_str());

	// send response

	std::ostringstream os;

	std::string body = "<html><body>HttpServer test</body></html>";

	os << "HTTP/1.1 200 OK\r\n"
		"Date: Fri, 31 Dec 1999 23:59:59 GMT\r\n"
		"Connection: close\r\n"
		"Content-Type: text/html\r\n"
		"Content-Length: " << body.length() << "\r\n\r\n" << body;

	connection.Transmit(os.str().c_str(), os.str().length());
}

