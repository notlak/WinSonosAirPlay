#include "stdafx.h"
#include "RtspServer.h"
#include "StreamingServer.h"
#include "SonosInterface.h"

#include <sstream>
#include <openssl\pem.h>

//#ifdef _DEBUG
//#define new DEBUG_NEW
//#endif

static int Base64Decode(const char* b64message, unsigned char** buffer, size_t* length)
{
	BIO *bio, *b64;
	int b64Len = strlen(b64message);
	int decodeLen = (b64Len * 3) / 4;

	if (b64message[b64Len - 1] == '=')
		decodeLen--;

	if (b64message[b64Len - 2] == '=')
		decodeLen--;

	*buffer = (unsigned char*) new char[decodeLen + 1];
	(*buffer)[decodeLen] = '\0';

	bio = BIO_new_mem_buf((void*)b64message, -1);
	b64 = BIO_new(BIO_f_base64());
	bio = BIO_push(b64, bio);

	BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL); //Do not use newlines to flush buffer
	*length = BIO_read(bio, *buffer, strlen(b64message));

	BIO_free_all(bio);

	return 0;
}

static int Base64Encode(const unsigned char* buffer, size_t length, char* b64text)
{
	BIO *bio, *b64;
	BUF_MEM *bufferPtr;

	b64 = BIO_new(BIO_f_base64());
	bio = BIO_new(BIO_s_mem());
	bio = BIO_push(b64, bio);

	BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL); // ignore newlines - write everything in one line
	BIO_write(bio, buffer, length);
	BIO_flush(bio);
	BIO_get_mem_ptr(bio, &bufferPtr);
	BIO_set_close(bio, BIO_NOCLOSE);
	BIO_free_all(bio);

	//*b64text = (*bufferPtr).data;

	memcpy(b64text, bufferPtr->data, bufferPtr->length);
	b64text[bufferPtr->length] = '\0';

	// hopefully this frees the allocated memory
	BUF_MEM_free(bufferPtr);

	return 0;
}

///////////////////////////////////////////////////////////////////////////////
// RtspServer
///////////////////////////////////////////////////////////////////////////////

RtspServer::RtspServer(const std::string& sonosUdn)
	: _airPortExpressKey(nullptr), _sonosUdn(sonosUdn)
{
	if (!LoadAirPortExpressKey())
		LOG("Error: unable to load private.key\n");

	_sonosStreamId = StreamingServer::GetStreamId();
}

RtspServer::~RtspServer()
{
	if (_airPortExpressKey)
		RSA_free(_airPortExpressKey);
}

bool RtspServer::LoadAirPortExpressKey()
{
	FILE* fKey = fopen("private.key", "rb");

	if (!fKey)
		return false;

	PEM_read_RSAPrivateKey(fKey, &_airPortExpressKey, nullptr, nullptr);

	fclose(fKey);

	return true;
}

void RtspServer::OnRequest(NetworkServerConnection& connection, NetworkRequest& request)
{
	if (request.type == "OPTIONS")
		HandleOptions(connection, request);
	else if (request.type == "ANNOUNCE")
		HandleAnnounce(connection, request);
	else if (request.type == "SETUP")
		HandleSetup(connection, request);
	else if (request.type == "RECORD")
		HandleRecord(connection, request);
	else if (request.type == "FLUSH")
		HandleFlush(connection, request);
	else if (request.type == "GET_PARAMETER")
		HandleGetParameter(connection, request);
	else if (request.type == "SET_PARAMETER")
		HandleSetParameter(connection, request);
	else if (request.type == "TEARDOWN")
		HandleTeardown(connection, request);

}

