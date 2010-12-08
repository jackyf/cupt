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
#include <map>

#include <cupt/file.hpp>
#include <cupt/system/state.hpp>
#include <cupt/cache/releaseinfo.hpp>
#include <cupt/cache.hpp>
#include <cupt/cache/version.hpp>
#include <cupt/cache/package.hpp>
#include <cupt/config.hpp>

#include <internal/tagparser.hpp>
#include <internal/cacheimpl.hpp>
#include <internal/common.hpp>

namespace cupt {

namespace internal {

using std::map;

typedef system::State::InstalledRecord InstalledRecord;

struct StateData
{
	shared_ptr< const Config > config;
	internal::CacheImpl* cacheImpl;
	map< string, shared_ptr< const InstalledRecord > > installedInfo;

	void parseDpkgStatus();
};

void parseStatusSubstrings(const string& packageName, const string& input,
		const shared_ptr< InstalledRecord >& installedRecord)
{
	// status should be a triplet delimited by spaces (i.e. 2 ones)
	internal::TagParser::StringRange current;

	current.first = current.second = input.begin();
	auto end = input.end();

	while (current.second != end && *current.second != ' ')
	{
		++current.second;
	}
	{ // want
#define CHECK_WANT(str, value) if (current.equal(BUFFER_AND_SIZE(str))) { installedRecord->want = InstalledRecord::Want:: value; } else
		CHECK_WANT("install", Install)
		CHECK_WANT("deinstall", Deinstall)
		CHECK_WANT("unknown", Unknown)
		CHECK_WANT("hold", Hold)
		CHECK_WANT("purge", Purge)
		{ // else
			fatal("malformed 'desired' status indicator (for package '%s')", packageName.c_str());
		}
#undef CHECK_WANT
	}

	if (current.second == end)
	{
		fatal("no 'error' status indicator (for package '%s')", packageName.c_str());
	}
	current.first = ++current.second;
	while (current.second != end && *current.second != ' ')
	{
		++current.second;
	}
	{ // flag
#define CHECK_FLAG(str, value) if (current.equal(BUFFER_AND_SIZE(str))) { installedRecord->flag = InstalledRecord::Flag:: value; } else
		CHECK_FLAG("ok", Ok)
		CHECK_FLAG("reinstreq", Reinstreq)
		CHECK_FLAG("hold", Hold)
		CHECK_FLAG("hold-reinstreq", HoldAndReinstreq)
		{ // else
			fatal("malformed 'error' status indicator (for package '%s')", packageName.c_str());
		}
#undef CHECK_FLAG
	}

	if (current.second == end)
	{
		fatal("no 'status' status indicator (for package '%s')", packageName.c_str());
	}
	current.first = current.second + 1;
	current.second = end;
	{ // status
#define CHECK_STATUS(str, value) if (current.equal(BUFFER_AND_SIZE(str))) { installedRecord->status = InstalledRecord::Status:: value; } else
		CHECK_STATUS("installed", Installed)
		CHECK_STATUS("not-installed", NotInstalled)
		CHECK_STATUS("config-files", ConfigFiles)
		CHECK_STATUS("unpacked", Unpacked)
		CHECK_STATUS("half-configured", HalfConfigured)
		CHECK_STATUS("half-installed", HalfInstalled)
		CHECK_STATUS("post-inst-failed", PostInstFailed)
		CHECK_STATUS("removal-failed", RemovalFailed)
		{ // else
			if (current.second - current.first >= 7 && !memcmp(&*current.first, "trigger", 7))
			{
				fatal("some dpkg triggers are not processed, please run 'dpkg --triggers-only -a' as root");
			}
			else
			{
				fatal("malformed 'status' status indicator (for package '%s')", packageName.c_str());
			}
		}
	}
}

void StateData::parseDpkgStatus()
{
	string path = config->getPath("dir::state::status");
	string openError;
	shared_ptr< File > file(new File(path, "r", openError));
	if (!openError.empty())
	{
		fatal("unable to open dpkg status file '%s': %s", path.c_str(), openError.c_str());
	}

	/*
	 Status lines are similar to apt Packages ones, with two differences:
	 1) 'Status' field: see header for possible values
	 2) purged packages contain only 'Package', 'Status', 'Priority'
	    and 'Section' fields.
	*/

	// filling release info
	shared_ptr< ReleaseInfo > releaseInfo(new ReleaseInfo);
	releaseInfo->archive = "installed";
	releaseInfo->codename = "now";
	releaseInfo->vendor = "dpkg";
	releaseInfo->verified = false;
	releaseInfo->notAutomatic = false;

	cacheImpl->releaseInfoAndFileStorage.push_back(make_pair(releaseInfo, file));
	internal::CacheImpl::PrePackageRecord prePackageRecord;
	prePackageRecord.releaseInfoAndFile = &*(cacheImpl->releaseInfoAndFileStorage.rbegin());

	cacheImpl->binaryReleaseData.push_back(releaseInfo);
	auto preBinaryPackages = &(cacheImpl->preBinaryPackages);

	try
	{
		internal::TagParser parser(file.get());
		internal::TagParser::StringRange tagName, tagValue;

		pair< const string, vector< internal::CacheImpl::PrePackageRecord > > pairForInsertion;
		string& packageName = const_cast< string& >(pairForInsertion.first);

		while ((prePackageRecord.offset = file->tell()), (parser.parseNextLine(tagName, tagValue) && !file->eof()))
		{
			string status;
			string provides;
			bool parsedTagsByIndex[4] = {0};
			bool& packageNameIsPresent = parsedTagsByIndex[0];
			bool& versionIsPresent = parsedTagsByIndex[2];
			do
			{
#define TAG(str, index, code) \
				if (!parsedTagsByIndex[index] && tagName.equal(BUFFER_AND_SIZE(str))) \
				{ \
					code; \
					parsedTagsByIndex[index] = true; \
					continue; \
				} \

				TAG("Package", 0, packageName = tagValue)
				TAG("Status", 1, status = tagValue)
				TAG("Version", 2, ;)
				TAG("Provides", 3, provides = tagValue)
#undef TAG
			} while (parser.parseNextLine(tagName, tagValue));

			if (!versionIsPresent)
			{
				continue;
			}
			// we don't check package name for correctness - even if it's incorrent, we can't decline installed packages :(

			if (!packageNameIsPresent)
			{
				fatal("no package name in the record");
			}
			shared_ptr< InstalledRecord > installedRecord(new InstalledRecord);
			parseStatusSubstrings(packageName, status, installedRecord);

			if (installedRecord->flag == InstalledRecord::Flag::Ok)
			{
				if (installedRecord->status != InstalledRecord::Status::NotInstalled &&
					installedRecord->status != InstalledRecord::Status::ConfigFiles)
				{
					// this conditions mean that package is installed or
					//   semi-installed, regardless it has full entry info, so
					//  add it (info) to cache

					auto it = preBinaryPackages->insert(pairForInsertion).first;
					it->second.push_back(prePackageRecord);

					if (!provides.empty())
					{
						cacheImpl->processProvides(&it->first,
								&*(provides.begin()), &*(provides.end()));
					}
				}

				// add parsed info to installed_info
				installedInfo.insert(pair< const string, shared_ptr< const InstalledRecord > >(
						std::move(packageName), std::move(installedRecord)));
			}
		}
	}
	catch (Exception&)
	{
		fatal("error parsing system status file '%s'", path.c_str());
	}
}

}

namespace system {

State::State(shared_ptr< const Config > config, internal::CacheImpl* cacheImpl)
	: __data(new internal::StateData)
{
	__data->config = config;
	__data->cacheImpl = cacheImpl;
	__data->parseDpkgStatus();
}

State::~State()
{
	delete __data;
}

shared_ptr< const State::InstalledRecord > State::getInstalledInfo(const string& packageName) const
{
	auto it = __data->installedInfo.find(packageName);
	if (it == __data->installedInfo.end())
	{
		return shared_ptr< const InstalledRecord >();
	}
	else
	{
		return it->second;
	}
}

vector< string > State::getInstalledPackageNames() const
{
	vector< string > result;

	FORIT(it, __data->installedInfo)
	{
		const InstalledRecord& installedRecord = *(it->second);

		if (installedRecord.status == InstalledRecord::Status::NotInstalled ||
			installedRecord.status == InstalledRecord::Status::ConfigFiles)
		{
			continue;
		}

		result.push_back(it->first);

	}

	return result;
}

const string State::InstalledRecord::Status::strings[] = {
	__("not installed"), __("unpacked"), __("half-configured"), __("half-installed"),
	__("config files"), __("postinst failed"), __("removal failed"), __("installed")
};

}
}

