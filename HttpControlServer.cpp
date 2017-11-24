#include "stdafx.h"
#include "HttpControlServer.h"
#include "VoiceRssInterface.h"
#include "SonosInterface.h"
#include <openssl\sha.h>
#include <sstream>
#include <fstream>
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

	if (request.path.substr(0, 7) == "/saygui") // web page for TTS
	{
		OnSayGui(connection, request);
	}
	else if (request.path.substr(0, 5) == "/say/") // command for text to speech via Sonos
	{
		if (!OnSayCommand(connection, request))
			SendBadResponse(connection, "Error");
		else
			SendGoodResponse(connection, "OK");
	}
	else if (request.path.substr(0, 7) == "/pause/")
		{
		if (!OnPauseCommand(connection, request))
			SendBadResponse(connection, "Error");
		else
			SendGoodResponse(connection, "OK");
		}
	else if (request.path.substr(0, 6) == "/stop/")
	{
		if (!OnStopCommand(connection, request))
			SendBadResponse(connection, "Error");
		else
			SendGoodResponse(connection, "OK");
	}
	else if (request.path.substr(0, 6) == "/test/")
	{
		if (!OnTestCommand(connection, request))
			SendBadResponse(connection, "Error");
		else
			SendGoodResponse(connection, "OK");
	}
	else if (request.path.substr(0, 5) == "/tts/") // sonos requesting mp3 text to speech audio
	{
		OnServeTts(connection, request);
	}
	else if (request.path.substr(0, 6) == "/play/")
	{
		if (!OnPlayCommand(connection, request))
			SendBadResponse(connection, "Error");
		else
			SendGoodResponse(connection, "OK");
	}
	else if (request.path.substr(0, 9) == "/playfav/")
	{
		if (!OnPlayFavCommand(connection, request))
			SendBadResponse(connection, "Error");
		else
			SendGoodResponse(connection, "OK");
	}
	else if (request.path.substr(0, 12) == "/getdevices/")
	{
		OnGetDevicesCommand(connection, request);
	}
	else if (request.path.substr(0, 10) == "/volumeup/")
	{
		if (!OnVolumeUpCommand(connection, request))
			SendBadResponse(connection, "Error");
		else
			SendGoodResponse(connection, "OK");
	}
	else if (request.path.substr(0, 12) == "/volumedown/")
	{
		if (!OnVolumeDownCommand(connection, request))
			SendBadResponse(connection, "Error");
		else
			SendGoodResponse(connection, "OK");
	}
	else if (request.path.substr(0, 11) == "/setvolume/")
	{
		if (!OnSetVolumeCommand(connection, request))
			SendBadResponse(connection, "Error");
		else
			SendGoodResponse(connection, "OK");

	}
	else if (request.path.substr(0, 7) == "/group/")
	{
		if (!OnGroupCommand(connection, request))
			SendBadResponse(connection, "Error");
		else
			SendGoodResponse(connection, "OK");

	}
	else if (request.path.substr(0, 9) == "/ungroup/")
	{
		if (!OnUngroupCommand(connection, request))
			SendBadResponse(connection, "Error");
		else
			SendGoodResponse(connection, "OK");

	}
	else
	{
		SendGoodResponse(connection, "Sonos HTTP Control Server");
	}
}

std::vector<std::string> HttpControlServer::Split(const std::string& str, char delimiter)
{
	std::vector<std::string> segments;

	size_t pos = 0, lastPos = 0;

	while ((pos = str.find(delimiter, lastPos)) != std::string::npos)
	{
		if (pos == lastPos) // case where first char is delimiter
		{
			lastPos++;
			continue;
		}

		segments.push_back(str.substr(lastPos, (pos - lastPos)));

		lastPos = pos + 1;
	}

	// account for final portion if not ending with delimiter
	if (lastPos < str.size()-1)
		segments.push_back(str.substr(lastPos));

	return segments;
}

bool HttpControlServer::OnSayGui(NetworkServerConnection& connection, NetworkRequest& request)
{
	std::ifstream fs("http\\TTS.html");
	NetworkResponse resp("HTTP/1.0", 200, "OK");

	if (fs.bad())
	{
		resp.responseCode = 404;
		resp.reason = "Not Found";
		connection.SendResponse(resp, true);
		return false;
	}

	std::stringstream s;

	s << fs.rdbuf();

	resp.AddHeaderField("Content-Type", "text/html");
	resp.AddContent(s.str().c_str(), s.str().length());
	connection.SendResponse(resp, true);

	return true;
}

