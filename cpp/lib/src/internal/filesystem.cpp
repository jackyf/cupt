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
#include <cstdio>
#include <cstring>
#include <cstddef>

#include <libgen.h>
#include <glob.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <fnmatch.h>

#include <internal/common.hpp>
#include <internal/filesystem.hpp>

namespace cupt {
namespace internal {
namespace fs {

string filename(const string& path)
{
	char* pathCopy = strdup(path.c_str());
	string result(::basename(pathCopy));
	free(pathCopy);

	return result;
}

string dirname(const string& path)
{
	char* pathCopy = strdup(path.c_str());
	string result(::dirname(pathCopy));
	free(pathCopy);

	return result;
}

bool move(const string& oldPath, const string& newPath)
{
	return (rename(oldPath.c_str(), newPath.c_str()) != -1);
}

vector< string > glob(const string& param)
{
	vector< string > strings;

	glob_t glob_result;
	auto result = glob(param.c_str(), 0, NULL, &glob_result);
	if (result != 0 && result != GLOB_NOMATCH)
	{
		globfree(&glob_result);
		fatal2e(__("%s() failed: '%s'"), "glob", param);
	}
	for (size_t i = 0; i < glob_result.gl_pathc; ++i)
	{
		strings.push_back(string(glob_result.gl_pathv[i]));
	}
	globfree(&glob_result);
	return strings;
}

vector< string > lglob(const string& directoryPath, const string& shellPattern)
{
	auto dirPtr = opendir(directoryPath.c_str());
	if (!dirPtr)
	{
		fatal2e(__("unable to open the directory '%s'"), directoryPath);
	}

	vector< string > strings;

	struct dirent* directoryEntryPtr = (struct dirent*)malloc(
			offsetof(struct dirent, d_name) + pathconf(directoryPath.c_str(), _PC_NAME_MAX) + 1);
	struct dirent* resultDirectoryEntryPtr;

	auto freeResources = [&dirPtr, &directoryEntryPtr, &directoryPath]()
	{
		free(directoryEntryPtr);
		if (closedir(dirPtr) == -1)
		{
			fatal2e(__("unable to close the directory '%s'"), directoryPath);
		}
	};

	for (;;)
	{
		auto readdirrResult = readdir_r(dirPtr, directoryEntryPtr, &resultDirectoryEntryPtr);
		if (readdirrResult)
		{
			freeResources();
			fatal2(__("%s() failed: '%s'"), "readdir_r", directoryPath);
		}

		if (!resultDirectoryEntryPtr)
		{
			freeResources();
			return strings;
		}

		const char* const& d_name = resultDirectoryEntryPtr->d_name;
		if (d_name[0] == '.')
		{
			continue;
		}

		if (!fnmatch(shellPattern.c_str(), d_name, 0))
		{
			strings.push_back(directoryPath + '/' + d_name);
		}
	}
}

bool __lstat(const string& path, struct stat* result)
{
	auto error = lstat(path.c_str(), result);
	if (error)
	{
		if (errno == ENOENT)
		{
			return false;
		}
		else
		{
			fatal2e(__("%s() failed: '%s'"), "lstat", path);
		}
	}
	return true;
}

bool fileExists(const string& path)
{
	struct stat s;
	return __lstat(path, &s) && (S_ISREG(s.st_mode) || S_ISLNK(s.st_mode) || S_ISFIFO(s.st_mode));
}

bool dirExists(const string& path)
{
	struct stat s;
	return __lstat(path, &s) && S_ISDIR(s.st_mode);
}

size_t fileSize(const string& path)
{
	struct stat s;
	if (!__lstat(path, &s))
	{
		fatal2(__("the file '%s' does not exist"), path);
	}
	if (!S_ISREG(s.st_mode))
	{
		fatal2(__("the file '%s' is not a regular file"), path);
	}
	return s.st_size;
}

void mkpath(const string& path)
{
	auto ensureDirectoryExist = [](const string& pathPart)
	{
		if (mkdir(pathPart.c_str(), 0755) == -1)
		{
			if (errno != EEXIST &&
					errno != EISDIR /* http://www.freebsd.org/cgi/query-pr.cgi?pr=59739 */)
			{
				fatal2e(__("unable to create the directory '%s'"), pathPart);
			}
		}
	};

	size_t position = 0;
	while (position = path.find('/', ++position), position != string::npos)
	{
		ensureDirectoryExist(path.substr(0, position));
	}
	ensureDirectoryExist(path);
}

}
}
}

