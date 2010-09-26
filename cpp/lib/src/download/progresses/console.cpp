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
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include <iostream>
#include <algorithm>

#include <boost/lexical_cast.hpp>

#include <cupt/common.hpp>
#include <cupt/download/progresses/console.hpp>

namespace cupt {

using boost::lexical_cast;

typedef download::Progress::DownloadRecord DownloadRecord;

struct DownloadRecordForPrint
{
	DownloadRecord record;
	string uri;
	string shortAlias;
};

namespace internal {

class ConsoleProgressImpl
{
	time_t previousUpdateTimestamp;

	static void nonBlockingPrint(const string&);
	static uint16_t getTerminalWidth();
	static void termClean();
	static void termPrint(const string&, const string&);
 public:
	ConsoleProgressImpl();
	void newDownload(const DownloadRecord& record, const string& longAlias);
	void finishedDownload(const string& uri, const string& result, const string& longAlias);
	void finish(uint64_t size, size_t time);

	bool isUpdateNeeded(bool immediate);
	void updateView(vector< DownloadRecordForPrint >, uint8_t downloadPercent,
			size_t overallEstimatedTime, size_t speed);
};

uint16_t ConsoleProgressImpl::getTerminalWidth()
{
	struct winsize w;
	if (!ioctl(STDERR_FILENO, TIOCGWINSZ, &w))
	{
		return w.ws_col;
	}
	else
	{
		// something went wrong with it...
		return 80;
	}
}

ConsoleProgressImpl::ConsoleProgressImpl()
	: previousUpdateTimestamp(0)
{}

void ConsoleProgressImpl::nonBlockingPrint(const string& s)
{
	auto oldStatus = fcntl(STDERR_FILENO, F_GETFL);
	bool statusIsModified = false;
	if (oldStatus == -1)
	{
		warn("unable to get standard error stream status flags: fcntl failed: EEE");
	}
	else if (fcntl(STDERR_FILENO, F_SETFL, (long)oldStatus | O_NONBLOCK) == -1)
	{
		warn("unable to make standard error stream non-blocking: fcntl failed: EEE");
	}
	else
	{
		statusIsModified = true;
	}

	write(STDERR_FILENO, s.c_str(), s.size());

	if (statusIsModified)
	{
		if (fcntl(STDERR_FILENO, F_SETFL, (long)oldStatus) == -1)
		{
			warn("unable to make standard error stream blocking again: fcntl failed: EEE");
		}
	}
}

void ConsoleProgressImpl::termClean()
{
	nonBlockingPrint(string(getTerminalWidth(), ' ') + "\r");
}

void ConsoleProgressImpl::termPrint(const string& s, const string& rightAppendage)
{
	auto allowedWidth = getTerminalWidth() - rightAppendage.size();
	string outputString = "\r";
	if (s.size() > allowedWidth)
	{
		outputString += s.substr(0, allowedWidth);
	}
	else
	{
		outputString += s + string(allowedWidth - s.size(), ' ');
	}
	outputString += rightAppendage;
	outputString += "\r";
	nonBlockingPrint(outputString);
}

void ConsoleProgressImpl::newDownload(const DownloadRecord& record, const string& longAlias)
{
	string sizeSuffix;
	if (record.size != (size_t)-1)
	{
		sizeSuffix = string(" [") + humanReadableSizeString(record.size) + "]";
	}
	termClean();
	nonBlockingPrint(__("Get") + ":" + lexical_cast< string >(record.number) + " " +
			longAlias + sizeSuffix + "\n");
}

void ConsoleProgressImpl::finishedDownload(const string& uri,
		const string& result, const string& longAlias)
{
	if (!result.empty())
	{
		// some error occured, output it
		termClean();
		nonBlockingPrint(sf("W: downloading '%s' (uri '%s') failed: %s\n",
				longAlias.c_str(), uri.c_str(), result.c_str()));
	}
}

bool ConsoleProgressImpl::isUpdateNeeded(bool immediate)
{
	if (!immediate)
	{
		auto timestamp = time(NULL);
		if (timestamp != previousUpdateTimestamp)
		{
			previousUpdateTimestamp = timestamp;
		}
		else
		{
			// don't renew stats too often just for download totals
			return false;
		}
	}

	// don't print progress meter when stderr not connected to a TTY
	return isatty(STDERR_FILENO);
}

string humanReadableDifftimeString(size_t time)
{
	auto days = time / (24*60*60);
	time %= (24*60*60);
	auto hours = time / (60*60);
	time %= (60*60);
	auto minutes = time / 60;
	auto seconds = time % 60;

	auto dayString = days < 1 ? "" : lexical_cast< string >(days) + "d";
	auto hourString = hours < 1 && dayString.empty() ? "" : lexical_cast< string >(hours) + "h";
	auto minuteString = minutes < 1 && hourString.empty() ? "" : lexical_cast< string >(minutes) + "m";
	auto secondString = lexical_cast< string >(seconds) + "s";

	return dayString + hourString + minuteString + secondString;
}

string humanReadableSpeedString(size_t speed)
{
	return humanReadableSizeString(speed) + "/s";
}

void ConsoleProgressImpl::updateView(vector< DownloadRecordForPrint > records,
		uint8_t overallDownloadPercent, size_t overallEstimatedTime, size_t speed)
{
	// print 'em all!

	// sort by download numbers
	std::sort(records.begin(), records.end(),
			[](const DownloadRecordForPrint& left, const DownloadRecordForPrint& right)
			{
				return left.record.number < right.record.number;
			});

	string viewString = sf("%d%% ", overallDownloadPercent);

	FORIT(it, records)
	{
		string suffix;
		if (it->record.beingPostprocessed)
		{
			suffix = " | postprocessing";
		}
		else if (it->record.size != (size_t)-1 && it->record.size != 0 /* no sense for empty files */)
		{
			suffix = sf("/%s %.0f%%", humanReadableSizeString(it->record.size).c_str(),
					(float)it->record.downloadedSize / it->record.size * 100);
		}
		viewString += sf("[%u %s %s%s]", it->record.number, it->shortAlias.c_str(),
				humanReadableSizeString(it->record.downloadedSize).c_str(), suffix.c_str());
	}
	auto speedAndTimeAppendage = string("| ") + humanReadableSpeedString(speed) +
			string(" | ETA: ") + humanReadableDifftimeString(overallEstimatedTime);
	termPrint(viewString, speedAndTimeAppendage);
}

void ConsoleProgressImpl::finish(uint64_t size, size_t time)
{
	termClean();
	nonBlockingPrint(sf(__("Fetched %s in %s.\n"),
			humanReadableSizeString(size).c_str(), humanReadableDifftimeString(time).c_str()));
}

}

namespace download {

ConsoleProgress::ConsoleProgress()
	: __impl(new internal::ConsoleProgressImpl)
{}

ConsoleProgress::~ConsoleProgress()
{
	delete __impl;
}

void ConsoleProgress::newDownloadHook(const string& uri, const DownloadRecord& record)
{
	__impl->newDownload(record, getLongAliasForUri(uri));
}

void ConsoleProgress::updateHook(bool immediate)
{
	if (!__impl->isUpdateNeeded(immediate))
	{
		return;
	}

	vector< DownloadRecordForPrint > printRecords;
	const std::map< string, DownloadRecord >& records = this->getDownloadRecords();
	FORIT(recordIt, records)
	{
		DownloadRecordForPrint recordForPrint;
		recordForPrint.record = recordIt->second;
		recordForPrint.uri = recordIt->first;
		recordForPrint.shortAlias = this->getShortAliasForUri(recordForPrint.uri);
		printRecords.push_back(std::move(recordForPrint));
	}

	auto estimatedSize = getOverallEstimatedSize();
	uint8_t overallPercent = estimatedSize ? (getOverallDownloadedSize() * 100 / estimatedSize) : 0;
	__impl->updateView(printRecords, overallPercent, getOverallEstimatedTime(), getDownloadSpeed());
}

void ConsoleProgress::finishedDownloadHook(const string& uri, const string& result)
{
	__impl->finishedDownload(uri, result, getLongAliasForUri(uri));
}

void ConsoleProgress::finish()
{
	__impl->finish(getOverallFetchedSize(), getOverallDownloadTime());
}

}
}