void RtspServer::HandleOptions(NetworkServerConnection& connection, NetworkRequest& request)
{
	LOG("RTSPServer: Got OPTIONS request\n");

	NetworkResponse resp("RTSP/1.0", 200, "OK");
	resp.AddHeaderField("Public", "OPTIONS, ANNOUNCE, SETUP, RECORD, PAUSE, FLUSH, TEARDOWN, GET_PARAMETER, SET_PARAMETER");
	resp.AddHeaderField("Server", "AirTunes/105.1");
	resp.AddHeaderField("CSeq", request.headerFieldMap["CSEQ"].c_str());

	if (request.headerFieldMap.find("APPLE-CHALLENGE") != request.headerFieldMap.end())
	{
		unsigned char rawAppleResp[32];
		int nRawAppleResp = 0;
		memset(rawAppleResp, 0, 32);
		size_t chLen = 0;
		unsigned char* pChallenge = nullptr;

		Base64Decode(request.headerFieldMap["APPLE-CHALLENGE"].c_str(), &pChallenge, &chLen);

		memcpy(rawAppleResp, pChallenge, chLen);
		delete[] pChallenge;
		nRawAppleResp = chLen;

		connection.GetIpAddress(&rawAppleResp[nRawAppleResp]);
		nRawAppleResp += 4;

		// copy mac address in, think this can be same random one used in mDNS advertisement

		rawAppleResp[nRawAppleResp++] = 0x11;
		rawAppleResp[nRawAppleResp++] = 0x22;
		rawAppleResp[nRawAppleResp++] = 0x33;
		rawAppleResp[nRawAppleResp++] = 0x44;
		rawAppleResp[nRawAppleResp++] = 0x55;
		rawAppleResp[nRawAppleResp++] = 0x66;
		rawAppleResp[nRawAppleResp++] = 0x77;
		rawAppleResp[nRawAppleResp++] = 0x88;

		// encrypt the apple response
		unsigned char encAppleResp[1024];

		int encLen = 0;

		if (_airPortExpressKey)
			encLen = RSA_private_encrypt(32, rawAppleResp, encAppleResp, _airPortExpressKey, RSA_PKCS1_PADDING);

		char* b64AppleResp = new char[2048];

		Base64Encode(encAppleResp, encLen, b64AppleResp);

		resp.AddHeaderField("Apple-Response", b64AppleResp);

		delete[] b64AppleResp;

	}

	connection.SendResponse(resp);
}

