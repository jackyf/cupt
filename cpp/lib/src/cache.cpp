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

#include <algorithm>

#include <cupt/regex.hpp>
#include <cupt/file.hpp>
#include <cupt/config.hpp>
#include <cupt/cache.hpp>
#include <cupt/cache/binarypackage.hpp>
#include <cupt/cache/binaryversion.hpp>
#include <cupt/system/state.hpp>

#include <internal/filesystem.hpp>
#include <internal/cacheimpl.hpp>

namespace cupt {

typedef internal::CacheImpl::PrePackageRecord PrePackageRecord;

Cache::Cache(shared_ptr< const Config > config, bool useSource, bool useBinary, bool useInstalled,
		const vector< string >& packageNameGlobsToReinstall)
{
	__impl = new internal::CacheImpl;
	__impl->config = config;
	__impl->binaryArchitecture.reset(new string(config->getString("apt::architecture")));

	FORIT(it, packageNameGlobsToReinstall)
	{
		__impl->packageNameRegexesToReinstall.push_back(globToRegex(*it));
	}

	{ // ugly hack to copy trusted keyring from APT whenever possible
		auto cuptKeyringPath = config->getString("gpgv::trustedkeyring");
		auto aptKeyringPath = "/etc/apt/trusted.gpg";
		// ignore all errors, let install do its best
		std::system(sf("install -m644 %s %s >/dev/null 2>/dev/null",
				aptKeyringPath, cuptKeyringPath.c_str()).c_str());
	}

	__impl->parseSourcesLists();

	if (useInstalled)
	{
		__impl->systemState.reset(new system::State(config, __impl));
	}

	FORIT(indexEntryIt, __impl->indexEntries)
	{
		const IndexEntry& entry = *indexEntryIt;

		if (entry.category == IndexEntry::Binary && !useBinary)
		{
			continue;
		}
		if (entry.category == IndexEntry::Source && !useSource)
		{
			continue;
		}

		__impl->processIndexEntry(entry);
	}

	__impl->parsePreferences();
	__impl->parseExtendedStates();
}

Cache::~Cache()
{
	delete __impl;
}

vector< shared_ptr< const ReleaseInfo > > Cache::getBinaryReleaseData() const
{
	return __impl->binaryReleaseData;
}

vector< shared_ptr< const ReleaseInfo > > Cache::getSourceReleaseData() const
{
	return __impl->sourceReleaseData;
}

vector< Cache::IndexEntry > Cache::getIndexEntries() const
{
	return __impl->indexEntries;
}

string Cache::getPathOfIndexList(const IndexEntry& entry) const
{
	return __impl->getPathOfIndexList(entry);
}

string Cache::getPathOfReleaseList(const IndexEntry& entry) const
{
	return __impl->getPathOfReleaseList(entry);
}

string Cache::getPathOfExtendedStates() const
{
	return __impl->getPathOfExtendedStates();
}

string Cache::getDownloadUriOfReleaseList(const IndexEntry& entry) const
{
	return __impl->getDownloadUriOfReleaseList(entry);
}

vector< Cache::IndexDownloadRecord > Cache::getDownloadInfoOfIndexList(const IndexEntry& entry) const
{
	return __impl->getDownloadInfoOfIndexList(entry);
}

vector< Cache::LocalizationDownloadRecord > Cache::getDownloadInfoOfLocalizedDescriptions(const IndexEntry& entry) const
{
	return __impl->getDownloadInfoOfLocalizedDescriptions(entry);
}

vector< string > Cache::getBinaryPackageNames() const
{
	vector< string > result;
	FORIT(it, __impl->preBinaryPackages)
	{
		result.push_back(it->first);
	}
	return result;
}

vector< string > Cache::getSourcePackageNames() const
{
	vector< string > result;
	FORIT(it, __impl->preSourcePackages)
	{
		result.push_back(it->first);
	}
	return result;
}

shared_ptr< const BinaryPackage > Cache::getBinaryPackage(const string& packageName) const
{
	return __impl->getBinaryPackage(packageName);
}

shared_ptr< const SourcePackage > Cache::getSourcePackage(const string& packageName) const
{
	return __impl->getSourcePackage(packageName);
}

ssize_t Cache::getPin(const shared_ptr< const Version >& version) const
{
	string installedVersionString;
	if (dynamic_pointer_cast< const BinaryVersion >(version))
	{
		auto package = getBinaryPackage(version->packageName);
		if (package)
		{
			auto installedVersion = package->getInstalledVersion();
			if (installedVersion)
			{
				installedVersionString = installedVersion->versionString;
			}
		}
	}

	return __impl->getPin(version, installedVersionString);
}

vector< Cache::PinnedVersion > Cache::getSortedPinnedVersions(const shared_ptr< const Package >& package) const
{
	vector< Cache::PinnedVersion > result;

	auto versions = package->getVersions();

	string installedVersionString;
	if (auto binaryPackage = dynamic_pointer_cast< const BinaryPackage >(package))
	{
		auto installedVersion = binaryPackage->getInstalledVersion();
		if (installedVersion)
		{
			installedVersionString = installedVersion->versionString;
		}
	}

	size_t versionCount = versions.size();
	for (size_t i = 0; i < versionCount; ++i)
	{
		shared_ptr< const Version >& version = versions[i];
		result.push_back(PinnedVersion(version, __impl->getPin(version, installedVersionString)));
	}

	auto sorter = [](const PinnedVersion& left, const PinnedVersion& right) -> bool
	{
		if (left.pin < right.pin)
		{
			return false;
		}
		else if (left.pin > right.pin)
		{
			return true;
		}
		else
		{
			return compareVersionStrings(left.version->versionString, right.version->versionString) > 0;
		}
	};
	std::stable_sort(result.begin(), result.end(), sorter);

	return result;
}

shared_ptr< const Version > Cache::getPolicyVersion(const shared_ptr< const Package >& package) const
{
	auto sortedPinnedVersions = getSortedPinnedVersions(package);
	// not assuming the package have at least valid version...
	if (sortedPinnedVersions.empty())
	{
		return shared_ptr< const Version >();
	}
	else
	{
		// so, just return version with maximum "candidatness"
		return sortedPinnedVersions[0].version;
	}
}

shared_ptr< const system::State > Cache::getSystemState() const
{
	return __impl->systemState;
}

bool Cache::isAutomaticallyInstalled(const string& packageName) const
{
	return __impl->extendedInfo.automaticallyInstalled.count(packageName);
}

vector< shared_ptr< const BinaryVersion > >
Cache::getSatisfyingVersions(const RelationExpression& relationExpression) const
{
	return __impl->getSatisfyingVersions(relationExpression);
}

vector< shared_ptr< const BinaryVersion > > Cache::getInstalledVersions() const
{
	vector< shared_ptr< const BinaryVersion > > result;

	auto packageNames = __impl->systemState->getInstalledPackageNames();
	FORIT(packageNameIt, packageNames)
	{
		const string& packageName = *packageNameIt;

		auto package = getBinaryPackage(packageName);
		if (!package)
		{
			fatal("internal error: unable to find the package '%s'", packageName.c_str());
		}
		auto version = package->getInstalledVersion();
		if (!version)
		{
			fatal("internal error: the package '%s' does not have installed version", packageName.c_str());
		}

		result.push_back(version);
	}

	return result;
}

const Cache::ExtendedInfo& Cache::getExtendedInfo() const
{
	return __impl->extendedInfo;
}

pair< string, string > Cache::getLocalizedDescriptions(const shared_ptr< const BinaryVersion >& version) const
{
	return __impl->getLocalizedDescriptions(version);
}

// static
bool Cache::verifySignature(const shared_ptr< const Config >& config, const string& path)
{
	auto debugging = config->getBool("debug::gpgv");
	if (debugging)
	{
		debug("verifying file '%s'", path.c_str());
	}

	auto keyringPath = config->getString("gpgv::trustedkeyring");
	if (debugging)
	{
		debug("keyring file is '%s'", keyringPath.c_str());
	}

	auto signaturePath = path + ".gpg";
	if (debugging)
	{
		debug("signature file is '%s'", signaturePath.c_str());
	}

	if (!internal::fs::exists(signaturePath))
	{
		if (debugging)
		{
			debug("signature file '%s' doesn't exist", signaturePath.c_str());
		}
		return 0;
	}

	// file checks
	{
		string openError;
		File file(signaturePath, "r", openError);
		if (!openError.empty())
		{
			if (debugging)
			{
				debug("unable to read signature file '%s': %s",
						signaturePath.c_str(), openError.c_str());
			}
			return false;
		}
	}
	{
		string openError;
		File file(keyringPath, "r", openError);
		if (!openError.empty())
		{
			if (debugging)
			{
				debug("unable to read keyring file '%s': %s",
						keyringPath.c_str(), openError.c_str());
			}
			return false;
		}
	}

	bool verifyResult = false;
	try
	{
		string gpgCommand = string("gpgv --status-fd 1 --keyring ") + keyringPath +
				' ' + signaturePath + ' ' + path + " 2>/dev/null";
		string openError;
		File gpgPipe(gpgCommand, "pr", openError);
		if (!openError.empty())
		{
			fatal("unable to open gpg pipe: %s", openError.c_str());
		}

		smatch m;
		auto gpgGetLine = [&gpgPipe, &m, &debugging]() -> string
		{
			static const sregex sigIdRegex = sregex::compile("\\[GNUPG:\\] SIG_ID");
			static const sregex generalRegex = sregex::compile("\\[GNUPG:\\] ");
			string result;
			do
			{
				gpgPipe.getLine(result);
				if (debugging && !gpgPipe.eof())
				{
					debug("fetched '%s' from gpg pipe", result.c_str());
				}
			} while (!gpgPipe.eof() && (
						regex_search(result, m, sigIdRegex, regex_constants::match_continuous) ||
						!regex_search(result, m, generalRegex, regex_constants::match_continuous)));

			if (gpgPipe.eof())
			{
				return "";
			}
			else
			{
				return regex_replace(result, generalRegex, "");
			}
		};


		auto status = gpgGetLine();
		if (status.empty())
		{
			// no info from gpg at all
			fatal("gpg: '%s': no info received", path.c_str());
		}

		// first line ought to be validness indicator
		static const sregex messageRegex = sregex::compile("(\\w+) (.*)");
		if (!regex_match(status, m, messageRegex))
		{
			fatal("gpg: '%s': invalid status string '%s'", path.c_str(), status.c_str());
		}

		string messageType = m[1];
		string message = m[2];

		if (messageType == "GOODSIG")
		{
			string furtherInfo = gpgGetLine();
			if (furtherInfo.empty())
			{
				fatal("gpg: '%s': error: unfinished status", path.c_str());
			}

			if (!regex_match(furtherInfo, m, messageRegex))
			{
				fatal("gpg: '%s': invalid further info string '%s'", path.c_str(), furtherInfo.c_str());
			}

			string furtherInfoType = m[1];
			string furtherInfoMessage = m[2];
			if (furtherInfoType == "VALIDSIG")
			{
				// no comments :)
				verifyResult = 1;
			}
			else if (furtherInfoType == "EXPSIG")
			{
				warn("gpg: '%s': expired signature: %s", path.c_str(), furtherInfoMessage.c_str());
			}
			else if (furtherInfoType == "EXPKEYSIG")
			{
				warn("gpg: '%s': expired key: %s", path.c_str(), furtherInfoMessage.c_str());
			}
			else if (furtherInfoType == "REVKEYSIG")
			{
				warn("gpg: '%s': revoked key: %s", path.c_str(), furtherInfoMessage.c_str());
			}
			else
			{
				warn("gpg: '%s': unknown error: %s %s",
						path.c_str(), furtherInfoType.c_str(), furtherInfoMessage.c_str());
			}
		}
		else if (messageType == "BADSIG")
		{
			warn("gpg: '%s': bad signature: %s", path.c_str(), message.c_str());
		}
		else if (messageType == "ERRSIG")
		{
			// gpg was not able to verify signature

			// maybe, public key was not found?
			bool publicKeyWasNotFound = false;
			auto detail = gpgGetLine();
			if (!detail.empty())
			{
				if (!regex_match(detail, m, messageRegex))
				{
					fatal("gpg: '%s': invalid detailed info string '%s'", path.c_str(), detail.c_str());
				}
				string detailType = m[1];
				string detailMessage = m[2];
				if (detailType == "NO_PUBKEY")
				{
					publicKeyWasNotFound = true;

					// the message looks like
					//
					// NO_PUBKEY D4F5CE00FA0E9B9D
					//
					warn("gpg: '%s': public key '%s' not found", path.c_str(), detailMessage.c_str());
				}
			}

			if (!publicKeyWasNotFound)
			{
				warn("gpg: '%s': could not verify signature: %s", path.c_str(), message.c_str());
			}
		}
		else if (messageType == "NODATA")
		{
			// no signature
			warn("gpg: '%s': empty signature", path.c_str());
		}
		else if (messageType == "KEYEXPIRED")
		{
			warn("gpg: '%s': expired key: %s", path.c_str(), message.c_str());
		}
		else
		{
			warn("gpg: '%s': unknown message received: %s %s",
					path.c_str(), messageType.c_str(), message.c_str());
		}
	}
	catch (Exception&)
	{
		warn("error while verifying signature for file '%s'", path.c_str());
	}

	if (debugging)
	{
		debug("the verify result is %u", (unsigned int)verifyResult);
	}
	return verifyResult;
}

string Cache::getPathOfCopyright(const shared_ptr< const BinaryVersion >& version)
{
	if (!version->isInstalled())
	{
		return string();
	}

	return string("/usr/share/doc/") + version->packageName + "/copyright";
}

string Cache::getPathOfChangelog(const shared_ptr< const BinaryVersion >& version)
{
	if (!version->isInstalled())
	{
		return string();
	}

	const string& packageName = version->packageName;
	const string commonPart = string("/usr/share/doc/") + packageName + "/";
	if (version->versionString.find('-') == string::npos)
	{
		return commonPart + "changelog.gz"; // non-native package
	}
	else
	{
		return commonPart + "changelog.Debian.gz"; // native package
	}
}

bool Cache::memoize = false;

} // namespace

