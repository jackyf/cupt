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
#include <cupt/common.hpp>
#include <cupt/download/uri.hpp>
#include <cupt/download/method.hpp>
#include <cupt/download/methodfactory.hpp>

namespace cupt {

class DebdeltaMethod: public download::Method
{
	string perform(const shared_ptr< const Config >& config, const download::Uri& uri,
			const string& targetPath, const std::function< void (const vector< string >&) >& callback)
	{
		auto deltaCallback = [callback](const vector< string >& params)
		{
			if (!params.empty() && params[0] == "expected-size")
			{
				return; // ignore it
			}
			callback(params);
		};

		// download delta file
		auto deltaUri = uri.getOpaque();
		auto deltaDownloadPath = targetPath + ".delta";

		download::MethodFactory methodFactory(config);
		auto method = methodFactory.getDownloadMethodForUri(deltaUri);
		auto deltaDownloadResult = method->perform(config, deltaUri, deltaDownloadPath, deltaCallback);
		delete method;
		if (!deltaDownloadResult.empty())
		{
			return format2(__("delta download failed: %s"), deltaDownloadResult);
		}

		// invoking a deb patcher
		auto patchCommand = format2("debpatch --accept-unsigned %s / %s >/dev/null",
				deltaDownloadPath, targetPath);
		auto patchResult = ::system(patchCommand.c_str());

		// remove delta anyway
		if (unlink(deltaDownloadPath.c_str()) == -1)
		{
			warn2e(__("unable to remove the file '%s'"), deltaDownloadPath);
		}

		if (patchResult != 0)
		{
			return format2(__("debpatch returned error code %d"), patchResult);
		}

		// all went ok
		return string();
	}
};

}

extern "C"
{
	cupt::download::Method* construct()
	{
		return new cupt::DebdeltaMethod();
	}
}