void RtspServer::HandleAnnounce(NetworkServerConnection& connection, NetworkRequest& request)
{
	LOG("RTSPServer: Got ANNOUNCE request\n");

	// needed from SDP portion of request
	// a=rsaaeskey:<base64 key> - base64 decrypt RSA-OAEP
	// a=aesiv:<iv> - init vector for music decrypt
	// a=rtpmap:<codec> - '96 L16/44100/2' = PCM, '96 AppleLossless' only these options supported? if not emit 415 error
	// a=fmtp:<codec optios> put in audioOptions[], used?
	// (a=fmtp:96 352 0 16 40 10 14 2 255 0 0 44100)
	//   ??, frameLength, compatibleVersion, bitDepth, pb, mb, kb, channels, maxRun, maxFrameBytes, avgBitRate, sampleRate
	// i=<client name>
	// if c= contains IP6 the IPv6 in use

	// create either an ALAC or PCM decoder stream depending on rtpmap
	// create output stream

	RtspServerConnection* pRtspServerConnection = static_cast<RtspServerConnection*>(&connection);

	// the parameters of interest are in the body, we null terminate this

	std::string content = request.pContent;

	size_t pos = content.find("rsaaeskey:");
	size_t len = 0;

	if (pos != std::string::npos)
	{
		pos += 10;
		len = content.find('\r', pos);

		len -= pos;

		std::string b64Key = content.substr(pos, len);

		unsigned char* encKey;
		size_t encKeyLen = 0;

		Base64Decode(b64Key.c_str(), &encKey, &encKeyLen);

		if (_airPortExpressKey)
			RSA_private_decrypt(encKeyLen, encKey, pRtspServerConnection->_aesKey, _airPortExpressKey, RSA_PKCS1_OAEP_PADDING);

		delete[] encKey;
	}

	pos = content.find("aesiv:");

	if (pos != std::string::npos)
	{
		pos += 6;
		len = content.find('\r', pos);

		len -= pos;
		std::string b64AesIv = content.substr(pos, len);

		unsigned char* iv = nullptr;
		size_t ivLen = 0;

		Base64Decode(b64AesIv.c_str(), &iv, &ivLen);

		memcpy(pRtspServerConnection->_aesIv, iv, ivLen);

		delete[] iv;
	}

	// parse the codec parameters for ALAC

	pos = content.find("a=fmtp:");

	if (pos != std::string::npos)
	{
		pos += 7;
		len = content.find('\r', pos);

		len -= pos;
		std::string params = content.substr(pos, len);

		// create magic cookie for ALAC decode
		// ###todo: handle parameters other than those for ALAC

		// format: a=fmtp:96 352 0 16 40 10 14 2 255 0 0 44100

		int frameLength, compatibleVersion, bitDepth, pb, mb, kb, numChannels, maxRun, maxFrameBytes, avgBitRate, sampleRate;

		int nParams = sscanf(params.c_str(), "96 %d %d %d %d %d %d %d %d %d %d %d",
			&frameLength, &compatibleVersion, &bitDepth,
			&pb, &mb, &kb,
			&numChannels, &maxRun, &maxFrameBytes, &avgBitRate, &sampleRate);

		//### check packing issues

		pRtspServerConnection->_alacConfig.frameLength = frameLength;
		pRtspServerConnection->_alacConfig.compatibleVersion = (uint8_t)compatibleVersion;
		pRtspServerConnection->_alacConfig.bitDepth = (uint8_t)bitDepth;
		pRtspServerConnection->_alacConfig.pb = (uint8_t)pb;
		pRtspServerConnection->_alacConfig.mb = (uint8_t)mb;
		pRtspServerConnection->_alacConfig.kb = (uint8_t)kb;
		pRtspServerConnection->_alacConfig.numChannels = (uint8_t)numChannels;
		pRtspServerConnection->_alacConfig.maxRun = (uint16_t)maxRun;
		pRtspServerConnection->_alacConfig.maxFrameBytes = maxFrameBytes;
		pRtspServerConnection->_alacConfig.avgBitRate = avgBitRate;
		pRtspServerConnection->_alacConfig.sampleRate = sampleRate;
	}

	// look for the name of the AIrPlay device

	pos = content.find("i=");

	if (pos != std::string::npos)
	{
		pos += 2;
		len = content.find('\r', pos) - pos;
		pRtspServerConnection->_airplayDevice = content.substr(pos, len);
	}

	NetworkResponse resp("RTSP/1.0", 200, "OK");
	resp.AddHeaderField("Server", "AirTunes/105.1");
	resp.AddHeaderField("CSeq", request.headerFieldMap["CSEQ"].c_str());

	connection.SendResponse(resp);
}

