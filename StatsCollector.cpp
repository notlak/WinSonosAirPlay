#include "stdafx.h"
#include "StatsCollector.h"

StatsCollector* StatsCollector::pInstance = nullptr;

StatsCollector* StatsCollector::GetInstance()
{
	if (pInstance == nullptr)
		pInstance = new StatsCollector();

	return pInstance;
}

void StatsCollector::Close()
{

}



StatsCollector::StatsCollector()
{
	
}


StatsCollector::~StatsCollector()
{
}


void StatsCollector::AddRxPacket()
{
	std::lock_guard<std::mutex> lock(_statsMutex);
	_stats.rxPackets++;
}

void StatsCollector::AddRxBytes(int nBytes)
{
	std::lock_guard<std::mutex> lock(_statsMutex);
	_stats.rxBytes += nBytes;
}

void StatsCollector::AddMissedPackets(int nPackets)
{
	std::lock_guard<std::mutex> lock(_statsMutex);
	_stats.missedPackets += nPackets;
}

StatsCollector::Stats StatsCollector::GetandReset()
{
	std::lock_guard<std::mutex> lock(_statsMutex);

	Stats s = _stats;

	_stats.Reset();

	return s;
}