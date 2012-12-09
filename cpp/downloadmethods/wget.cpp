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
#include <sys/stat.h>

#include <thread>
#include <atomic>
#include <chrono>

#include <boost/lexical_cast.hpp>
using boost::lexical_cast;

#include <cupt/config.hpp>
#include <cupt/download/method.hpp>
#include <cupt/download/uri.hpp>
#include <cupt/file.hpp>

namespace cupt {

// returns true if succeeded
static bool __get_file_size(const string& path, ssize_t* result)
{
	struct stat st;
	if (lstat(path.c_str(), &st) == -1)
	{
		if (errno != ENOENT)
		{
			fatal2e(__("%s() failed: '%s'"), "lstat", path);
		}
		return false;
	}
	else
	{
		*result = st.st_size;
		return true;
	}
}

class WgetMethod: public cupt::download::Method
{
	string perform(const shared_ptr< const Config >& config, const download::Uri& uri,
			const string& targetPath, const std::function< void (const vector< string >&) >& callback)
	{
		bool wgetProcessFinished = false;
		std::condition_variable wgetProcessFinishedCV;
		std::mutex wgetProcessFinishedMutex;

		try
		{
			ssize_t totalBytes = 0;
			if (__get_file_size(targetPath, &totalBytes))
			{
				callback(vector< string > { "downloading",
						lexical_cast< string >(totalBytes), lexical_cast< string >(0)});
			}

			// wget executor
			vector< string > p; // array to put parameters
			{
				p.push_back("env");
				auto proxy = getAcquireSuboptionForUri(config, uri, "proxy");
				if (!proxy.empty() && proxy != "DIRECT")
				{
					p.push_back(uri.getProtocol() + "_proxy=" + proxy);
				}
				p.push_back("wget"); // passed as a binary name, not parameter
				p.push_back("--continue");
				p.push_back(string("--tries=") + lexical_cast< string >(config->getInteger("acquire::retries")+1));
				auto maxSpeedLimit = getIntegerAcquireSuboptionForUri(config, uri, "dl-limit");
				if (maxSpeedLimit)
				{
					p.push_back(string("--limit-rate=") + lexical_cast< string >(maxSpeedLimit) + "k");
				}
				if (proxy == "DIRECT")
				{
					p.push_back("--no-proxy");
				}
				if (uri.getProtocol() != "http" || !config->getBool("acquire::http::allowredirect"))
				{
					p.push_back("--max-redirect=0");
				}
				auto timeout = getIntegerAcquireSuboptionForUri(config, uri, "timeout");
				if (timeout)
				{
					p.push_back(string("--timeout=") + lexical_cast< string >(timeout));
				}
				p.push_back(string(uri));
				p.push_back(string("--output-document=") + targetPath);
				p.push_back(format2("--user-agent=\"Wget (libcupt/%s)\"", cupt::libraryVersion));
				p.push_back("2>&1");
			}

			std::thread downloadingStatsThread([&targetPath, &totalBytes, &callback,
					&wgetProcessFinishedMutex, &wgetProcessFinishedCV, &wgetProcessFinished]()
			{
				std::unique_lock< std::mutex > conditionMutexLock(wgetProcessFinishedMutex);
				while (!wgetProcessFinishedCV.wait_for(conditionMutexLock, std::chrono::milliseconds(100),
						[&wgetProcessFinished](){ return wgetProcessFinished; }))
				{
					decltype(totalBytes) newTotalBytes;
					if (__get_file_size(targetPath, &newTotalBytes))
					{
						if (newTotalBytes != totalBytes)
						{
							callback(vector< string >{ "downloading",
									lexical_cast< string >(newTotalBytes),
									lexical_cast< string >(newTotalBytes - totalBytes) });
							totalBytes = newTotalBytes;
						}
					}
				}
			});

			string errorString;
			auto oldMessageFd = cupt::messageFd; // disable printing errors
			cupt::messageFd = -1;
			try
			{
				string openError;
				File wgetOutputFile(join(" ", p), "pr", openError);
				if (!openError.empty())
				{
					fatal2(__("unable to launch a wget process: %s"), openError);
				}

				string line;
				while (wgetOutputFile.getLine(line), !wgetOutputFile.eof())
				{
					errorString += line;
					errorString += '\n';
				}

				{
					std::lock_guard< std::mutex > guard(wgetProcessFinishedMutex);
					wgetProcessFinished = true;
				}
				wgetProcessFinishedCV.notify_all();
				downloadingStatsThread.join();
			}
			catch (Exception&)
			{
				cupt::messageFd = oldMessageFd;
				return errorString;
			}

			cupt::messageFd = oldMessageFd;
			return "";
		}
		catch (Exception& e)
		{
			return format2("download method error: %s", e.what());
		}
	}
};

}

extern "C"
{
	cupt::download::Method* construct()
	{
		return new cupt::WgetMethod();
	}
}