void RtspServer::HandleSetup(NetworkServerConnection& connection, NetworkRequest& request)
{
	LOG("RTSPServer: Got SETUP request\n");

	// nodetunes does:
	// generate 3 random port numbers 5000..9999 for audio, control and timing
	// call rtp.start() which creates UDP (dgram) sockets and binds them to the 3 ports
	// when data arrives at the audio socket decrypt it and write to output stream
	// use the control port to check for timeouts

	RtspServerConnection* pConn = static_cast<RtspServerConnection*>(&connection);

	pConn->_pAudioSocket = new CUdpSocket();
	pConn->_pControlSocket = new CUdpSocket();
	pConn->_pTimingSocket = new CUdpSocket();

LOG("Audio socket init\n");
	pConn->_pAudioSocket->Initialise();
LOG("Control socket init\n");
	pConn->_pControlSocket->Initialise();
LOG("Timing socket init\n");
	pConn->_pTimingSocket->Initialise();

	if (pConn->_pAudioSocket->GetPort() < 0 || pConn->_pControlSocket->GetPort() < 0 || pConn->_pTimingSocket->GetPort() < 0)
	{
		LOG("Error: unable to create UDP sockets\n");
	}
	else
	{
		NetworkResponse resp("RTSP/1.0", 200, "OK");
		resp.AddHeaderField("Server", "AirTunes/105.1");
		resp.AddHeaderField("CSeq", request.headerFieldMap["CSEQ"].c_str());

		std::ostringstream transport;
		transport << "RTP/AVP/UDP;unicast;mode=record"
			";server_port=" << pConn->_pAudioSocket->GetPort() <<
			";control_port=" << pConn->_pControlSocket->GetPort() <<
			";timing port=" << pConn->_pTimingSocket->GetPort();

		resp.AddHeaderField("Transport", transport.str().c_str());
		resp.AddHeaderField("Session", "1");
		resp.AddHeaderField("Audio-Jack-Status", "connected");

		connection.SendResponse(resp);

		// get a streamId
		//pConn->_streamId = StreamingServer::GetStreamId();

		// actually one will already have been associated to this server

		pConn->_streamId = _sonosStreamId;

		// tell the sonos to start playing the stream

		std::ostringstream uri;
		
		uri << "x-rincon-mp3radio://" << connection.GetIpAddress() << ":" << StreamingServer::GetPort() <<
			"/" << pConn->_streamId << "/listen.m3u";
			
		//uri << "http://us1.internet-radio.com:8180/listen.pls&t=.m3u";
		//uri << "x-rincon-mp3radio://http://www.voicerss.org/controls/speech.ashx?hl=en-gb&src=hello%20hazel%20you%20idiot&c=mp3&rnd=0.8578293843928012";

		std::string metadata("AirPlay");

		if (!pConn->_airplayDevice.empty())
			metadata = pConn->_airplayDevice;

		//SonosInterface::GetInstance()->SetAvTransportUri(_sonosUdn.c_str(), uri.str().c_str(), metadata.c_str());
		//SonosInterface::GetInstance()->Play(_sonosUdn.c_str());

		SonosInterface::GetInstance()->PlayUri(_sonosUdn.c_str(), uri.str().c_str(), metadata.c_str());

		pConn->_pAudioThread = new std::thread(&RtspServerConnection::AudioThread, pConn);
	}
}

void RtspServer::HandleRecord(NetworkServerConnection& connection, NetworkRequest& request)
{
	LOG("RTSPServer: Got RECORD request\n");

	NetworkResponse resp("RTSP/1.0", 200, "OK");
	resp.AddHeaderField("Server", "AirTunes/105.1");
	resp.AddHeaderField("CSeq", request.headerFieldMap["CSEQ"].c_str());
	resp.AddHeaderField("Audio-Latency", "0");

	connection.SendResponse(resp);
}

void RtspServer::HandleFlush(NetworkServerConnection& connection, NetworkRequest& request)
{
	LOG("RTSPServer: Got FLUSH request\n");

	NetworkResponse resp("RTSP/1.0", 200, "OK");
	resp.AddHeaderField("Server", "AirTunes/105.1");
	resp.AddHeaderField("CSeq", request.headerFieldMap["CSEQ"].c_str());

	connection.SendResponse(resp);
}

void RtspServer::HandleGetParameter(NetworkServerConnection& connection, NetworkRequest& request)
{
	LOG("RTSPServer: Got GET_PARAMETERS request\n");

	NetworkResponse resp("RTSP/1.0", 200, "OK");
	resp.AddHeaderField("Server", "AirTunes/105.1");
	resp.AddHeaderField("CSeq", request.headerFieldMap["CSEQ"].c_str());

	connection.SendResponse(resp);
}

// FindContentCode() returns the first byte after the content code or nullptr
unsigned char* RtspServer::FindContentCode(const char* pCode, unsigned char* pData, int len)
{
	unsigned char* pLoc = nullptr;

	for (int i = 0; i < len - 4 - 4 + 1 && !pLoc; i++)
	{
		if (pData[i] == pCode[0] && pData[i + 1] == pCode[1] &&
			pData[i + 2] == pCode[2] && pData[i + 3] == pCode[3])
		{
			pLoc = pData + i + 4;
		}
	}

	return pLoc;
}

