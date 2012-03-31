/**************************************************************************
*   Copyright (C) 2010 by Eugene V. Lyubimkin                             *
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License                  *
*   (version 3 or above) as published by the Free Software Foundation.    *
*                                                                         *
*   This program is distributed in the hope that it will be useful,       *
*   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
*   GNU General Public License for more details.                          *
*                                                                         *
*   You should have received a copy of the GNU GPL                        *
*   along with this program; if not, write to the                         *
*   Free Software Foundation, Inc.,                                       *
*   51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA               *
**************************************************************************/
#include <map>
#include <list>

#include <boost/lexical_cast.hpp>

#include <cupt/download/progress.hpp>

namespace cupt {

using boost::lexical_cast;

namespace internal {

typedef download::Progress::DownloadRecord DownloadRecord;

using std::map;
using std::list;

struct AliasPair
{
	string shortAlias;
	string longAlias;
};

class ProgressImpl
{
	// for download speed counting
	struct FetchedChunk
	{
		struct timespec timeSpec;
		size_t size;
	};
	list< FetchedChunk > fetchedChunks;
 public:
	ProgressImpl();
	void addChunk(size_t size);
	size_t getDownloadSpeed() const;

	uint64_t doneDownloadsSize;
	uint64_t fetchedSize;
	map< string, AliasPair > aliases;
	size_t nextDownloadNumber;
	uint64_t totalEstimatedSize;
	time_t startTimestamp;
	map< string, DownloadRecord > nowDownloading;
};

ProgressImpl::ProgressImpl()
	: doneDownloadsSize(0), fetchedSize(0), nextDownloadNumber(1),
	totalEstimatedSize(-1), startTimestamp(time(NULL))
{}

struct timespec getCurrentTimeSpec()
{
	struct timespec currentTimeSpec;
	if (clock_gettime(CLOCK_REALTIME, &currentTimeSpec) == -1)
	{
		warn2e(__("%s() failed"), "clock_gettime");
		currentTimeSpec.tv_sec = time(NULL);
		currentTimeSpec.tv_nsec = 0;
	}
	return currentTimeSpec;
}

float getTimeSpecDiff(const timespec& oldValue, const timespec& newValue)
{
	float result = newValue.tv_sec - oldValue.tv_sec;
	result += float(newValue.tv_nsec - oldValue.tv_nsec) / (1000*1000*1000);
	return result;
}

void ProgressImpl::addChunk(size_t size)
{
	FetchedChunk chunk;
	chunk.size = size;

	auto currentTimeSpec = getCurrentTimeSpec();
	chunk.timeSpec = currentTimeSpec;
	fetchedChunks.push_back(std::move(chunk));

	// cleaning old chunks
	FORIT(it, fetchedChunks)
	{
		if (getTimeSpecDiff(it->timeSpec, currentTimeSpec) < download::Progress::speedCalculatingAccuracy)
		{
			fetchedChunks.erase(fetchedChunks.begin(), it);
			break;
		}
	}
}

size_t ProgressImpl::getDownloadSpeed() const
{
	auto currentTimeSpec = getCurrentTimeSpec();

	auto it = fetchedChunks.begin();
	for(; it != fetchedChunks.end(); ++it)
	{
		if (getTimeSpecDiff(it->timeSpec, currentTimeSpec) < download::Progress::speedCalculatingAccuracy)
		{
			break;
		}
	}
	size_t fetchedBytes = 0;
	for(; it != fetchedChunks.end(); ++it)
	{
		fetchedBytes += it->size;
	}

	return fetchedBytes / download::Progress::speedCalculatingAccuracy;
}

}

namespace download {

float Progress::speedCalculatingAccuracy = 16;

Progress::Progress()
	: __impl(new internal::ProgressImpl)
{}

void Progress::setShortAliasForUri(const string& uri, const string& alias)
{
	__impl->aliases[uri].shortAlias = alias;
}

void Progress::setLongAliasForUri(const string& uri, const string& alias)
{
	__impl->aliases[uri].longAlias = alias;
}

Progress::~Progress()
{
	delete __impl;
}

string Progress::getLongAliasForUri(const string& uri) const
{
	auto it = __impl->aliases.find(uri);
	if (it != __impl->aliases.end())
	{
		return it->second.longAlias;
	}
	else
	{
		return uri;
	}
}

string Progress::getShortAliasForUri(const string& uri) const
{
	auto it = __impl->aliases.find(uri);
	if (it != __impl->aliases.end())
	{
		return it->second.shortAlias;
	}
	else
	{
		return uri;
	}
}

void Progress::setTotalEstimatedSize(uint64_t size)
{
	__impl->totalEstimatedSize = size;
}

void Progress::progress(const vector< string >& params)
{
	if (params.size() == 1 && params[0] == "finish")
	{
		finishHook();
		return;
	}
	if (params.size() < 2)
	{
		fatal2(__("download progress: received a progress message with less than 2 total parameters"));
	}
	const string& uri = params[0];
	const string& action = params[1];

	if (action == "ping")
	{
		updateHook(false);
	}
	else if (action == "start")
	{
		if (params.size() > 3)
		{
			fatal2(__("download progress: received a submessage '%s' with more than %u parameters"), "start", 1);
		}
		// new download
		DownloadRecord& record = __impl->nowDownloading[uri];
		record.number = __impl->nextDownloadNumber++;
		if (params.size() == 3)
		{
			record.size = lexical_cast< size_t >(params[2]);
		}
		else
		{
			record.size = -1;
		}
		record.downloadedSize = 0;
		record.beingPostprocessed = false;

		newDownloadHook(uri, record);
		updateHook(true);
	}
	else
	{
		// this is info about something that currently downloading
		auto recordIt = __impl->nowDownloading.find(uri);
		if (recordIt == __impl->nowDownloading.end())
		{
			fatal2(__("download progress: received an info for a not started download, uri '%s'"), uri);
		}
		DownloadRecord& record = recordIt->second;
		if (action == "downloading")
		{
			if (params.size() != 4)
			{
				fatal2(__("download progress: received a submessage '%s' with more than %u parameters"), "downloading", 2);
			}
			record.downloadedSize = lexical_cast< size_t >(params[2]);
			auto bytesInFetchedPiece = lexical_cast< size_t >(params[3]);
			__impl->fetchedSize += bytesInFetchedPiece;
			__impl->addChunk(bytesInFetchedPiece);
			updateHook(false);
		}
		else if (action == "expected-size")
		{
			if (params.size() != 3)
			{
				fatal2(__("download progress: received a submessage '%s' with more than %u parameters"), "expected-size", 1);
			}
			record.size = lexical_cast< size_t >(params[2]);
			updateHook(true);
		}
		else if (action == "pre-done")
		{
			if (params.size() != 2)
			{
				fatal2(__("download progress: received a submessage '%s' with more than %u parameters"), "pre-done", 0);
			}
			record.beingPostprocessed = true;
			updateHook(true);
		}
		else if (action == "done")
		{
			if (params.size() != 3)
			{
				fatal2(__("download progress: received a submessage '%s' with more than %u parameters"), "done", 1);
			}
			const string& result = params[2];
			if (result.empty()) // only if download succeeded
			{
				auto value = (record.size != (size_t)-1 ? record.size : record.downloadedSize);
				__impl->doneDownloadsSize += value;
			}
			finishedDownloadHook(uri, result);
			__impl->nowDownloading.erase(recordIt);
			updateHook(true);
		}
		else
		{
			fatal2(__("download progress: received the wrong action '%s'"), action);
		}
	}
}

const std::map< string, Progress::DownloadRecord >& Progress::getDownloadRecords() const
{
	return __impl->nowDownloading;
}

uint64_t Progress::getOverallDownloadedSize() const
{
	// firstly, start up with filling size of already downloaded things
	uint64_t result = __impl->doneDownloadsSize;
	// count each amount bytes download for all active entries
	FORIT(it, __impl->nowDownloading)
	{
		result += it->second.downloadedSize;
	}

	return result;
}

uint64_t Progress::getOverallEstimatedSize() const
{
	if (__impl->totalEstimatedSize != (uint64_t)-1)
	{
		// caller has specified the estimated size, just use it
		return __impl->totalEstimatedSize;
	}
	else
	{
		// otherwise compute it based on data we have
		auto result = __impl->doneDownloadsSize;
		FORIT(it, __impl->nowDownloading)
		{
			// add or real estimated size, or downloaded size (for entries
			// where download size hasn't been determined yet)
			auto size = it->second.size;
			if (size == (size_t)-1)
			{
				size = it->second.downloadedSize;
			}
			result += size;
		}
		return result;
	}
}

uint64_t Progress::getOverallFetchedSize() const
{
	return __impl->fetchedSize;
}

size_t Progress::getOverallEstimatedTime() const
{
	auto estimatedSize = getOverallEstimatedSize();
	float overallPart = estimatedSize ? ((float)getOverallDownloadedSize() / estimatedSize) : 0.0;
	if (overallPart < 0.001)
	{
		overallPart = 0.001;
	}
	auto currentTimestamp = time(NULL);
	return (currentTimestamp - __impl->startTimestamp) / overallPart;
}

size_t Progress::getOverallDownloadTime() const
{
	return time(NULL) - __impl->startTimestamp;
}

size_t Progress::getDownloadSpeed() const
{
	return __impl->getDownloadSpeed();
}

void Progress::updateHook(bool)
{}

void Progress::newDownloadHook(const string&, const DownloadRecord&)
{}

void Progress::finishedDownloadHook(const string&, const string&)
{}

void Progress::finishHook()
{}

}
}

