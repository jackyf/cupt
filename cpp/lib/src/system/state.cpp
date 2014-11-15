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
	map<string, unique_ptr< const InstalledRecord >> installedInfo;

	void parseDpkgStatus();
};

void parseStatusSubstrings(const string& packageName, const string& input, InstalledRecord* installedRecord)
{
	// status should be a triplet delimited by spaces (i.e. 2 ones)
	internal::TagParser::StringRange current;

	current.first = current.second = input.data();
	auto end = input.data() + input.size();

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
			fatal2(__("malformed '%s' status indicator (for the package '%s')"), "desired", packageName);
		}
#undef CHECK_WANT
	}

	if (current.second == end)
	{
		fatal2(__("no '%s' status indicator (for the package '%s')"), "error", packageName);
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
		{ // else
			fatal2(__("malformed '%s' status indicator (for the package '%s')"), "error", packageName);
		}
#undef CHECK_FLAG
	}

	if (current.second == end)
	{
		fatal2(__("no '%s' status indicator (for the package '%s')"), "status", packageName);
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
		CHECK_STATUS("triggers-pending", TriggersPending)
		CHECK_STATUS("triggers-awaited", TriggersAwaited)
		{ // else
			fatal2(__("malformed '%s' status indicator (for the package '%s')"), "status", packageName);
		}
	}
}

static bool packageHasFullEntryInfo(const InstalledRecord& record)
{
	return record.status != InstalledRecord::Status::NotInstalled &&
			record.status != InstalledRecord::Status::ConfigFiles;
}

typedef pair< shared_ptr< const ReleaseInfo >, shared_ptr< File > > VersionSource;

VersionSource* createVersionSource(internal::CacheImpl* cacheImpl,
		const string& archiveName, const shared_ptr< File >& file)
{
	// filling release info
	shared_ptr< ReleaseInfo > releaseInfo(new ReleaseInfo);
	releaseInfo->archive = archiveName;
	releaseInfo->codename = "now";
	releaseInfo->vendor = "dpkg";
	releaseInfo->verified = false;
	releaseInfo->notAutomatic = false;

	cacheImpl->binaryReleaseData.push_back(releaseInfo);

	cacheImpl->releaseInfoAndFileStorage.push_back(make_pair(releaseInfo, file));
	return &*(cacheImpl->releaseInfoAndFileStorage.rbegin());
}

void StateData::parseDpkgStatus()
{
	string path = config->getPath("dir::state::status");
	string openError;
	shared_ptr< File > file(new File(path, "r", openError));
	if (!openError.empty())
	{
		fatal2(__("unable to open the dpkg status file '%s': %s"), path, openError);
	}

	/*
	 Status lines are similar to apt Packages ones, with two differences:
	 1) 'Status' field: see header for possible values
	 2) purged packages contain only 'Package', 'Status', 'Priority'
	    and 'Section' fields.
	*/

	auto installedSource = createVersionSource(cacheImpl, "installed", file);
	auto improperlyInstalledSource = createVersionSource(cacheImpl, "improperly-installed", file);

	auto preBinaryPackages = &(cacheImpl->preBinaryPackages);

	internal::CacheImpl::PrePackageRecord prePackageRecord;

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

				TAG("Package", 0, packageName = tagValue.toString())
				TAG("Status", 1, status = tagValue.toString())
				TAG("Version", 2, ;)
				TAG("Provides", 3, provides = tagValue.toString())
#undef TAG
			} while (parser.parseNextLine(tagName, tagValue));

			if (!versionIsPresent)
			{
				continue;
			}
			// we don't check package name for correctness - even if it's incorrent, we can't decline installed packages :(

			if (!packageNameIsPresent)
			{
				fatal2(__("no package name in the record"));
			}
			unique_ptr<InstalledRecord> installedRecord(new InstalledRecord());
			parseStatusSubstrings(packageName, status, installedRecord.get());

			if (packageHasFullEntryInfo(*installedRecord))
			{
				// this conditions mean that package is installed or
				// semi-installed, regardless it has full entry info, so add it
				// (info) to cache
				prePackageRecord.releaseInfoAndFile = installedRecord->isBroken() ?
						improperlyInstalledSource : installedSource;

				auto it = preBinaryPackages->insert(pairForInsertion).first;
				it->second.push_back(prePackageRecord);

				if (!provides.empty())
				{
					cacheImpl->processProvides(&it->first,
							&*(provides.begin()), &*(provides.end()));
				}
			}

			// add parsed info to installed_info
			installedInfo.emplace(std::move(packageName), std::move(installedRecord));
		}
	}
	catch (Exception&)
	{
		fatal2(__("error parsing the dpkg status file '%s'"), path);
	}
}

}

namespace system {

bool State::InstalledRecord::isBroken() const
{
	return flag != InstalledRecord::Flag::Ok ||
			status == InstalledRecord::Status::HalfInstalled;
}

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

const State::InstalledRecord* State::getInstalledInfo(const string& packageName) const
{
	auto it = __data->installedInfo.find(packageName);
	if (it == __data->installedInfo.end())
	{
		return nullptr;
	}
	else
	{
		return it->second.get();
	}
}

vector< string > State::getInstalledPackageNames() const
{
	vector< string > result;

	FORIT(it, __data->installedInfo)
	{
		const InstalledRecord& installedRecord = *(it->second);

		if (internal::packageHasFullEntryInfo(installedRecord))
		{
			result.push_back(it->first);
		}
	}

	return result;
}

vector< string > State::getReinstallRequiredPackageNames() const
{
	vector< string > result;

	FORIT(it, __data->installedInfo)
	{
		const InstalledRecord::Flag::Type& flag = it->second->flag;
		const InstalledRecord::Status::Type& status = it->second->status;
		if (flag == InstalledRecord::Flag::Reinstreq ||
				status == InstalledRecord::Status::HalfInstalled)
		{
			result.push_back(it->first);
		}
	}

	return result;
}

string State::getArchitecture() const
{
	return __data->config->getString("apt::architecture");
}

const string State::InstalledRecord::Status::strings[] = {
	N__("not installed"), N__("unpacked"), N__("half-configured"), N__("half-installed"),
	N__("config files"), N__("installed")
};

}
}