void RtspServer::ParseDmap(unsigned char* pData, int len, MetaData& meta)
{
	if (len < 4 || memcmp(pData, "mlit", 4))
		return;

	int itemSize = ntohl(*(int*)(pData + 4));

	/*SET_PARAMETER rtsp://192.168.1.87/8891522206846895449 RTSP/1.0
	RTP-Info: rtptime=1799381347
	Content-Length: 134
	Content-Type: application/x-dmap-tagged
	CSeq: 8
	DACP-ID: 2F56DAFF2A472297
	Active-Remote: 1957818474
	User-Agent: AirPlay/267.3

	mlit...~mper.....H.D.>..asal....Mind Elevationasar....Nightmares on Waxascp....asgn....minm....On Days (bonus track)asdk.....caps.....*/

	unsigned char* pContent = nullptr;
	int fieldSize = 0;

	meta.album = "";
	meta.artist = "";
	meta.title = "";

	if ((pContent = FindContentCode("asal", pData + 8, itemSize)) != nullptr)
	{
		fieldSize = (ntohl(*(int*)pContent));
		meta.album.assign((char*)pContent + 4, fieldSize);
	}

	if ((pContent = FindContentCode("asar", pData + 8, itemSize)) != nullptr)
	{
		fieldSize = (ntohl(*(int*)pContent));
		meta.artist.assign((char*)pContent + 4, fieldSize);
	}

	if ((pContent = FindContentCode("minm", pData + 8, itemSize)) != nullptr)
	{
		fieldSize = (ntohl(*(int*)pContent));
		meta.title.assign((char*)pContent + 4, fieldSize);
	}
}

void RtspServer::HandleSetParameter(NetworkServerConnection& connection, NetworkRequest& request)
{
	LOG("RTSPServer: Got SET_PARAMETERS request\n");

	NetworkResponse resp("RTSP/1.0", 200, "OK");
	resp.AddHeaderField("Server", "AirTunes/105.1");
	resp.AddHeaderField("CSeq", request.headerFieldMap["CSEQ"].c_str());

	if (request.headerFieldMap.find("CONTENT-TYPE") != request.headerFieldMap.end())
	{
		// look for volume setting
		if (request.headerFieldMap["CONTENT-TYPE"] == "text/parameters")
		{
			std::string content(request.pContent);
			int pos = content.find("volume:");

			if (pos != std::string::npos)
			{
				pos += 8;
				float vol = (float)atof(content.substr(pos).c_str());

				LOG("Got AirPlay volume %f\n", vol);

				vol = vol > -30.0f ? vol : -30.0f;

				int sonosVol = 100 - (int)(-1.0f * (vol / 30.0f) * 100.0f);

				LOG("Setting Sonos volume to: %d\n", sonosVol);

				SonosInterface::GetInstance()->SetVolume(_sonosUdn.c_str(), sonosVol);
			}
		}
		// look for dmap metadata
		else if (request.headerFieldMap["CONTENT-TYPE"] == "application/x-dmap-tagged")
		{
			MetaData meta;
			ParseDmap((unsigned char*)request.pContent, request.contentLength, meta);
			LOG("Metadata: [%s] [%s] [%s]\n", 
				meta.album.c_str(), meta.artist.c_str(), meta.title.c_str());

			RtspServerConnection* pConn = dynamic_cast<RtspServerConnection*>(&connection);

			StreamingServer::GetStreamingServer()->MetaDataUpdate(pConn->_streamId, meta);
		}
	}

	connection.SendResponse(resp);
}

void RtspServer::HandleTeardown(NetworkServerConnection& connection, NetworkRequest& request)
{
	LOG("RTSPServer: Got TEARDOWN request\n");

	NetworkResponse resp("RTSP/1.0", 200, "OK");
	resp.AddHeaderField("Server", "AirTunes/105.1");
	resp.AddHeaderField("CSeq", request.headerFieldMap["CSEQ"].c_str());

	connection.SendResponse(resp);
}

