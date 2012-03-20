/**************************************************************************
*   Copyright (C) 2008-2011 by Eugene V. Lyubimkin                        *
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
// this group is for stat()
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <boost/lexical_cast.hpp>
using boost::lexical_cast;

#include <cupt/download/method.hpp>
#include <cupt/download/uri.hpp>
#include <cupt/file.hpp>

namespace cupt {

class FileMethod: public download::Method
{
	string copyFile(const string& sourcePath, File& sourceFile, const string& targetPath,
			const std::function< void (const vector< string >&) >& callback)
	{
		// preparing target
		string openError;
		File targetFile(targetPath, "a", openError);
		if (!openError.empty())
		{
			return format2("unable to open the file '%s' for appending: %s", targetPath, openError);
		}
		auto totalBytes = targetFile.tell();
		callback(vector< string > { "downloading",
				lexical_cast< string >(totalBytes), lexical_cast< string >(0)});

		{ // determing the size
			struct stat st;
			if (::stat(sourcePath.c_str(), &st) == -1)
			{
				fatal2e(__("%s() failed: '%s'"), "stat", sourcePath);
			}
			callback(vector< string > { "expected-size",
					lexical_cast< string >(st.st_size) });
		}

		{ // writing
			char buffer[4096];
			size_t size = sizeof(buffer);
			while (sourceFile.getBlock(buffer, size), size)
			{
				targetFile.put(buffer, size);
				totalBytes += size;
				callback(vector< string > { "downloading",
						lexical_cast< string >(totalBytes), lexical_cast< string >(size)});
			}
		}

		return string();
	}
	string perform(const shared_ptr< const Config >& /* config */, const download::Uri& uri,
			const string& targetPath, const std::function< void (const vector< string >&) >& callback)
	{
		auto sourcePath = uri.getOpaque();
		auto protocol = uri.getProtocol();

		// preparing source
		string openError;
		File sourceFile(sourcePath, "r", openError);
		if (!openError.empty())
		{
			return format2("unable to open the file '%s' for reading: %s", sourcePath, openError);
		}

		if (protocol == "copy")
		{
			return copyFile(sourcePath, sourceFile, targetPath, callback); // full copying
		}
		else if (protocol == "file")
		{
			// symlinking
			unlink(targetPath.c_str()); // yep, no error handling;
			if (symlink(sourcePath.c_str(), targetPath.c_str()) == -1)
			{
				return format2e("unable to create symbolic link '%s' -> '%s'", targetPath, sourcePath);
			}
			return string();
		}
		else
		{
			fatal2i("a wrong scheme '%s' for the 'file' download method", protocol);
			return string(); // unreachable
		}
	}
};

}

extern "C"
{
	cupt::download::Method* construct()
	{
		return new cupt::FileMethod();
	}
}

