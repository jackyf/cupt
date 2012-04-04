/**************************************************************************
*   Copyright (C) 2010-2011 by Eugene V. Lyubimkin                        *
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
#include <algorithm>
#include <cstdio>
#include <sstream>
#include <queue>
#include <map>
#include <set>

#include <boost/lexical_cast.hpp>

#include <signal.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/un.h>

#include <cupt/config.hpp>
#include <cupt/download/manager.hpp>
#include <cupt/download/uri.hpp>
#include <cupt/download/progress.hpp>
#include <cupt/download/method.hpp>
#include <cupt/download/methodfactory.hpp>
#include <cupt/pipe.hpp>

#include <internal/common.hpp>

namespace cupt {
namespace internal {

using namespace cupt::download;

typedef Manager::ExtendedUri ExtendedUri;

struct InputMessage
{
	int sock;
	vector< string > message;
};

using std::queue;
using std::map;
using std::multimap;
using std::set;
using boost::lexical_cast;
using std::make_pair;

typedef queue< vector< string > > MessageQueue;

static void sendSocketMessage(int socket, const vector< string >& message)
{
	auto compactedMessage = join("\1", message);
	if (message.size() >= 0xFFFF)
	{
		fatal2i("sendSocketMessage: message size exceeded 64K");
	}
	uint16_t len = compactedMessage.size();

	if (write(socket, &len, sizeof(len)) == -1 ||
			write(socket, compactedMessage.c_str(), len) == -1)
	{
		fatal2e(__("unable to send a socket message"));
	}
}

static void sendSocketMessage(Pipe& pipe, const vector< string >& message)
{
	sendSocketMessage(pipe.getWriterFd(), message);
}

static vector< string > receiveSocketMessage(int socket)
{
	uint16_t len;
	auto readResult = read(socket, &len, sizeof(len));
	if (readResult == -1)
	{
		if (errno != ECONNRESET /* handled under */)
		{
			fatal2e(__("unable to receive a socket message length"));
		}
	}
	else if (readResult != sizeof(len) && readResult != 0)
	{
		fatal2(__("unable to receive a socket message length: %s"), __("partial message arrived"));
	}

	if (readResult == 0 || readResult == -1 /* connection reset */)
	{
		return { "eof" };
	}
	else
	{
		char buf[0xFFFF]; // must be enough for reading max(uint16) bytes
		readResult = read(socket, buf, len);
		if (readResult == -1)
		{
			fatal2e(__("unable to receive a socket message"));
		}
		else if (readResult == 0)
		{
			fatal2(__("unable to receive a socket message: %s"), __("unexpected end of stream"));
		}
		else if (readResult != len)
		{
			fatal2(__("unable to receive a socket message: %s"), __("partial message arrived"));
		}

		string compactedMessage(buf, len);
		return split('\1', compactedMessage, true);
	}
}

struct InnerDownloadElement
{
	queue< ExtendedUri > extendedUris;
	size_t size;
	std::function< string () > postAction;
};

class ManagerImpl
{
	shared_ptr< const Config > config;
	shared_ptr< Progress > progress;
	int serverSocket;
	string serverSocketPath;
	sockaddr_un serverSocketAddress;
	shared_ptr< Pipe > parentPipe;
	pid_t workerPid;
	MethodFactory methodFactory;

	// worker data
	map< string, string > done; // uri -> result
	struct ActiveDownloadInfo
	{
		int waiterSocket;
		pid_t performerPid;
		shared_ptr< Pipe > performerPipe;
		string targetPath;
	};
	map< string, ActiveDownloadInfo > activeDownloads; // uri -> info
	struct OnHoldRecord
	{
		string uri;
		string targetPath;
		int waiterSocket;
	};
	queue< OnHoldRecord > onHold;
	multimap< string, int > pendingDuplicates; // uri -> waiterSocket
	map< string, size_t > sizes;

	void finishPendingDownload(multimap< string, int >::iterator, const string&, bool);
	void processFinalResult(MessageQueue&, const vector< string >& params, bool debugging);
	void processPreliminaryResult(MessageQueue&, const vector< string >& params, bool debugging);
	void processProgressMessage(MessageQueue&, const vector< string >& params);
	void proceedDownload(MessageQueue&, const vector< string >& params, bool debugging);
	void killPerformerBecauseOfWrongSize(MessageQueue&, const string& uri,
			const string& actionName, const string& errorString);
	void terminateDownloadProcesses();
	void startNewDownload(const string& uri, const string& targetPath, int waiterSocket, bool debugging);
	InputMessage pollAllInput(MessageQueue& workerQueue,
			const vector< int >& persistentSockets, set< int >& clientSockets,
			bool exitFlag, bool debugging);
	void worker();
	string perform(const string& uri, const string& targetPath, int sock);

