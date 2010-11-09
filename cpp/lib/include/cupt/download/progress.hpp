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
	/// download element
	struct DownloadRecord
	{
		size_t number; ///< unique number
		size_t downloadedSize; ///< already downloaded amount of bytes
		size_t size; ///< expected file size, @c -1 if unknown
		bool beingPostprocessed; ///< is download being postprocessed
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
	virtual void finishHook();

 public:
	/// constructor
	Progress();

	/// amount of seconds considered while calculating a download speed
	/**
	 * Default: 16
	 */
	static float speedCalculatingAccuracy;

	/// sets a short alias for URI
	/**
	 * @param uri uri
	 * @param alias short alias
	 */
	void setShortAliasForUri(const string& uri, const string& alias);
	/// sets a long alias for URI
	/**
	 * @param uri uri
	 * @param alias long alias
	 */
	void setLongAliasForUri(const string& uri, const string& alias);
	/// sets total download size for the all download progress lifetime
	/**
	 * This method should be called if this amount is known beforehand to get
	 * better overall progress indication.
	 */
	void setTotalEstimatedSize(uint64_t size);

	/// @cond
	void progress(const vector< string >& params);
	/// @endcond

	/// destructor
	virtual ~Progress();
};

}
}

#endif

