/**************************************************************************
*   Copyright (C) 2010-2012 by Eugene V. Lyubimkin                        *
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
#include <cstring>

#include <boost/lexical_cast.hpp>
using boost::lexical_cast;

#include <curl/curl.h>

#include <cupt/config.hpp>
#include <cupt/file.hpp>
#include <cupt/download/method.hpp>
#include <cupt/download/uri.hpp>

namespace cupt {

class CurlWrapper
{
	CURL* __handle;
	char __error_buffer[CURL_ERROR_SIZE];
 public:
	CurlWrapper()
	{
		memset(__error_buffer, 0, sizeof(__error_buffer));
		__handle = curl_easy_init();
		if (!__handle)
		{
			fatal2(__("unable to create a Curl handle"));
		}
		__init();
	}
	void setOption(CURLoption optionName, long value, const char* alias)
	{
		auto returnCode = curl_easy_setopt(__handle, optionName, value);
		if (returnCode != CURLE_OK)
		{
			fatal2(__("unable to set the Curl option '%s': curl_easy_setopt failed: %s"),
					alias, curl_easy_strerror(returnCode));
		}
	}
	void setOption(CURLoption optionName, void* value, const char* alias)
	{
		auto returnCode = curl_easy_setopt(__handle, optionName, value);
		if (returnCode != CURLE_OK)
		{
			fatal2(__("unable to set the Curl option '%s': curl_easy_setopt failed: %s"),
					alias, curl_easy_strerror(returnCode));
		}
	}
	void setOption(CURLoption optionName, const string& value, const char* alias)
	{
		auto returnCode = curl_easy_setopt(__handle, optionName, value.c_str());
		if (returnCode != CURLE_OK)
		{
			fatal2(__("unable to set the Curl option '%s': curl_easy_setopt failed: %s"),
					alias, curl_easy_strerror(returnCode));
		}
	}
	void setLargeOption(CURLoption optionName, curl_off_t value, const char* alias)
	{
		auto returnCode = curl_easy_setopt(__handle, optionName, value);
		if (returnCode != CURLE_OK)
		{
			fatal2(__("unable to set the Curl option '%s': curl_easy_setopt failed: %s"),
					alias, curl_easy_strerror(returnCode));
		}
	}
	void __init()
	{
		setOption(CURLOPT_FAILONERROR, 1, "fail on error");
		setOption(CURLOPT_NETRC, CURL_NETRC_OPTIONAL, "netrc");
		setOption(CURLOPT_USERAGENT, format2("Curl (libcupt/%s)", cupt::libraryVersion), "user-agent");
		curl_easy_setopt(__handle, CURLOPT_ERRORBUFFER, __error_buffer);
	}
	ssize_t getExpectedDownloadSize() const
	{
		double value;
		curl_easy_getinfo(__handle, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &value);
		return value;
	}
	CURLcode perform()
	{
		return curl_easy_perform(__handle);
	}
	string getError() const
	{
		return string(__error_buffer);
	}
	~CurlWrapper()
	{
		curl_easy_cleanup(__handle);
	}
};

string* fileWriteErrorPtr;
File* filePtr;
CurlWrapper* curlPtr;
const std::function< void (const vector< string >&) >* callbackPtr;
ssize_t* totalBytesPtr;

extern "C"
{
	size_t curlWriteFunction(void* data, size_t size, size_t nmemb, void*)
	{
		size *= nmemb;

		if (!size)
		{
			return size; // empty file
		}

		// writing data to file
		try
		{
			filePtr->put((const char*)data, size);
		}
		catch (Exception& e)
		{
			*fileWriteErrorPtr = e.what();
			return 0;
		}

		static bool firstChunk = true;
		if (firstChunk)
		{
			firstChunk = false;
			auto expectedSize = curlPtr->getExpectedDownloadSize();
			if (expectedSize > 0)
			{
				(*callbackPtr)(vector< string >{ "expected-size",
						lexical_cast< string >(expectedSize + *totalBytesPtr) });
			}
		}

		*totalBytesPtr += size;
		(*callbackPtr)(vector< string >{ "downloading",
				lexical_cast< string >(*totalBytesPtr), lexical_cast< string >(size) });

		return size;
	}
}

class CurlMethod: public cupt::download::Method
{
	string perform(const shared_ptr< const Config >& config, const download::Uri& uri,
			const string& targetPath, const std::function< void (const vector< string >&) >& callback)
	{
		try
		{
			CurlWrapper curl;
			// bad connections can return 'receive failure' transient error
			// occasionally, give them several tries to finish the download
			auto transientErrorsLeft = config->getInteger("acquire::retries");

			{ // setting options
				curl.setOption(CURLOPT_URL, string(uri), "uri");
				auto downloadLimit = getIntegerAcquireSuboptionForUri(config, uri, "dl-limit");
				if (downloadLimit)
				{
					curl.setLargeOption(CURLOPT_MAX_RECV_SPEED_LARGE, downloadLimit*1024, "upper speed limit");
				}
				auto proxy = getAcquireSuboptionForUri(config, uri, "proxy");
				if (proxy == "DIRECT")
				{
					curl.setOption(CURLOPT_PROXY, "", "proxy");
				}
				else if (!proxy.empty())
				{
					curl.setOption(CURLOPT_PROXY, proxy, "proxy");
				}
				if (uri.getProtocol() == "http" && config->getBool("acquire::http::allowredirect"))
				{
					curl.setOption(CURLOPT_FOLLOWLOCATION, 1, "follow-location");
				}
				auto timeout = getIntegerAcquireSuboptionForUri(config, uri, "timeout");
				if (timeout)
				{
					curl.setOption(CURLOPT_CONNECTTIMEOUT, timeout, "connect timeout");
					curl.setOption(CURLOPT_LOW_SPEED_LIMIT, 1, "low speed limit");
					curl.setOption(CURLOPT_LOW_SPEED_TIME, timeout, "low speed timeout");
				}
				curl.setOption(CURLOPT_WRITEFUNCTION, (void*)&curlWriteFunction, "write function");
			}

			RequiredFile file(targetPath, "a");

			start:
			ssize_t totalBytes = file.tell();
			callback(vector< string > { "downloading",
					lexical_cast< string >(totalBytes), lexical_cast< string >(0)});
			curl.setOption(CURLOPT_RESUME_FROM, totalBytes, "resume from");

			string fileWriteError;

			{
				fileWriteErrorPtr = &fileWriteError;
				filePtr = &file;
				curlPtr = &curl;
				callbackPtr = &callback;
				totalBytesPtr = &totalBytes;
			}

			auto performResult = curl.perform();

			if (!fileWriteError.empty())
			{
				return fileWriteError;
			}
			else if (performResult == CURLE_OK)
			{
				return string(); // all went ok
			}
			else if (performResult == CURLE_PARTIAL_FILE)
			{
				// partial data? no problem, we might request it
				return string();
			}
			else
			{
				// something went wrong :(

				// transient errors handling
				if (performResult == CURLE_RECV_ERROR && transientErrorsLeft)
				{
					if (config->getBool("debug::downloader"))
					{
						debug2("transient error while downloading '%s'", string(uri));
					}
					--transientErrorsLeft;
					goto start;
				}

				if (performResult == CURLE_RANGE_ERROR)
				{
					if (config->getBool("debug::downloader"))
					{
						debug2("range command failed, need to restart from beginning while downloading '%s'", string(uri));
					}
					if (unlink(targetPath.c_str()) == -1)
					{
						return format2e(__("unable to remove target file for re-downloading"));
					}
					goto start;
				}

				return curl.getError();
			}
		}
		catch (Exception& e)
		{
			return format2(__("download method error: %s"), e.what());
		}
	}
};

}

extern "C"
{
	cupt::download::Method* construct()
	{
		return new cupt::CurlMethod();
	}
}