	int getUriPriority(const Uri& uri);
	map< string, InnerDownloadElement > convertEntitiesToDownloads(
			const vector< Manager::DownloadEntity >& entities);
 public:
	ManagerImpl(const shared_ptr< const Config >& config, const shared_ptr< Progress >& progress);
	string download(const vector< Manager::DownloadEntity >& entities);
	~ManagerImpl();
};

ManagerImpl::ManagerImpl(const shared_ptr< const Config >& config_, const shared_ptr< Progress >& progress_)
	: config(config_), progress(progress_), methodFactory(config_)
{
	if (config->getBool("cupt::worker::simulate"))
	{
		return;
	}

	// getting a file path for main socket
	auto temporaryName = tempnam(NULL, "cupt");
	if (temporaryName)
	{
		serverSocketPath = temporaryName;
		free(temporaryName);
	}
	else
	{
		serverSocketPath = string("/tmp/cupt-downloader-") + lexical_cast< string >(getpid()); // :P
	}

	// creating main socket
	unlink(serverSocketPath.c_str());
	serverSocket = socket(AF_UNIX, SOCK_STREAM, 0);
	if (serverSocket == -1)
	{
		fatal2e(__("unable to open the server socket"));
	}
	serverSocketAddress.sun_family = AF_UNIX;
	strcpy(serverSocketAddress.sun_path, serverSocketPath.c_str());
	if (bind(serverSocket, (sockaddr*)&serverSocketAddress, sizeof(sockaddr_un)) == -1)
	{
		fatal2e(__("unable to bind the server socket to the file '%s'"), serverSocketPath);
	}
	if (listen(serverSocket, SOMAXCONN) == -1)
	{
		fatal2e(__("unable to make the server socket on the file '%s' listen for connections"), serverSocketPath);
	}

	parentPipe.reset(new Pipe("parent"));

	auto pid = fork();
	if (pid == -1)
	{
		fatal2e(__("unable to create the download worker process: fork() failed"));
	}

	if (pid)
	{
		// this is a main process
		workerPid = pid;
		parentPipe->useAsWriter();
	}
	else
	{
		// this is background worker process
		try
		{
			worker();
		}
		catch (Exception&)
		{
			// try our best to close all sockets available
			parentPipe.reset();
			close(serverSocket);

			if (config->getBool("debug::downloader"))
			{
				debug2("aborting the download worker process because of the exception");
			}

			terminateDownloadProcesses();

			_exit(EXIT_FAILURE);
		}
	}
}

ManagerImpl::~ManagerImpl()
{
	if (!config->getBool("cupt::worker::simulate"))
	{
		// shutdown worker thread (if running)
		if (!waitpid(workerPid, NULL, WNOHANG))
		{
			sendSocketMessage(*parentPipe, vector< string >{ "exit" });

			int childExitStatus;
			if (waitpid(workerPid, &childExitStatus, 0) == -1)
			{
				warn2e(__("unable to shutdown the download worker process: waitpid() failed"));
			}
			else if (childExitStatus != 0)
			{
				warn2(__("the download worker process exited abnormally: %s"),
						getWaitStatusDescription(childExitStatus));
			}
		}
		else
		{
			warn2(__("the download worker process aborted unexpectedly"));
		}

		// cleaning server socket
		if (close(serverSocket) == -1)
		{
			warn2e(__("unable to close the download server socket"));
		}

		if (unlink(serverSocketPath.c_str()) == -1)
		{
			warn2e(__("unable to remove the download server socket file '%s'"), serverSocketPath);
		}
	}
}

void ManagerImpl::terminateDownloadProcesses()
{
	FORIT(activeDownloadIt, activeDownloads)
	{
		auto pid = activeDownloadIt->second.performerPid;
		if (kill(pid, SIGTERM) == -1)
		{
			if (errno != ESRCH)
			{
				warn2(__("download manager: unable to terminate a performer process (pid '%d')"), pid);
			}
		}
	}
}

// each worker has own process, so own ping pipe too
Pipe pingPipe("worker's ping");
volatile sig_atomic_t pingIsProcessed = true;

