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
#ifndef CUPT_DOWNLOAD_PROGRESS_SEEN
#define CUPT_DOWNLOAD_PROGRESS_SEEN

/// @file

#include <ctime>
#include <map>

#include <cupt/common.hpp>

namespace cupt {

namespace internal {

class ProgressImpl;

}

namespace download {

/// download progress meter
class Progress
{
	internal::ProgressImpl* __impl;
 public:
	struct DownloadRecord
	{
		size_t number;
		size_t downloadedSize;
		size_t size;
		bool beingPostprocessed;
	};
 protected:
	string getLongAliasForUri(const string& uri) const;
	string getShortAliasForUri(const string& uri) const;
	const std::map< string, DownloadRecord >& getDownloadRecords() const;
	uint64_t getOverallDownloadedSize() const;
	uint64_t getOverallEstimatedSize() const;
	uint64_t getOverallFetchedSize() const;
	size_t getOverallDownloadTime() const;
	size_t getOverallEstimatedTime() const;
	size_t getDownloadSpeed() const;

	virtual void newDownloadHook(const string& uri, const DownloadRecord&);
	virtual void finishedDownloadHook(const string& uri, const string& result);
	virtual void updateHook(bool immediate);

 public:
	Progress();

	static float speedCalculatingAccuracy;

	void setShortAliasForUri(const string& uri, const string& alias);
	void setLongAliasForUri(const string& uri, const string& alias);
	void setTotalEstimatedSize(uint64_t size);

	void progress(const vector< string >& params);
	virtual void finish();

	virtual ~Progress();
};

}
}

#endif

