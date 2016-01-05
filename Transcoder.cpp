#include "stdafx.h"
#include "Transcoder.h"


#include <ALACBitUtilities.h>

FILE* fPcmFile;
FILE* fMp3File;

CTranscoder::CTranscoder()
	: _pAlacDecoder(nullptr), _pAlacBitBuffer(nullptr)
{
	fPcmFile = fopen("airplay.pcm", "wb");
	fMp3File = fopen("airplay.mp3", "wb");
}

CTranscoder::~CTranscoder()
{
	delete _pAlacDecoder;
	delete _pAlacBitBuffer;
	delete[] _pAlacInputBuffer;
	delete[] _pAlacOutputBuffer;

	fclose(fPcmFile);
	fclose(fMp3File);

	lame_close(_pLameGlobalFlags);
	delete[] _pMp3Buffer;

}

void LameError(const char* format, va_list ap)
{
	TRACE("%s\n", format);
}

bool CTranscoder::Init(ALACSpecificConfig* alacConfig)
{
	// first the ALAC decoder

	_pAlacDecoder = new ALACDecoder();
	_pAlacDecoder->Init(alacConfig, sizeof ALACSpecificConfig);

	int nInputPacketBytes = alacConfig->numChannels * (alacConfig->bitDepth >> 3) * alacConfig->frameLength + kALACMaxEscapeHeaderBytes; // nChannelsPerFrame * output.bitsPerChannel/8 * input.framesPerPacket[frameLength?] + kALACMaxEscapeHeaderBytes
	_pAlacInputBuffer = new uint8_t[nInputPacketBytes];
	_pAlacOutputBuffer = new uint8_t[nInputPacketBytes - kALACMaxEscapeHeaderBytes];
	
	_pAlacBitBuffer = new BitBuffer();
	BitBufferInit(_pAlacBitBuffer, _pAlacInputBuffer, nInputPacketBytes);

	_alacConfig = *alacConfig;

	// now the lame decoder

	_pLameGlobalFlags = lame_init();

	lame_set_errorf(_pLameGlobalFlags, LameError);
	lame_set_debugf(_pLameGlobalFlags, LameError);
	lame_set_msgf(_pLameGlobalFlags, LameError);

	lame_set_num_channels(_pLameGlobalFlags, 2);
	lame_set_in_samplerate(_pLameGlobalFlags, 44100);
	lame_set_brate(_pLameGlobalFlags, 192); // 128
	lame_set_mode(_pLameGlobalFlags, JOINT_STEREO);
	lame_set_quality(_pLameGlobalFlags, 2);   /* 2=high  5 = medium  7=low */

	if (lame_init_params(_pLameGlobalFlags) < 0)
	{
		TRACE("Error: unable to initialise lame decoder\n");
		return false;
	}

	_nMp3Buffer = 1.25 * alacConfig->frameLength + 7200; // worst case apparently
	_pMp3Buffer = new uint8_t[_nMp3Buffer];

	return true;
}

bool CTranscoder::Write(unsigned char* pAlac, int len)
{
	uint32_t nOutFrames = 0;
	
	memcpy(_pAlacInputBuffer, pAlac, len);

	uint32_t status = _pAlacDecoder->Decode(_pAlacBitBuffer, _pAlacOutputBuffer, _alacConfig.frameLength, _alacConfig.numChannels, &nOutFrames);
	BitBufferReset(_pAlacBitBuffer);

	int nOutBytes = nOutFrames * _alacConfig.numChannels * _alacConfig.bitDepth >> 3;

	if (status < 0)
		TRACE("Error: ALAC Decode returned %d\n", status);
	else
		fwrite(_pAlacOutputBuffer, 1, nOutBytes, fPcmFile);
	
	int nMp3Bytes = lame_encode_buffer_interleaved(_pLameGlobalFlags, (short*)_pAlacOutputBuffer, nOutBytes >> 2, _pMp3Buffer, _nMp3Buffer);

	if (nMp3Bytes < 0)
		TRACE("Error: lame_encode_buffer() failed\n");
	else
		fwrite(_pMp3Buffer, 1, nMp3Bytes, fMp3File);

	return status >= 0;
}