#include "stdafx.h"
#include "HttpControlServer.h"
#include "VoiceRssInterface.h"
#include <openssl\sha.h>
#include <sstream>
#include <iomanip>


HttpControlServer::HttpControlServer()
	: _ttsPath("TTS")
{
}


HttpControlServer::~HttpControlServer()
{
}

void HttpControlServer::Initialise(const std::string& ttsPath)
{
	_ttsPath = ttsPath;

	CreateDirectoryA(_ttsPath.c_str(), nullptr);

	// ### todo: check if ttsPath exists - if not create it
	// also decide on trailing '\' or not
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

	if (request.path.substr(0, 5) == "/say/") // command for text to speech via Sonos
	{
		NetworkResponse resp("HTTP/1.0", 200, "OK");

		if (!OnSayCommand(connection, request))
		{
			resp.responseCode = 500;
			resp.reason = "Internal Server Error";
			resp.AddHeaderField("Content-Type", "text/plain");
			std::string body = "Error";
			resp.AddContent(body.c_str(), body.size());
			connection.SendResponse(resp, true);
		}
		else
		{
			resp.AddHeaderField("Content-Type", "text/plain");
			std::string body = "OK";
			resp.AddContent(body.c_str(), body.size());
			connection.SendResponse(resp, true);
		}

	}
	else if (request.path.substr(0, 5) == "/tts/") // sonos requesting mp3 text to speech audio
	{
		OnServeTts(connection, request);
	}
	else
	{
		NetworkResponse resp("HTTP/1.0", 200, "OK");
		resp.AddHeaderField("Content-Type", "text/html");
		std::string body = "<html><body>HttpControlServer</body></html>";
		resp.AddContent(body.c_str(), body.size());
		connection.SendResponse(resp, true);

		/*
		std::ostringstream os;
		os << "HTTP/1.0 200 OK\r\n"
			//"Date: Fri, 31 Dec 1999 23:59:59 GMT\r\n"
			//"Connection: close\r\n"
			"Content-Type: text/html\r\n"
			"Content-Length: " << body.length() << "\r\n\r\n" << body;
		connection.Transmit(os.str().c_str(), os.str().length());
		*/

	}
}

bool HttpControlServer::OnSayCommand(NetworkServerConnection& connection, NetworkRequest& request)
{
	bool success = false;

	// we're here because request.path[0..4] = '/say/'

	if (request.type == "GET")
	{
		std::string text = request.path.substr(5, request.path.find(" ", 5));
		text = UnescapeText(text);

		// calculate filename

		std::string pathname = _ttsPath + "\\" + GetTextHashFilename(text);

		// look for the file

		struct _stat stats;

		char* pAudioData = nullptr;
		std::vector<unsigned char> audioData;

		if (_stat(pathname.c_str(), &stats) != -1) // we have the audio already
		{
			LOG("HttpControlServer::OnSayCommand() using cached TTS sample\n");

			pAudioData = new char[stats.st_size]; // not expecting a large file

			FILE* fin = fopen(pathname.c_str(), "rb");
			fread(pAudioData, 1, stats.st_size, fin);
			fclose(fin);

			success = true;
		}
		else
		{
			VoiceRssInterface vi;
			vi.Initialise("40885560089140ba89f5caef69ccd65a");

			if (vi.Convert(text, audioData))
			{
				LOG("HttpControlServer::OnSayCommand() got TTS sample from the web\n");
				pAudioData = (char*)audioData.data();

				success = true;
			}
			else
			{
				LOG("HttpControlServer::OnSayCommand() failed to get audio\n");
			}
		}

		if (audioData.size() > 0) // i.e. we just got this from the web, save it
		{
			FILE* faudio = fopen(pathname.c_str(), "wb");

			if (faudio)
			{
				fwrite(audioData.data(), 1, audioData.size(), faudio);
				fclose(faudio);
			}
			else
			{
				LOG("HttpControlServer::OnSayCommand() error: failed to save audio file %s\n", pathname.c_str());
			}
		}
		else
		{
			// we need to delete the buffer array we used when loading the file
			delete[] pAudioData;
		}
		
		// now say it!

		if (success)
		{

		}

	}
	else
	{
		// ### todo: POST
	}

	return success;
}

bool HttpControlServer::OnServeTts(NetworkServerConnection& connection, NetworkRequest& request)
{
	// we're here because request.path[0..4] = '/tts/'

	std::string pathname = _ttsPath + "\\" + request.path.substr(5);


	struct _stat stats;

	if (_stat(_ttsPath.c_str(), &stats) != -1)
	{

		//FILE* fin = fopen(pathname.c_str(rb));

		//if ()
		{

		}
	}
	else
	{
		return false;
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
