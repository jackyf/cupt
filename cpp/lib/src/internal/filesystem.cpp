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

#include <libgen.h>
#include <glob.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

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

string move(const string& oldPath, const string& newPath)
{
	if (rename(oldPath.c_str(), newPath.c_str()) == -1)
	{
		return sf(__("unable to rename '%s' to '%s': EEE"), oldPath.c_str(), newPath.c_str());
	}
	return "";
}

vector< string > glob(const string& param)
{
	vector< string > strings;

	glob_t glob_result;
	auto result = glob(param.c_str(), 0, NULL, &glob_result);
	if (result != 0 && result != GLOB_NOMATCH)
	{
		globfree(&glob_result);
		fatal("glob() failed: '%s': EEE", param.c_str());
	}
	for (size_t i = 0; i < glob_result.gl_pathc; ++i)
	{
		strings.push_back(string(glob_result.gl_pathv[i]));
	}
	globfree(&glob_result);
	return strings;
}

bool __stat(const string& path, struct stat* result)
{
	auto error = stat(path.c_str(), result);
	if (error)
	{
		if (errno == ENOENT)
		{
			return false;
		}
		else
		{
			fatal("stat() failed: '%s': EEE", path.c_str());
		}
	}
	return true;
}

bool fileExists(const string& path)
{
	struct stat s;
	return __stat(path, &s) && (S_ISREG(s.st_mode) || S_ISLNK(s.st_mode) || S_ISFIFO(s.st_mode));
}

bool dirExists(const string& path)
{
	struct stat s;
	return __stat(path, &s) && S_ISDIR(s.st_mode);
}

}
}
}