void sendPingMessage(int)
{
	if (pingIsProcessed) // don't send a ping when last is not processed
	{
		sendSocketMessage(pingPipe, vector< string >{ "ping" });
	}
	pingIsProcessed = false;
}

void enablePingTimer()
{
	struct sigaction newAction;
	newAction.sa_handler = sendPingMessage;
	if (sigfillset(&newAction.sa_mask) == -1)
	{
		fatal2e(__("%s() failed"), "sigfillset");
	}
	newAction.sa_flags = SA_RESTART;
	if (sigaction(SIGALRM, &newAction, NULL) == -1)
	{
		fatal2e(__("%s() failed"), "sigaction");
	}

	struct itimerval timerStruct;
	timerStruct.it_interval.tv_sec = 0;
	timerStruct.it_interval.tv_usec = 250000; // 0.25s
	timerStruct.it_value.tv_sec = 0;
	timerStruct.it_value.tv_usec = timerStruct.it_interval.tv_usec;
	if (setitimer(ITIMER_REAL, &timerStruct, NULL) == -1)
	{
		fatal2e(__("%s() failed"), "setitimer");
	}
}

void disablePingTimer()
{
	struct sigaction defaultAction;
	defaultAction.sa_handler = SIG_DFL;
	if (sigemptyset(&defaultAction.sa_mask) == -1)
	{
		fatal2e(__("%s() failed"), "sigemptyset");
	}
	defaultAction.sa_flags = 0;
	if (sigaction(SIGALRM, &defaultAction, NULL) == -1)
	{
		fatal2e(__("%s() failed"), "sigaction");
	}

	struct itimerval timerStruct;
	timerStruct.it_interval.tv_sec = 0;
	timerStruct.it_interval.tv_usec = 0;
	timerStruct.it_value.tv_sec = 0;
	timerStruct.it_value.tv_usec = 0;
	if (setitimer(ITIMER_REAL, &timerStruct, NULL) == -1)
	{
		fatal2e(__("%s() failed"), "setitimer");
	}
}

void ManagerImpl::processPreliminaryResult(MessageQueue& workerQueue,
		const vector< string >& params, bool debugging)
{
	if (params.size() != 2) // uri, result
	{
		fatal2i("download manager: wrong parameter count for 'done' message");
	}
	const string& uri = params[0];
	const string& result = params[1];

	if (debugging)
	{
		debug2("preliminary download result: '%s': '%s'", uri, result);
	}
	bool isDuplicatedDownload = false;

	auto downloadInfoIt = activeDownloads.find(uri);
	if (downloadInfoIt == activeDownloads.end())
	{
		fatal2i("download manager: received preliminary result for unexistent download, uri '%s'", uri);
	}
	ActiveDownloadInfo& downloadInfo = downloadInfoIt->second;
	sendSocketMessage(downloadInfo.waiterSocket,
			vector< string > { uri, result, lexical_cast< string >(isDuplicatedDownload) });

	// cleanup after child
	if (waitpid(downloadInfo.performerPid, NULL, 0) == -1)
	{
		fatal2e(__("waitpid on the performer process failed"));
	}
	downloadInfo.performerPipe.reset();

	// update progress
	workerQueue.push({ "progress", uri, "pre-done" });
}

void ManagerImpl::finishPendingDownload(multimap< string, int >::iterator pendingDownloadIt,
		const string& result, bool debugging)
{
	const bool isDuplicatedDownload = true;

	const string& uri = pendingDownloadIt->first;

	if (debugging)
	{
		debug2("final download result for a duplicated request: '%s': %s", uri, result);
	}
	auto waiterSocket = pendingDownloadIt->second;
	sendSocketMessage(waiterSocket,
			vector< string > { uri, result, lexical_cast< string >(isDuplicatedDownload) });

	pendingDuplicates.erase(pendingDownloadIt);
}

