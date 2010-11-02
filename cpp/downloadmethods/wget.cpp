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
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/stat.h>

#include <cstring>

#include <boost/lexical_cast.hpp>
using boost::lexical_cast;

#include <cupt/config.hpp>
#include <cupt/download/method.hpp>
#include <cupt/download/uri.hpp>
#include <cupt/pipe.hpp>

namespace cupt {

static int childPid = 0;
struct sigaction oldSigAction;

void sigTermHandler(int)
{
	if (childPid != 0)
	{
		kill(childPid, SIGTERM); // with error checking, yeah
	}
	raise(SIGTERM); // terminate itself
}

void enableSigTermHandler()
{
	struct sigaction action;
	memset(&action, sizeof(action), 0);
	action.sa_handler = sigTermHandler;
	if (sigemptyset(&action.sa_mask) == -1)
	{
		fatal("sigemptyset failed: EEE");
	}
	action.sa_flags = SA_RESETHAND;
	if (sigaction(SIGTERM, &action, &oldSigAction) == -1)
	{
		fatal("wget download method: unable to setup SIGTERM handler: sigaction failed: EEE");
	}
}

void disableSigTermHanlder()
{
	if (sigaction(SIGTERM, &oldSigAction, NULL) == -1)
	{
		fatal("wget download method: unable to reset SIGTERM handler: sigaction failed: EEE");
	}
}

class WgetMethod: public cupt::download::Method
{
	string perform(const shared_ptr< const Config >& config, const download::Uri& uri,
			const string& targetPath, const std::function< void (const vector< string >&) >& callback)
	{
		enableSigTermHandler();

		Pipe wgetErrorStream("wget error stream");
		auto workerPid = fork();
		if (workerPid == -1)
		{
			fatal("unable to fork: EEE");
		}
		if (workerPid)
		{
			childPid = workerPid;
			// main process
			string errorString;
			wgetErrorStream.useAsReader();
			FILE* inputHandle = fdopen(wgetErrorStream.getReaderFd(), "r");
			if (!inputHandle)
			{
				fatal("unable to fdopen wget error stream: EEE"); 
			}
			char buf[1024];
			while (fgets(buf, sizeof(buf), inputHandle), !feof(inputHandle))
			{
				errorString += buf;
			}
			int workerStatus;
			if (waitpid(workerPid, &workerStatus, 0) == -1)
			{
				fatal("waitpid failed: EEE");
			}
			if (!WIFEXITED(workerStatus))
			{
				fatal("wget wrapper process exited abnormally");
			}
			disableSigTermHanlder(); // we about to exit, restore it
			if (WEXITSTATUS(workerStatus) != 0)
			{
				return errorString;
			}
			else
			{
				return "";
			}
		}
		else
		{
			// wrapper
			wgetErrorStream.useAsWriter();
			try
			{
				ssize_t totalBytes = 0;
				{
					struct stat st;
					if (lstat(targetPath.c_str(), &st) == -1)
					{
						if (errno != ENOENT)
						{
							fatal("stat on file '%s' failed: EEE", targetPath.c_str());
						}
					}
					else
					{
						totalBytes = st.st_size;
						callback(vector< string > { "downloading",
								lexical_cast< string >(totalBytes), lexical_cast< string >(0)});
					}
				}

				auto wgetPid = fork();
				if (wgetPid == -1)
				{
					fatal("unable to fork: EEE");
				}
				if (wgetPid)
				{
					childPid = wgetPid;
					// still wrapper
					int waitResult;
					int childStatus;
					struct timespec ts;
					ts.tv_sec = 0;
					ts.tv_nsec = 100 * 1000 * 1000; // 100 milliseconds
					while (waitResult = waitpid(wgetPid, &childStatus, WNOHANG), !waitResult)
					{
						if (nanosleep(&ts, NULL) == -1)
						{
							if (errno != EINTR)
							{
								fatal("nanosleep failed: EEE");
							}
						}
						struct stat st;
						if (lstat(targetPath.c_str(), &st) == -1)
						{
							if (errno != ENOENT) // wget haven't created the file yet
							{
								fatal("stat on file '%s' failed: EEE", targetPath.c_str());
							}
						}
						else
						{
							auto newTotalBytes = st.st_size;
							if (newTotalBytes != totalBytes)
							{
								callback(vector< string >{ "downloading",
										lexical_cast< string >(newTotalBytes),
										lexical_cast< string >(newTotalBytes - totalBytes) });
								totalBytes = newTotalBytes;
							}
						}
					}
					if (waitResult == -1)
					{
						fatal("waitpid failed: EEE");
					}
					if (!WIFEXITED(childStatus) || WEXITSTATUS(childStatus) != 0)
					{
						_exit(EXIT_FAILURE);
					}
				}
				else
				{
					// wget executor
					vector< char* > envp;
					vector< string > p; // temporary array to put parameters
					{
						p.push_back("wget"); // passed as a binary name, not parameter
						p.push_back("--continue");
						p.push_back(string("--tries=") + lexical_cast< string >(config->getInteger("acquire::retries")+1));
						auto maxSpeedLimit = getIntegerAcquireSuboptionForUri(config, uri, "dl-limit");
						if (maxSpeedLimit)
						{
							p.push_back(string("--limit-rate=") + lexical_cast< string >(maxSpeedLimit) + "k");
						}
						auto proxy = getAcquireSuboptionForUri(config, uri, "proxy");
						if (proxy == "DIRECT")
						{
							p.push_back("--no-proxy");
						}
						else if (!proxy.empty())
						{
							auto argument = uri.getProtocol() + "_proxy=" + proxy;
							envp.push_back(strdup(argument.c_str()));
						}
						if (uri.getProtocol() != "http" || !config->getBool("acquire::http::allow-redirects"))
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
					}

					vector< char* > params;
					FORIT(it, p)
					{
						params.push_back(strdup(it->c_str()));
					}
					params.push_back(NULL);
					envp.push_back(NULL);

					if (dup2(wgetErrorStream.getWriterFd(), STDOUT_FILENO) == -1) // redirecting stdout
					{
						fatal("unable to redirect wget standard output: dup2 failed: EEE");
					}
					if (dup2(wgetErrorStream.getWriterFd(), STDERR_FILENO) == -1) // redirecting stderr
					{
						fatal("unable to redirect wget error stream: dup2 failed: EEE");
					}
					execve("/usr/bin/wget", &params[0], &envp[0]);
					// if we are here, exec returned an error
					fatal("unable to launch wget process: EEE");
				}
			}
			catch (Exception& e)
			{
				char nonWgetError[] = "download method error: ";
				write(wgetErrorStream.getWriterFd(), nonWgetError, sizeof(nonWgetError) - 1);
				write(wgetErrorStream.getWriterFd(), e.what(), strlen(e.what()));
				write(wgetErrorStream.getWriterFd(), "\n", 1);
				exit(EXIT_FAILURE);
			}
			exit(0);
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