bool HttpControlServer::OnSayCommand(NetworkServerConnection& connection, NetworkRequest& request)
{
	bool success = false;

	std::vector<std::string> pathParts = Split(request.path, '/');

	// we're here because request.path[0..4] = '/say/'

	if (request.type == "GET")
	{
		std::string speaker = pathParts[1]; // 0 will be "say"
		std::string text = pathParts[2]; 
		text = UnescapeText(text);

		// calculate filename

		std::string filename = GetTextHashFilename(text);
		std::string pathname = _ttsPath + "\\" + filename;

		// look for the file

		struct _stat stats;

		std::vector<unsigned char> audioData;

		if (_stat(pathname.c_str(), &stats) != -1) // we have the audio already
		{
			LOG("HttpControlServer::OnSayCommand() using cached TTS sample\n");

			success = true;
		}
		else
		{
			VoiceRssInterface vi;
			vi.Initialise("40885560089140ba89f5caef69ccd65a");

			if (vi.Convert(text, audioData))
			{
				LOG("HttpControlServer::OnSayCommand() got TTS sample from the web\n");

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
				success = false;
			}
		}
		
		// now say it!

		if (success)
		{
			const int MaxHostLen = 255;
			char hostname[MaxHostLen] = { 0 };

			if (gethostname(hostname, MaxHostLen))
			{
				LOG("HttpControlServer::OnSayCommand() error: unable to get hostname\n");
			}
			else
			{
				// speaker "ALL" = all speakers

				std::ostringstream uri;
				uri << "http://" << hostname << ":" << _port << "/tts/" << filename;
				SonosInterface::GetInstance()->PlayFileFromServer(speaker, uri.str(), "Voice Alert");
			}

		}

	}
	else
	{
		// ### todo: POST
	}

	return success;
}

bool HttpControlServer::OnPauseCommand(NetworkServerConnection& connection, NetworkRequest& request)
{
	bool success = false;

	std::vector<std::string> pathParts = Split(request.path, '/');

	// we're here because request.path[0..6] = '/pause/'

	if (request.type == "GET" && pathParts.size() > 1)
	{
		std::string speaker = pathParts[1]; // 0 will be "pause"

		// speaker "ALL" = all speakers

		success = SonosInterface::GetInstance()->PauseBlocking(speaker);
	}
	else
	{
		// ### todo: POST
	}

	return success;
}

bool HttpControlServer::OnStopCommand(NetworkServerConnection& connection, NetworkRequest& request)
{
	bool success = false;

	std::vector<std::string> pathParts = Split(request.path, '/');

	// we're here because request.path[0..5] = '/stop/'

	if (request.type == "GET" && pathParts.size() > 1)
	{
		std::string speaker = pathParts[1]; // 0 will be "stop"
											// speaker "ALL" = all speakers

		success = SonosInterface::GetInstance()->StopBlocking(speaker);
	}
	else
	{
		// ### todo: POST
	}

	return success;
}

bool HttpControlServer::OnPlayCommand(NetworkServerConnection& connection, NetworkRequest& request)
{
	bool success = false;

	std::vector<std::string> pathParts = Split(request.path, '/');

	// we're here because request.path[0..5] = '/play/'

	if (request.type == "GET" && pathParts.size() > 1)
	{
		std::string speaker = pathParts[1]; // 0 will be "stop"
											// speaker "ALL" = all speakers

		success = SonosInterface::GetInstance()->PlayBlocking(speaker);
	}
	else
	{
		// ### todo: POST
	}

	return success;
}

bool HttpControlServer::OnVolumeUpCommand(NetworkServerConnection& connection, NetworkRequest& request)
{
	bool success = false;

	std::vector<std::string> pathParts = Split(request.path, '/');

	// we're here because request.path[0..9] = '/volumeup/'

	if (request.type == "GET" && pathParts.size() > 1)
	{
		std::string speaker = pathParts[1]; // 0 will be "volumeup"
											//[ speaker "ALL" = all speakers ] ### TODO!

		// get the curent volume

		int vol = 0;
		if (!SonosInterface::GetInstance()->GetVolumeBlocking(speaker, vol))
			return false;

		// now increase by 10%

		vol += 10;

		if (vol > 90) vol = 90;

		success = SonosInterface::GetInstance()->SetVolumeBlocking(speaker, vol);
	}
	else
	{
		// ### todo: POST
	}

	return success;
}

bool HttpControlServer::OnVolumeDownCommand(NetworkServerConnection& connection, NetworkRequest& request)
{
	bool success = false;

	std::vector<std::string> pathParts = Split(request.path, '/');

	// we're here because request.path[0..11] = '/volumedown/'

	if (request.type == "GET" && pathParts.size() > 1)
	{
		std::string speaker = pathParts[1]; // 0 will be "volumedown"
											//[ speaker "ALL" = all speakers ] ### TODO!

		// get the curent volume

		int vol = 0;
		if (!SonosInterface::GetInstance()->GetVolumeBlocking(speaker, vol))
			return false;

		// now decrease by 10%

		vol -= 10;

		if (vol < 0) vol = 0;

		success = SonosInterface::GetInstance()->SetVolumeBlocking(speaker, vol);
	}
	else
	{
		// ### todo: POST
	}

	return success;
}

bool HttpControlServer::OnSetVolumeCommand(NetworkServerConnection& connection, NetworkRequest& request)
{
	bool success = false;

	std::vector<std::string> pathParts = Split(request.path, '/');

	// we're here because request.path[0..10] = '/setvolume/'

	if (request.type == "GET" && pathParts.size() > 2)
	{
		std::string speaker = pathParts[1]; // 0 will be "setvolume"
											//[ speaker "ALL" = all speakers ] ### TODO!

		int vol = atoi(pathParts[2].c_str());

		if (vol < 0 || vol > 100) // don't do anything if an unexpected (non percentage) number given
			return false;

		if (vol > 90) vol = 90; // set an arbitrary limit of 90%
			
		success = SonosInterface::GetInstance()->SetVolumeBlocking(speaker, vol);
	}
	else
	{
		// ### todo: POST
	}

	return success;
}