void ManagerImpl::processFinalResult(MessageQueue& workerQueue,
		const vector< string >& params, bool debugging)
{
	if (params.size() != 2) // uri, result
	{
		fatal2i("download manager: wrong parameter count for 'done-ack' message");
	}

	const string& uri = params[0];
	const string& result = params[1];
	if (debugging)
	{
		debug2("final download result: '%s': '%s'", uri, result);
	}

	// put the query to the list of finished ones
	done[uri] = result;

	if (debugging)
	{
		debug2("started checking pending queue");
	}
	{ // answering on duplicated requests if any

		auto matchedPendingDownloads = pendingDuplicates.equal_range(uri);
		for (auto pendingDownloadIt = matchedPendingDownloads.first;
				pendingDownloadIt != matchedPendingDownloads.second;)
		{
			finishPendingDownload(pendingDownloadIt++, result, debugging);
		}
	}
	if (debugging)
	{
		debug2("finished checking pending queue");
	}

	auto downloadInfoIt = activeDownloads.find(uri);
	if (downloadInfoIt == activeDownloads.end())
	{
		fatal2i("download manager: received final result for unexistent download, uri '%s'", uri);
	}
	activeDownloads.erase(downloadInfoIt);

	// update progress
	workerQueue.push({ "progress", uri, "done", result });

	// schedule next download if any
	workerQueue.push({ "pop-download" });
}

void ManagerImpl::killPerformerBecauseOfWrongSize(MessageQueue& workerQueue,
		const string& uri, const string& actionName, const string& errorString)
{
	// so, this download don't make sense
	auto downloadInfoIt = activeDownloads.find(uri);
	if (downloadInfoIt == activeDownloads.end())
	{
		fatal2i("download manager: received '%s' submessage for unexistent download, uri '%s'",
				actionName, uri);
	}
	ActiveDownloadInfo& downloadInfo = downloadInfoIt->second;
	// rest in peace, young process
	if (kill(downloadInfo.performerPid, SIGTERM) == -1)
	{
		fatal2e(__("unable to kill the process %u"), downloadInfo.performerPid);
	}
	// process it as failed
	workerQueue.push({ "done", uri, errorString });

	const string& path = downloadInfo.targetPath;
	if (unlink(path.c_str()) == -1)
	{
		warn2e(__("unable to remove the file '%s'"), path);
	}
}

void ManagerImpl::processProgressMessage(MessageQueue& workerQueue, const vector< string >& params)
{
	if (params.size() < 2) // uri, action name
	{
		fatal2i("download manager: wrong parameter count for 'progress' message");
	}

	const string& uri = params[0];
	const string& actionName = params[1];

	auto downloadSizeIt = sizes.find(uri);
	if (actionName == "expected-size" && downloadSizeIt != sizes.end())
	{
		// ok, we knew what size we should get, and the method has reported its variant
		// now compare them strictly
		if (params.size() != 3)
		{
			fatal2i("download manager: wrong parameter count for 'progress' message, 'expected-size' submessage");
		}

		auto expectedSize = lexical_cast< size_t >(params[2]);
		if (expectedSize != downloadSizeIt->second)
		{
			auto errorString = format2(__("invalid expected size: expected '%zu', got '%zu'"),
					downloadSizeIt->second, expectedSize);
			killPerformerBecauseOfWrongSize(workerQueue, uri, actionName, errorString);
		}
	}
	else
	{
		if (actionName == "downloading" && downloadSizeIt != sizes.end())
		{
			// checking for overflows
			if (params.size() != 4)
			{
				fatal2i("download manager: wrong parameter count for 'progress' message, 'downloading' submessage");
			}
			auto currentSize = lexical_cast< size_t >(params[2]);
			if (currentSize > downloadSizeIt->second)
			{
				auto errorString = format2(__("downloaded more than expected: expected '%zu', downloaded '%zu'"),
						downloadSizeIt->second, currentSize);
				killPerformerBecauseOfWrongSize(workerQueue, uri, actionName, errorString);
				return;
			}
		}
		// update progress
		progress->progress(params);
	}
}

void ManagerImpl::proceedDownload(MessageQueue& workerQueue, const vector< string >& params, bool debugging)
{
	if (params.size() != 3) // uri, targetPath, waiterSocket
	{
		fatal2i("download manager: wrong parameter count for 'progress' message");
	}
	const string& uri = params[0];
	if (debugging)
	{
		debug2("processing download '%s'", uri);
	}

	const int waiterSocket = lexical_cast< int >(params[2]);
	{ // pre-checks
		auto maxSimultaneousDownloadsAllowed = config->getInteger("cupt::downloader::max-simultaneous-downloads");

		auto doneIt = done.find(uri);
		if (doneIt != done.end())
		{
			const string& result = doneIt->second;
			auto insertIt = pendingDuplicates.insert(make_pair(uri, waiterSocket));
			// and immediately process it
			finishPendingDownload(insertIt, result, debugging);

			workerQueue.push({ "pop-download" });
			return;
		}
		else if (activeDownloads.count(uri))
		{
			if (debugging)
			{
				debug2("pushed '%s' to pending queue", uri);
			}
			pendingDuplicates.insert(make_pair(uri, waiterSocket));
			workerQueue.push({ "pop-download" });
			return;
		}
		else if (activeDownloads.size() >= (size_t)maxSimultaneousDownloadsAllowed)
		{
			// put the query on hold
			if (debugging)
			{
				debug2("put '%s' on hold", uri);
			}
			OnHoldRecord holdRecord;
			holdRecord.uri = uri;
			holdRecord.targetPath = params[1];
			holdRecord.waiterSocket = waiterSocket;
			onHold.push(std::move(holdRecord));
			return;
		}
	}

	// there is a space for new download, start it
	startNewDownload(uri, params[1], waiterSocket, debugging);
}

