#include "stdafx.h"
#include "RtspServer.h"

#include <openssl\pem.h>
#include <openssl\rsa.h>


int Base64Decode(const char* b64message, unsigned char** buffer, size_t* length)
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
	assert(*length == decodeLen); // length should equal decodeLen, else something went horribly wrong
	BIO_free_all(bio);

	return 0;
}

int Base64Encode(const unsigned char* buffer, size_t length, char* b64text)
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


RtspServer::RtspServer()
	: _airPortExpressKey(nullptr)
{
	ASSERT(LoadAirPortExpressKey());
}


RtspServer::~RtspServer()
{
	if (_airPortExpressKey)
		RSA_free(_airPortExpressKey);
}

bool RtspServer::LoadAirPortExpressKey()
{
	FILE* fKey = fopen("private.key", "rb");

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

		int encLen = RSA_private_encrypt(32, rawAppleResp, encAppleResp, _airPortExpressKey, RSA_PKCS1_PADDING);

		char* b64AppleResp = new char[2048];

		Base64Encode(encAppleResp, encLen, b64AppleResp);

		resp.AddHeaderField("Apple-Response", b64AppleResp);

		delete[] b64AppleResp;

	}

	connection.SendResponse(resp);
}

void RtspServer::HandleAnnounce(NetworkServerConnection& connection, NetworkRequest& request)
{
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

	std::string b64Key = request.headerFieldMap["RSAAESKEY"];

	unsigned char* encKey;
	size_t encKeyLen = 0;

	Base64Decode(b64Key.c_str(), &encKey, &encKeyLen);

	RSA_private_decrypt(encKeyLen, encKey, _aesKey, _airPortExpressKey, RSA_PKCS1_OAEP_PADDING);

	free(encKey);

	pos = req.find("aesiv:");

	if (pos != std::string::npos)
	{
		pos += 6;
		len = req.find('\r', pos);

		len -= pos;
		std::string b64AesIv = req.substr(pos, len);

		unsigned char* iv = nullptr;
		size_t ivLen = 0;

		Base64Decode(b64AesIv.c_str(), &iv, &ivLen);

		memcpy(_aesIv, iv, ivLen);
	}

	// parse the codec parameters for ALAC

	pos = req.find("a=fmtp:");

	if (pos != std::string::npos)
	{
		pos += 7;
		len = req.find('\r', pos);

		len -= pos;
		std::string params = req.substr(pos, len);

		// create magic cookie for ALAC decode
		// ###todo: handle parameters other than those for ALAC

		// format: a=fmtp:96 352 0 16 40 10 14 2 255 0 0 44100

		int frameLength, compatibleVersion, bitDepth, pb, mb, kb, numChannels, maxRun, maxFrameBytes, avgBitRate, sampleRate;

		int nParams = sscanf(params.c_str(), "96 %d %d %d %d %d %d %d %d %d %d %d",
			&frameLength, &compatibleVersion, &bitDepth,
			&pb, &mb, &kb,
			&numChannels, &maxRun, &maxFrameBytes, &avgBitRate, &sampleRate);

		ASSERT(nParams == 11);

		//### check packing issues

		_alacConfig.frameLength = frameLength;
		_alacConfig.compatibleVersion = (uint8_t)compatibleVersion;
		_alacConfig.bitDepth = (uint8_t)bitDepth;
		_alacConfig.pb = (uint8_t)pb;
		_alacConfig.mb = (uint8_t)mb;
		_alacConfig.kb = (uint8_t)kb;
		_alacConfig.numChannels = (uint8_t)numChannels;
		_alacConfig.maxRun = (uint16_t)maxRun;
		_alacConfig.maxFrameBytes = maxFrameBytes;
		_alacConfig.avgBitRate = avgBitRate;
		_alacConfig.sampleRate = sampleRate;

	}

}

void RtspServer::HandleSetup(NetworkServerConnection& connection, NetworkRequest& request)
{
}

void RtspServer::HandleRecord(NetworkServerConnection& connection, NetworkRequest& request)
{
}

void RtspServer::HandleFlush(NetworkServerConnection& connection, NetworkRequest& request)
{
}

void RtspServer::HandleGetParameter(NetworkServerConnection& connection, NetworkRequest& request)
{
}

void RtspServer::HandleSetParameter(NetworkServerConnection& connection, NetworkRequest& request)
{
}

void RtspServer::HandleTeardown(NetworkServerConnection& connection, NetworkRequest& request)
{
}

