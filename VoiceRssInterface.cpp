#include "stdafx.h"
#include "VoiceRssInterface.h"
#include "Log.h"
#include <openssl\sha.h>
#include <WS2tcpip.h>
#include <sstream>
#include <iomanip>
#include <vector>
#include <algorithm>

VoiceRssInterface::VoiceRssInterface()
{

}

VoiceRssInterface::~VoiceRssInterface()
{

}

void VoiceRssInterface::Initialise(const std::string& apiKey, const std::string& dirPath)
{
	_apiKey = apiKey;
	_dirPath = dirPath;
}

bool VoiceRssInterface::Convert(const std::string& text, std::string& filename)
{
	std::ostringstream req;
	std::string escText("");
	std::string doc;

	EscapeString(text, escText);

	req << "GET " 
		<< "/?key=" << _apiKey << "&src=" << escText << "&hl=en-gb&f=44khz_16bit_mono"
		<< " HTTP/1.0\r\nHost: api.voicerss.org\r\n\r\n";

	const int HashLen = 256 / 8;

	unsigned char hash[HashLen];

	SHA256((unsigned char*)text.c_str(), text.size(), hash);

	std::ostringstream os;

	for (int i = 0; i < HashLen; i++)
	{
		os << std::setfill('0') << std::setw(2) << std::hex << (int)hash[i];
	}

	os << ".mp3";

	filename = os.str();

	std::vector<unsigned char> content;

	bool success = NetworkRequest("api.voicerss.org", 80, req.str(), content);

	if (success && content.size() > 0)
	{
		std::string path = _dirPath + "\\" + filename;

		FILE* faudio = fopen(path.c_str(), "wb");

		if (faudio)
		{
			fwrite(content.data(), 1, content.size(), faudio);
			fclose(faudio);
		}
		else
		{
			LOG("VoiceRssInterface::Convert() error: unable to write to %s\n", path.c_str());
		}
			
	}

	return true;
}

bool VoiceRssInterface::NetworkRequest(const std::string& hostname, int port, const std::string& req, std::vector<unsigned char>& content)
{
	PADDRINFOA pAddrInfo = nullptr;
	int ret = 0;

	if (getaddrinfo(hostname.c_str(), "http", nullptr, &pAddrInfo))
		return false;
	
	bool success = false;

	content.clear();

	/*
	// prepare inet address from URL of document
	sockaddr_in addr;
	//addr.sin_addr.s_addr = inet_addr(ip); // put here ip address of device
	inet_pton(AF_INET, ip, &addr.sin_addr); // put here ip address of device
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port); // put here port for connection
	*/

	const int rbsize = 4096;    // internal buffer size
	char rbuff[rbsize] = { 0 };    // internal temporary receive buffer
	int rbshift = 0;            // index in internal buffer of current begin of free space
	int b = 0;                    // bytes curently received
	int tb = 0;                    // bytes totally received
	std::string respbuff;            // response buffer

	std::vector<char> respVec;


	DWORD startTime = GetTickCount();

	// create TCP socket
	SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	DWORD timeOutMs = 3000;
	//setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeOutMs, sizeof DWORD);

	sockaddr_in addr = { 0 };
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	memcpy(&addr.sin_addr, &((sockaddr_in*)pAddrInfo->ai_addr)->sin_addr, sizeof(addr.sin_addr));

	// connect socket
	if (connect(s, (sockaddr*)&addr, sizeof(sockaddr_in)) == SOCKET_ERROR)
	{
		int err = WSAGetLastError();
		LOG("VoiceRssInterface::NetworkRequest() connect failed: %d\n", err);
	}

	// send request
	b = send(s, req.c_str(), strlen(req.c_str()), 0);

	if (b == SOCKET_ERROR || b == 0)
	{
		int err = WSAGetLastError();
		LOG("Error: %d\n", err);
	}

	// receive response data - document
	while ((b = recv(s, rbuff + rbshift, rbsize - rbshift, 0)) != SOCKET_ERROR)
	{
		if (b == 0)
			break;

		// sum of all received chars
		tb += b;

		// sum of currently received chars
		rbshift += b;

		// temporary buffer has been filled thus copy data to response buffer
		if (rbshift == rbsize)
		{
			respVec.insert(respVec.end(), rbuff, rbuff + rbshift);
			rbshift = 0;
		}
	}

	if (b == SOCKET_ERROR)
	{
		DWORD now = GetTickCount();
		int err = WSAGetLastError();
		LOG("VoiceRssInterface::NetworkRequest() recv error:%d read:%d delay:%dms\n", err, tb, now - startTime);
	}

	// close connection gracefully
	shutdown(s, SD_SEND);
	//while (int bc = recv(s, rbuff, rbsize, 0))
	//if (bc == SOCKET_ERROR) break;
	closesocket(s);

	// analyse received data
	if (tb > 0)
	{
		// copy any remaining data to response buffer
		if (rbshift > 0)
			respVec.insert(respVec.end(), rbuff, rbuff + rbshift);

		const char* Newline = "\r\n";
		const char* EndOfHeader = "\r\n\r\n";
		const char* GoodResponse = "200 OK";

		// find end of header and convert to a string

		auto it = std::search(respVec.begin(), respVec.end(), EndOfHeader, EndOfHeader + strlen(EndOfHeader));

		std::string header("");

		if (it != respVec.end())
		{
			header = std::string(respVec.begin(), it);

			if (header.substr(0, header.find("\r\n")).find(GoodResponse) == std::string::npos)
			{
				LOG("VoiceRssInterface::NetworkRequest() error returned by server\n");
			}
			else
			{
				// get content-length

				int contentLen = 0;
				int actualContentLen = respVec.size() - (it - respVec.begin() + 4);

				// first convert header to lowercase
				std::transform(header.begin(), header.end(), header.begin(), tolower);

				int pos = header.find("content-length:");
				if (pos != std::string::npos)
				{
					std::istringstream(header.substr(pos, header.find("\r\n", pos)).substr(15)) >> contentLen;
				}

				LOG("ContentLen:%d Actual:%d\n", contentLen, actualContentLen);

				if (contentLen != actualContentLen)
				{
					LOG("VoiceRssInterface::NetworkRequest() bad content length\n");
					return false;
				}

				content = std::vector<unsigned char>(it + 4, respVec.end());
			}
		}
	}

	return true;
}

void VoiceRssInterface::EscapeString(const std::string& in, std::string& out)
{
	std::ostringstream os;

	for (int i = 0; i < (int)in.length(); i++)
	{
		if (in[i] == ' ')
			os << "%20";
		else if (in[i] == '"')
			os << "%22";
		else if (in[i] == '#')
			os << "%23";
		else if (in[i] == '$')
			os << "%24";
		else if (in[i] == '/')
			os << "%2F";
		else if (in[i] == '<')
			os << "%3C";
		else if (in[i] == '>')
			os << "%3E";
		else if (in[i] == '?')
			os << "%3F";
		else if (in[i] == '&')
			os << "%26";
		else
			os << in[i];
	}

	out = os.str();
}