///////////////////////////////////////////////////////////////////////////////
// RtspServerConnection
///////////////////////////////////////////////////////////////////////////////

RtspServerConnection::RtspServerConnection(NetworkServerInterface* pServerInterface, SOCKET socket, SOCKADDR_IN& remoteAddr)
	: NetworkServerConnection(pServerInterface, socket, remoteAddr),
	_pAudioSocket(nullptr),
	_pControlSocket(nullptr),
	_pTimingSocket(nullptr),
	_pAudioThread(nullptr),
	_stopAudioThread(false)
{

}

RtspServerConnection::~RtspServerConnection()
{
	LOG("RTSP connection for stream %d closed\n", _streamId);

	_stopAudioThread = true;

	if (_pAudioThread)
		_pAudioThread->join();

	delete _pAudioThread;

	delete _pAudioSocket;
	delete _pControlSocket;
	delete _pTimingSocket;
}

bool RtspServerConnection::Close()
{
	// stop the Sonos playing if we've lost the AirPlay connection

	RtspServer* pRtspServer = static_cast<RtspServer*>(_pServer);

	SonosInterface::GetInstance()->Stop(pRtspServer->GetAssociatedSonos().c_str());

	// now close as normal
	return NetworkServerConnection::Close();
}

void RtspServerConnection::AudioThread()
{
	LOG("AudioThread() running stream %d...\n", _streamId);

	const int BufferSize = 2048;
	char buffer[BufferSize];

	_transcoder.Init(&_alacConfig, _streamId); // streamId hardcoded to 1

	unsigned short lastSeq = 0;

	while (!_stopAudioThread)
	{
		int nBytes;

		nBytes = _pAudioSocket->Read(buffer, BufferSize);
		if (nBytes > 0)
		{
			unsigned short seq = 0;
			DecryptAudio((unsigned char*)buffer, nBytes, seq);
			_transcoder.Write((unsigned char*)buffer + 12, nBytes - 12);

			if (lastSeq > 0 && seq - lastSeq != 1)
				LOG("AudioThread() missing sequence %u -> %u\n", lastSeq, seq);

			lastSeq = seq;

			//LOG("Received %d bytes from Audio port Seq:%d\n", nBytes, seq);
		}

		nBytes = _pControlSocket->Read(buffer, BufferSize);
		//if (nBytes > 0)
		//LOG("Received %d bytes from Control port\n", nBytes);

		nBytes = _pTimingSocket->Read(buffer, BufferSize);
		//if (nBytes > 0)
		//LOG("Received %d bytes from Timing port\n", nBytes);
	}

}

bool RtspServerConnection::DecryptAudio(unsigned char* pEncBytes, int len, unsigned short& seq)
{
	const int HeaderSize = 12;

	int remainder = (len - HeaderSize) % 16;

	int endOfData = len - remainder;

	EVP_CIPHER_CTX* ctx; // context

	if (!(ctx = EVP_CIPHER_CTX_new()))
		return false;

	if (1 != EVP_DecryptInit_ex(ctx, EVP_aes_128_cbc(), nullptr, _aesKey, _aesIv))
	{
		EVP_CIPHER_CTX_free(ctx);
		return false;
	}

	EVP_CIPHER_CTX_set_padding(ctx, 0);

	int decLen = 0;
	unsigned char tempBuff[16];

	for (int i = HeaderSize; i <= endOfData - 16; i += 16)
	{
		memcpy(tempBuff, &pEncBytes[i], 16);
		EVP_DecryptUpdate(ctx, &pEncBytes[i], &decLen, tempBuff, 16);  // decrypt in 16 byte chunks
	}

	EVP_CIPHER_CTX_free(ctx);

	seq = (pEncBytes[2] << 8) + pEncBytes[3];

	return true;
}