void ManagerImpl::startNewDownload(const string& uri, const string& targetPath,
		int waiterSocket, bool debugging)
{
	if (debugging)
	{
		debug2("starting download '%s'", uri);
	}
	ActiveDownloadInfo& downloadInfo = activeDownloads[uri]; // new element
	downloadInfo.targetPath = targetPath;
	downloadInfo.waiterSocket = waiterSocket;

	shared_ptr< Pipe > performerPipe(new Pipe("performer"));

	auto downloadPid = fork();
	if (downloadPid == -1)
	{
		fatal2e(__("unable to create a performer process: fork() failed"));
	}
	downloadInfo.performerPid = downloadPid;

	if (downloadPid)
	{
		// worker process, go ahead
		performerPipe->useAsReader();
		downloadInfo.performerPipe = performerPipe;
	}
	else
	{
		// background downloader process
		performerPipe->useAsWriter();

		// start progress
		vector< string > progressMessage = { "progress", uri, "start" };
		auto sizeIt = sizes.find(uri);
		if (sizeIt != sizes.end())
		{
			progressMessage.push_back(lexical_cast< string >(sizeIt->second));
		}

		sendSocketMessage(*performerPipe, progressMessage);

		auto errorMessage = perform(uri, targetPath, performerPipe->getWriterFd());
		sendSocketMessage(*performerPipe, vector< string >{ "done", uri, errorMessage });

		performerPipe.reset();

		_exit(0);
	}
}

InputMessage ManagerImpl::pollAllInput(MessageQueue& workerQueue,
		const vector< int >& persistentSockets, set< int >& clientSockets,
		bool exitFlag, bool debugging)
{
	if (!workerQueue.empty())
	{
		auto message = workerQueue.front();
		workerQueue.pop();
		return { INT_MAX, std::move(message) };
	}

	auto newInputPollFd = [](int sock) -> pollfd
	{
		pollfd pollFd;
		pollFd.fd = sock;
		pollFd.events = POLLIN;
		return pollFd;
	};
	vector< pollfd > pollInput;
	FORIT(persistentSocketIt, persistentSockets)
	{
		pollInput.push_back(newInputPollFd(*persistentSocketIt));
	}
	FORIT(runtimeSocketIt, clientSockets)
	{
		pollInput.push_back(newInputPollFd(*runtimeSocketIt));
	}
	FORIT(activeDownloadRecordIt, activeDownloads)
	{
		const shared_ptr< Pipe >& performerPipe = activeDownloadRecordIt->second.performerPipe;
		if (performerPipe) // may be false between 'done' and 'done-ack' messages, when performerPipe is closed
		{
			pollInput.push_back(newInputPollFd(performerPipe->getReaderFd()));
		}
	}

	do_poll:
	int waitParam = (exitFlag ? 0 /* immediately */ : -1 /* infinite */);
	auto pollResult = poll(&pollInput[0], pollInput.size(), waitParam);
	if (pollResult == -1)
	{
		if (errno == EINTR)
		{
			goto do_poll;
		}
		else
		{
			fatal2e(__("unable to poll worker loop sockets"));
		}
	}

	FORIT(pollInputRecordIt, pollInput)
	{
		if (!pollInputRecordIt->revents)
		{
			continue;
		}
		auto sock = pollInputRecordIt->fd;

		if (sock == serverSocket)
		{
			// new connection appeared
			sock = accept(sock, NULL, NULL);
			if (sock == -1)
			{
				fatal2e(__("unable to accept new socket connection"));
			}
			if (debugging)
			{
				debug2("accepted new connection");
			}
			clientSockets.insert(sock);
		}

		return { sock, receiveSocketMessage(sock) };
	}

	return { 0, {} }; // exitFlag is set, no more messages to read
}

