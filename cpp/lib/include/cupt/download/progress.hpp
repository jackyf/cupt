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
	/**
	 * @param uri
	 * @return long alias for @a uri if it was specified, @a uri otherwise
	 */
	string getLongAliasForUri(const string& uri) const;
	/**
	 * @param uri
	 * @return short alias for @a uri if it was specified, @a uri otherwise
	 */
	string getShortAliasForUri(const string& uri) const;
	/**
	 * Gets current downloads.
	 *
	 * @return map of uris to download records.
	 */
	const std::map< string, DownloadRecord >& getDownloadRecords() const;
	/**
	 * @return the sum of already done downloads in the current session plus
	 * the sum of downloaded parts of running downloads
	 */
	uint64_t getOverallDownloadedSize() const;
	/**
	 * Overall estimated size is guaranteed to be not less than @ref
	 * getOverallDownloadedSize.
	 *
	 * @return total estimated size counting both done and running downloads,
	 * and predefined size if it was set by @ref setTotalEstimatedSize before
	 */
	uint64_t getOverallEstimatedSize() const;
	/**
	 * @return total byte count of all data chunks fetched from the network (or
	 * its equivalent)
	 */
	uint64_t getOverallFetchedSize() const;
	/**
	 * @return number of seconds since the start of the download session
	 */
	size_t getOverallDownloadTime() const;
	/**
	 * @return number of seconds, estimated time to finish since the start of
	 * the download session
	 */
	size_t getOverallEstimatedTime() const;
	/**
	 * @return current download speed in bytes/second
	 */
	size_t getDownloadSpeed() const;

	/**
	 * This hook is called when new download starts.
	 *
	 * @param uri
	 * @param downloadRecord
	 */
	virtual void newDownloadHook(const string& uri, const DownloadRecord& downloadRecord);
	/**
	 * This hook is called when some download is finished.
	 *
	 * @param uri
	 * @param result download exit code, empty string is success, non-empty
	 * string is human-readable download error message
	 */
	virtual void finishedDownloadHook(const string& uri, const string& result);
	/**
	 * This hook is called whenever some download information is updated
	 * (including being called after @ref newDownloadHook and @ref
	 * finishedDownloadHook).
	 *
	 * @param immediate is update important or not; examples of important
	 * updates: new download, finished download, changes of a download state;
	 * examples of unimportant updates: number of download bytes changes for
	 * some download
	 */
	virtual void updateHook(bool immediate);
	/**
	 * This hook is called before the end of the download session.
	 */
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
	 * @param uri
	 * @param alias short alias
	 */
	void setShortAliasForUri(const string& uri, const string& alias);
	/// sets a long alias for URI
	/**
	 * @param uri
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