bool HttpControlServer::OnGroupCommand(NetworkServerConnection& connection, NetworkRequest& request)
{
	bool success = false;

	std::vector<std::string> pathParts = Split(request.path, '/');

	// we're here because request.path[0..10] = '/setvolume/'

	if (request.type == "GET" && pathParts.size() > 1)
	{
		std::string speaker = pathParts[1]; // 0 will be "group"

		success = SonosInterface::GetInstance()->GroupSpeakersBlocking(speaker);
	}
	else
	{
		// ### todo: POST
	}

	return success;
}

bool HttpControlServer::OnUngroupCommand(NetworkServerConnection& connection, NetworkRequest& request)
{
	return SonosInterface::GetInstance()->UngroupSpeakersBlocking();
}


bool HttpControlServer::OnTestCommand(NetworkServerConnection& connection, NetworkRequest& request)
{
	int vol;
	bool muted;

	//SonosInterface::GetInstance()->GetVolumeBlocking("Kitchen", vol);
	//SonosInterface::GetInstance()->GetMuteBlocking("Kitchen", muted);

	std::vector<std::string> pathParts = Split(request.path, '/');
	SonosInterface::GetInstance()->GetFavouritesBlocking(pathParts[1]);

	return true;
}

bool HttpControlServer::OnPlayFavCommand(NetworkServerConnection& connection, NetworkRequest& request)
{

	// request url will be /playfav/<room>/<name of favourite>
	
	std::vector<std::string> pathParts = Split(request.path, '/');

	if (pathParts.size() < 3)
		return false;

	std::string fav = UnescapeText(pathParts[2]);

	// set the volumet to 25% first

	if (!SonosInterface::GetInstance()->SetVolumeBlocking(pathParts[1], 25))
		return false;

	return SonosInterface::GetInstance()->PlayFavouriteBlocking(pathParts[1], fav);

	//### todo

	// to find track: AVTransport:GetPositionInfo action and look at TrackURI/TrackMetaData
	// to find play state use: AvTransport:GetTransportInfo the look at CurrentTransportState and maybe CurrentTransportStatus
}


bool HttpControlServer::OnServeTts(NetworkServerConnection& connection, NetworkRequest& request)
{
	// we're here because request.path[0..4] = '/tts/'

	std::string pathname = _ttsPath + "\\" + request.path.substr(5);

	NetworkResponse resp("HTTP/1.0", 200, "OK");

	FILE* fin = fopen(pathname.c_str(), "rb");

	if (fin)
	{
		fseek(fin, 0L, SEEK_END);
		long size = ftell(fin);
		fseek(fin, 0L, SEEK_SET);

		char* pAudioData = new char[size];
		fread(pAudioData, 1, size, fin);
		fclose(fin);

		resp.AddHeaderField("Content-Type", "audio/mpeg3");
		resp.AddContent(pAudioData, size);

		delete[] pAudioData;

	}
	else
	{
		resp.responseCode = 404;
		resp.reason = "Not Found";
	}

	connection.SendResponse(resp);

	return true;
}

bool HttpControlServer::OnGetDevicesCommand(NetworkServerConnection& connection, NetworkRequest& request)
{
	// Get a list of devices from the SonosInterface and respond with a JSON object

	std::stringstream json;

	std::list<SonosDevice> devList;

	SonosInterface::GetInstance()->GetListOfDevices(devList);

	json << (R"({"devices": [)");

	for (auto it = devList.begin(); it != devList.end(); ++it)
	{
		SonosDevice& dev = *it;

		if (it != devList.begin())
			json << ",";

		json << "{" << R"("name": ")" << dev._name << R"(", )";
		json << R"("id": ")" << dev._udn << R"(", )";
		json << R"("address": ")" << dev._address << R"("})";
	}

	json << "]}";

	NetworkResponse resp("HTTP/1.0", 200, "OK");
	resp.AddHeaderField("Content-Type", "application/json");
	resp.AddContent(json.str().c_str(), json.str().length());

	connection.SendResponse(resp);

	return true;
}


void HttpControlServer::SendGoodResponse(NetworkServerConnection& connection, const std::string& body)
{
	NetworkResponse resp("HTTP/1.0", 200, "OK");
	resp.AddHeaderField("Content-Type", "text/plain");
	resp.AddContent(body.c_str(), body.size());
	connection.SendResponse(resp, true);
}

void HttpControlServer::SendBadResponse(NetworkServerConnection& connection, const std::string& body)
{
	NetworkResponse resp("HTTP/1.0", 500, "Internal Server Error");
	resp.AddHeaderField("Content-Type", "text/plain");
	resp.AddContent(body.c_str(), body.size());
	connection.SendResponse(resp, true);
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