void doNothing(int)
{}

void makeSyscallsRestartable()
{
	struct sigaction action;
	memset(&action, sizeof(action), 0);
	action.sa_handler = doNothing;
	if (sigemptyset(&action.sa_mask) == -1)
	{
		fatal2e(__("%s() failed"), "sigemptyset");
	}
	action.sa_flags = SA_RESTART;
	if (sigaction(SIGCHLD, &action, NULL) == -1)
	{
		fatal2e(__("%s() failed"), "sigaction");
	}
}

void ManagerImpl::worker()
{
	bool debugging = config->getBool("debug::downloader");
	if (debugging)
	{
		debug2("download worker process started");
	}

	parentPipe->useAsReader();

	bool exitFlag = false;

	makeSyscallsRestartable();
	enablePingTimer();

	const vector< int > persistentSockets = {
			pingPipe.getReaderFd(), parentPipe->getReaderFd(), serverSocket
	};
	set< int > clientSockets;

	MessageQueue workerQueue;

	// while caller may set exit flag, we should continue processing as long as
	// something is pending in internal queue
	InputMessage inputMessage;
	while (inputMessage = pollAllInput(workerQueue, persistentSockets, clientSockets, exitFlag, debugging), inputMessage.sock)
	{
		auto sock = inputMessage.sock;
		auto& params = inputMessage.message;
		auto command = params[0];
		params.erase(params.begin());

		if (command == "exit")
		{
			if (debugging)
			{
				debug2("exit scheduled");
			}
			exitFlag = true;
		}
		else if (command == "eof")
		{
			// the current socket reported EOF
			if (debugging)
			{
				debug2("eof has been reported");
			}
			if (sock == parentPipe->getReaderFd())
			{
				// oh-ho, parent process exited before closing parentPipe socket...
				// no reason to live anymore
				terminateDownloadProcesses();
				_exit(EXIT_FAILURE);
			}
			bool isPerformerSocket = false;
			FORIT(activeDownloadIt, activeDownloads)
			{
				auto performerPipe = activeDownloadIt->second.performerPipe;
				if (performerPipe && sock == performerPipe->getReaderFd())
				{
					// looks like download method crashed
					//
					// call processPreliminaryResult() immediately to remove sock from
					// the list of polled sockets
					processPreliminaryResult(workerQueue, vector< string > {
							activeDownloadIt->first, "download method exited unexpectedly" }, debugging);
					isPerformerSocket = true;
					break;
				}
			}

			if (!isPerformerSocket)
			{
				clientSockets.erase(sock); // if it is a client socket
				if (close(sock) == -1)
				{
					fatal2e(__("unable to close the client socket"));
				}
			}
		}
		else if (command == "download")
		{
			// new query appeared
			if (params.size() != 2) // uri, target path
			{
				fatal2i("download manager: wrong parameter count for 'download' message");
			}
			const string& uri = params[0];
			if (debugging)
			{
				debug2("download request: '%s'", uri);
			}
			workerQueue.push({ "proceed-download", uri, params[1], lexical_cast< string >(sock) });
		}
		else if (command == "set-download-size")
		{
			if (params.size() != 2) // uri, size
			{
				fatal2i("download manager: wrong parameter count for 'set-download-size' message");
			}
			const string& uri = params[0];
			const size_t size = lexical_cast< size_t >(params[1]);
			sizes[uri] = size;
		}
		else if (command == "done")
		{
			// some query finished, we have preliminary result for it
			processPreliminaryResult(workerQueue, params, debugging);
		}
		else if (command == "done-ack")
		{
			// this is final ACK from download with final result
			processFinalResult(workerQueue, params, debugging);
		}
		else if (command == "progress")
		{
			processProgressMessage(workerQueue, params);
		}
		else if (command == "ping")
		{
			sendSocketMessage(pingPipe, vector< string >({ "progress", "", "ping" }));

			// ping clients regularly so they can detect if worker process died
			FORIT(socketIt, clientSockets)
			{
				pollfd pollFd;
				pollFd.fd = *socketIt;
				pollFd.events = POLLERR;

				// but don't send anything if waiterSocket is already closed by pair
				// that can easily happen when process send done/done-ack and dies
				int pollResult;
				do_poll:
				pollResult = poll(&pollFd, 1, 0);
				if (pollResult == -1)
				{
					if (errno == EINTR)
					{
						goto do_poll;
					}
					fatal2e(__("download worker: polling the waiter socket failed"));
				}
				if (!pollResult)
				{
					sendSocketMessage(*socketIt, vector< string >{ "ping" });
				}
			}
			pingIsProcessed = true;
		}
		else if (command == "pop-download")
		{
			if (!onHold.empty())
			{
				// put next of waiting queries
				auto next = onHold.front();
				onHold.pop();
				if (debugging)
				{
					debug2("enqueue '%s' from hold", next.uri);
				}
				workerQueue.push({ "proceed-download",
						next.uri, next.targetPath, lexical_cast< string >(next.waiterSocket) });
			}
		}
		else if (command == "set-long-alias")
		{
			if (params.size() != 2)
			{
				fatal2i("download manager: wrong parameter count for 'set-long-alias' message");
			}
			progress->setLongAliasForUri(params[0], params[1]);
		}
		else if (command == "set-short-alias")
		{
			if (params.size() != 2)
			{
				fatal2i("download manager: wrong parameter count for 'set-short-alias' message");
			}
			progress->setShortAliasForUri(params[0], params[1]);
		}
		else if (command == "proceed-download")
		{
			proceedDownload(workerQueue, params, debugging);
		}
		else
		{
			fatal2i("download manager: invalid worker command '%s'", command);
		}
	}
	disablePingTimer();
	// finishing progress
	progress->progress(vector< string >{ "finish" });

	if (debugging)
	{
		debug2("download worker process finished");
	}
	_exit(0);
	return; // unreachable :)
}

