#include "stdafx.h"
#include "HttpControlServer.h"
#include <openssl\sha.h>
#include <sstream>
#include <iomanip>


HttpControlServer::HttpControlServer()
{
}


HttpControlServer::~HttpControlServer()
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

void HttpControlServer::OnRequest(NetworkServerConnection& connection, NetworkRequest& request)
{
	LOG("HttpControlServer::OnRequest() type:[%s] path:[%s] protocol:[%s]\n", 
		request.type.c_str(), request.path.c_str(), request.protocol.c_str());

	std::ostringstream os;
	std::string body;

	if (request.path.substr(0, 5) == "/say/") // command for text to speech via Sonos
	{
		OnSayCommand(connection, request);
	}
	else if (request.path.substr(0, 5) == "/tts/") // sonos requesting mp3 text to speech audio
	{

	}
	else
	{
		body = "<html><body>HttpControlServer</body></html>";
		os << "HTTP/1.1 200 OK\r\n"
			"Date: Fri, 31 Dec 1999 23:59:59 GMT\r\n"
			"Connection: close\r\n"
			"Content-Type: text/html\r\n"
			"Content-Length: " << body.length() << "\r\n\r\n" << body;
		connection.Transmit(os.str().c_str(), os.str().length());
	}
}

void HttpControlServer::OnSayCommand(NetworkServerConnection& connection, NetworkRequest& request)
{
	// we're here because request.path[0..4] = '/say/'

	if (request.type == "GET")
	{
		std::string text = request.path.substr(5, request.path.find(" ", 5));
		text = UnescapeText(text);

		// calculate filename

		std::string filename = GetTextHashFilename(text);

		// look for the file



	}
	else
	{
		// ### todo: POST
	}
}

std::string HttpControlServer::UnescapeText(const std::string& text)
{
	std::ostringstream os;

	for (int i = 0; i < (int)text.size(); i++)
	{
		if (text[i] == '%')
		{
			if (text.size() - i >= 2)
				os << (char)std::stoi(text.substr(i + 1, 2), nullptr, 16);

			i += 2;
		}
		else
		{
			os << text[i];
		}

	}

	return os.str();
}

std::string HttpControlServer::GetTextHashFilename(const std::string& text)
{
	const int HashLen = 256 / 8;

	unsigned char hash[HashLen];

	SHA256((unsigned char*)text.c_str(), text.size(), hash);

	std::ostringstream os;

	for (int i = 0; i < HashLen; i++)
	{
		os << std::setfill('0') << std::setw(2) << std::hex << (int)hash[i];
	}

	os << ".mp3";

	return os.str();
}

///////////////////////////////////////////////////////////////////////////////
// HttpControlServerConnection implementation
///////////////////////////////////////////////////////////////////////////////

HttpControlServerConnection::HttpControlServerConnection(NetworkServerInterface* pServerInterface, SOCKET socket, SOCKADDR_IN& remoteAddr)
	: NetworkServerConnection(pServerInterface, socket, remoteAddr)
{

}

HttpControlServerConnection::~HttpControlServerConnection()
{

}