int ManagerImpl::getUriPriority(const Uri& uri)
{
	auto protocol = uri.getProtocol();
	auto optionName = string("cupt::downloader::protocols::") + protocol + "::priority";
	auto result = config->getInteger(optionName);
	return result ? result : 100;
}

map< string, InnerDownloadElement > ManagerImpl::convertEntitiesToDownloads(
		const vector< Manager::DownloadEntity >& entities)
{
	map< string, InnerDownloadElement > result;

	FORIT(entityIt, entities)
	{
		const string& targetPath = entityIt->targetPath;
		if (targetPath.empty())
		{
			fatal2(__("passed a download entity with an empty target path"));
		}
		if (result.count(targetPath))
		{
			fatal2(__("passed distinct download entities with the same target path '%s'"), targetPath);
		}
		InnerDownloadElement& element = result[targetPath];

		// sorting uris by protocols' priorities
		vector< pair< Manager::ExtendedUri, int > > extendedPrioritizedUris;
		FORIT(extendedUriIt, entityIt->extendedUris)
		{
			extendedPrioritizedUris.push_back(make_pair(*extendedUriIt, getUriPriority(extendedUriIt->uri)));
		}
		std::sort(extendedPrioritizedUris.begin(), extendedPrioritizedUris.end(), [this]
				(const pair< Manager::ExtendedUri, int >& left, const pair< Manager::ExtendedUri, int >& right)
				{
					return left.second > right.second;
				});
		FORIT(it, extendedPrioritizedUris)
		{
			element.extendedUris.push(it->first);
		}

		element.size = entityIt->size;
		element.postAction = entityIt->postAction;
	}

	return result;
}

static void checkSocketForTimeout(int sock)
{
	pollfd pollFd;
	pollFd.fd = sock;
	pollFd.events = POLLIN;
	do_poll:
	auto pollResult = poll(&pollFd, 1, 2000); // 2 seconds
	if (pollResult == -1)
	{
		if (errno == EINTR)
		{
			goto do_poll;
		}
		else
		{
			fatal2e(__("download client: polling the client socket failed"));
		}
	}
	else if (!pollResult)
	{
		fatal2(__("download client: the download server socket timed out"));
	}
}

string ManagerImpl::download(const vector< Manager::DownloadEntity >& entities)
{
	if (config->getBool("cupt::worker::simulate"))
	{
		FORIT(entityIt, entities)
		{
			vector< string > uris;
			FORIT(extendedUriIt, entityIt->extendedUris)
			{
				uris.push_back(extendedUriIt->uri);
			}

			simulate2("downloading: %s", join(" | ", uris));
		}
		return "";
	}

	auto downloads = convertEntitiesToDownloads(entities);

	map< string, string > waitedUriToTargetPath;

	auto sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sock == -1)
	{
		fatal2e(__("unable to open a client socket"));
	}
	if (connect(sock, (sockaddr*)&serverSocketAddress, sizeof(sockaddr_un)) == -1)
	{
		fatal2e(__("unable to connect to the server socket"));
	}

	auto scheduleDownload = [&sock, &downloads, &waitedUriToTargetPath](const string& targetPath)
	{
		InnerDownloadElement& downloadElement = downloads[targetPath];

		auto extendedUri = downloadElement.extendedUris.front();
		downloadElement.extendedUris.pop();

		const string& uri = extendedUri.uri;

		if (downloadElement.size != (size_t)-1)
		{
			sendSocketMessage(sock,
					vector< string >{ "set-download-size", uri, lexical_cast< string >(downloadElement.size) });
		}
		if (!extendedUri.shortAlias.empty())
		{
			sendSocketMessage(sock, vector< string >{ "set-short-alias", uri, extendedUri.shortAlias });
		}
		if (!extendedUri.longAlias.empty())
		{
			sendSocketMessage(sock, vector< string >{ "set-long-alias", uri, extendedUri.longAlias });
		}
		sendSocketMessage(sock, vector< string >{ "download", uri, targetPath });

		waitedUriToTargetPath[uri] = targetPath;
	};

	FORIT(downloadIt, downloads)
	{
		scheduleDownload(downloadIt->first);
	}

	// now wait for them
	string result; // no error by default
	while (!waitedUriToTargetPath.empty())
	{
		checkSocketForTimeout(sock);

		auto params = receiveSocketMessage(sock);
		if (params.size() == 1 && params[0] == "ping")
		{
			continue; // it's just ping that worker is alive
		}
		if (params.size() != 3)
		{
			fatal2i("download client: wrong parameter count for download result message");
		}
		const string& uri = params[0];

		auto waitedUriIt = waitedUriToTargetPath.find(uri);
		if (waitedUriIt == waitedUriToTargetPath.end())
		{
			fatal2i("download client: received unknown uri '%s'", uri);
		}

		const string targetPath = waitedUriIt->second;
		const InnerDownloadElement& element = downloads[targetPath];

		waitedUriToTargetPath.erase(waitedUriIt);

		string errorString = params[1];
		bool isDuplicatedDownload = lexical_cast< bool >(params[2]);

		if (errorString.empty() && !isDuplicatedDownload)
		{
			// download seems to be done well, but we also have post-action specified
			// but do this only if this file wasn't post-processed before
			try
			{
				errorString = element.postAction();
			}
			catch (std::exception& e)
			{
				errorString = format2(__("the postprocessing action raised an exception '%s'"), e.what());
			}
			catch (...)
			{
				errorString = __("the postprocessing action raised an exception");
			}
		}

		if (!isDuplicatedDownload)
		{
			// now we know final result, send it back (for progress indicator)
			sendSocketMessage(sock, vector< string >{ "done-ack", uri, errorString });
		}

		if (!errorString.empty())
		{
			// this download hasn't been processed smoothly
			// check - maybe we have another URI(s) for this file?
			if (!element.extendedUris.empty())
			{
				// yes, so reschedule a download with another URI
				scheduleDownload(targetPath);
			}
			else
			{
				// no, this URI was last
				result = errorString;
			}
		}
	}

	if (close(sock) == -1)
	{
		fatal2e(__("unable to close the client socket"));
	}

	return result;
}

string ManagerImpl::perform(const string& uri, const string& targetPath, int sock)
{
	auto callback = [&sock, &uri](const vector< string >& params)
	{
		vector< string > newParams = { "progress", uri };
		newParams.insert(newParams.end(), params.begin(), params.end());
		sendSocketMessage(sock, newParams);
	};
	string result;
	try
	{
		auto downloadMethod = methodFactory.getDownloadMethodForUri(uri);
		result = downloadMethod->perform(config, uri, targetPath, callback);
		delete downloadMethod;
	}
	catch (Exception& e)
	{
		result = e.what();
	}
	return result;
}

}

namespace download {

Manager::Manager(const shared_ptr< const Config >& config, const shared_ptr< Progress >& progress)
	: __impl(new internal::ManagerImpl(config, progress))
{}

Manager::~Manager()
{
	delete __impl;
}

string Manager::download(const vector< DownloadEntity >& entities)
{
	return __impl->download(entities);
}

}
}

