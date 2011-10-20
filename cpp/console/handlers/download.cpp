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
#include <iostream>
using std::cout;
using std::endl;

#include "../common.hpp"
#include "../handlers.hpp"
#include "../selectors.hpp"

#include <cupt/cache/sourceversion.hpp>
#include <cupt/cache/releaseinfo.hpp>
#include <cupt/system/state.hpp>
#include <cupt/download/manager.hpp>

string getCodenameAndComponentString(const Version& version, const string& baseUri)
{
	vector< string > parts;
	FORIT(sourceIt, version.sources)
	{
		auto releaseInfo = sourceIt->release;
		if (releaseInfo->baseUri != baseUri)
		{
			continue;
		}
		parts.push_back(releaseInfo->codename + '/' + releaseInfo->component);
	}
	return join(",", parts);
}

int downloadSourcePackage(Context& context)
{
	auto config = context.getConfig();
	vector< string > arguments;
	bpo::options_description options("");
	options.add_options()
		("download-only,d", "")
		("tar-only", "")
		("diff-only", "")
		("dsc-only", "");

	auto variables = parseOptions(context, options, arguments);

	if (arguments.empty())
	{
		fatal("no source package expressions specified");
	}

	if (!shellMode)
	{
		Version::parseInfoOnly = false;
		Version::parseRelations = false;
	}
	auto cache = context.getCache(true, true, true);

	vector< SourceVersion::FileParts::Type > partsToDownload = {
		SourceVersion::FileParts::Tarball,
		SourceVersion::FileParts::Diff,
		SourceVersion::FileParts::Dsc
	};

	bool downloadOnly = false;
	if (variables.count("download-only"))
	{
		downloadOnly = true;
	}
	if (variables.count("tar-only"))
	{
		partsToDownload = { SourceVersion::FileParts::Tarball };
		downloadOnly = true;
	}
	if (variables.count("diff-only"))
	{
		partsToDownload = { SourceVersion::FileParts::Diff };
		downloadOnly = true;
	}
	if (variables.count("dsc-only"))
	{
		partsToDownload = { SourceVersion::FileParts::Dsc };
		downloadOnly = true;
	}

	vector< Manager::DownloadEntity > downloadEntities;
	vector< string > dscFilenames;

	FORIT(argumentIt, arguments)
	{
		auto versions = selectSourceVersionsWildcarded(cache, *argumentIt);

		FORIT(it, versions)
		{
			const shared_ptr< const SourceVersion >& version = *it;

			const string& packageName = version->packageName;
			const string& versionString = version->versionString;

			auto downloadInfo = version->getDownloadInfo();

			FORIT(partIt, partsToDownload)
			{
				const vector< Version::FileRecord >& fileRecords = version->files[*partIt];
				FORIT(fileRecordIt, fileRecords)
				{
					const Version::FileRecord& record = *fileRecordIt;

					Manager::DownloadEntity downloadEntity;
					const string& filename = record.name;
					if (*partIt == SourceVersion::FileParts::Dsc)
					{
						dscFilenames.push_back(filename); // for unpacking after
					}
					FORIT(downloadInfoIt, downloadInfo)
					{
						string shortAlias = packageName + ' ' + SourceVersion::FileParts::strings[*partIt];
						string longAlias = sf("%s %s %s %s %s", downloadInfoIt->baseUri.c_str(),
								getCodenameAndComponentString(*version, downloadInfoIt->baseUri).c_str(),
								packageName.c_str(), versionString.c_str(),
								SourceVersion::FileParts::strings[*partIt].c_str());
						string uri = downloadInfoIt->baseUri + '/' +
								downloadInfoIt->directory + '/' + filename;

						downloadEntity.extendedUris.push_back(
								Manager::ExtendedUri(uri, shortAlias, longAlias));
					}
					downloadEntity.targetPath = filename;
					downloadEntity.size = record.size;
					downloadEntity.postAction = [record, filename]() -> string
					{
						if (!record.hashSums.verify(filename))
						{
							if (unlink(filename.c_str()) == -1)
							{
								warn("unable to delete file '%s': EEE", filename.c_str());
							}
							return __("hash sums mismatch");
						}
						return "";
					};

					downloadEntities.push_back(std::move(downloadEntity));
				}
			}
		}
	}


	{ // downloading
		auto downloadProgress = getDownloadProgress(config);
		Manager downloadManager(config, downloadProgress);
		auto downloadError = downloadManager.download(downloadEntities);
		if (!downloadError.empty())
		{
			fatal("there were download errors");
		}
	}; // make sure that download manager is already destroyed at this point

	if (!downloadOnly)
	{
		// unpack downloaded sources
		FORIT(filenameIt, dscFilenames)
		{
			string command = "dpkg-source -x " + *filenameIt;
			if (::system(command.c_str()))
			{
				warn("dpkg-source on file '%s' failed", filenameIt->c_str());
			}
		}
	}

	// all's good
	return 0;
}

int downloadChangelogOrCopyright(Context& context, ChangelogOrCopyright::Type type)
{
	if (!shellMode)
	{
		Version::parseInfoOnly = false;
		Version::parseRelations = false;
	}

	auto config = context.getConfig();

	vector< string > arguments;
	bpo::options_description options("");
	options.add_options()
		("installed-only", "");
	auto variables = parseOptions(context, options, arguments);

	if (arguments.empty())
	{
		fatal("no binary package expressions specified");
	}

	auto cache = context.getCache(false, !variables.count("installed-only"), true);

	const string typeString = (type == ChangelogOrCopyright::Changelog ?
			"changelog" : "copyright");
	const map< string, string > baseUrisByVendor = {
		{ "Debian", "http://packages.debian.org/changelogs/pool" },
		{ "Ubuntu", "http://changelogs.ubuntu.com/changelogs/pool" },
		// yes, 'changelogs' even for copyrights :)
	};

	string pagerProgram;
	{
		auto systemState = cache->getSystemState();
		auto installedStatus = State::InstalledRecord::Status::Installed;

		auto sensibleUtilsInstalledInfo = systemState->getInstalledInfo("sensible-utils");
		if (sensibleUtilsInstalledInfo && sensibleUtilsInstalledInfo->status == installedStatus)
		{
			pagerProgram = "sensible-pager";
		}
		else
		{
			auto lessInstalledInfo = systemState->getInstalledInfo("less");
			if (lessInstalledInfo && lessInstalledInfo->status == installedStatus)
			{
				pagerProgram = "less";
			}
			else
			{
				pagerProgram = "cat";
			}
		}
	}

	FORIT(argumentIt, arguments)
	{
		auto versions = selectBinaryVersionsWildcarded(cache, *argumentIt);

		FORIT(versionIt, versions)
		{
			const shared_ptr< const BinaryVersion >& version = *versionIt;
			string localTargetPath;
			if (type == ChangelogOrCopyright::Changelog)
			{
				localTargetPath = Cache::getPathOfChangelog(version);
			}
			else
			{
				localTargetPath = Cache::getPathOfCopyright(version);
			}

			if (!localTargetPath.empty())
			{
				// there is a local changelog/copyright, display it
				auto preparedCommand = (type == ChangelogOrCopyright::Changelog ? "zcat" : "cat");
				auto result = ::system(sf("%s %s | %s",
						preparedCommand, localTargetPath.c_str(), pagerProgram.c_str()).c_str());
				// return non-zero code in case of some error
				if (result)
				{
					return result;
				}
			}
			else
			{
				Manager::DownloadEntity downloadEntity;
				FORIT(it, version->sources)
				{
					if (it->release->vendor != "Debian" && it->release->vendor != "Ubuntu")
					{
						// this is probably not a package from Debian or Ubuntu archive
						continue;
					}

					// all following is only good guessings
					string sourceVersionString = version->sourceVersionString;
					{ // a hack to work around dropping epoch on Debian/Ubuntu web links
						auto position = sourceVersionString.find(':');
						if (position != string::npos)
						{
							sourceVersionString = sourceVersionString.substr(position+1);
						}
					}
					const string& sourcePackageName = version->sourcePackageName;
					string shortPrefix = sourcePackageName.compare(0, 3, "lib") ?
							sourcePackageName.substr(0, 1) : sourcePackageName.substr(0, 4);


					string uri = baseUrisByVendor.find(it->release->vendor)->second + '/' +
							it->release->component + '/' + shortPrefix + '/' +
							sourcePackageName + '/' + sourcePackageName + '_' +
							sourceVersionString + '/' + typeString;

					const string& shortAlias = version->packageName;
					string longAlias = version->packageName + ' ' + version->versionString + ' ' + typeString;
					downloadEntity.extendedUris.push_back(Manager::ExtendedUri(
							uri, shortAlias, longAlias));
				}

				if (!downloadEntity.extendedUris.empty())
				{
					downloadEntity.size = (size_t)-1;

					char tempFilename[] = "cupt-download-XXXXXX";
					if (mkstemp(tempFilename) == -1)
					{
						fatal("unable to create a temporary file: mkstemp failed: EEE");
					}

					try
					{
						downloadEntity.targetPath = tempFilename;
						downloadEntity.postAction = []()
						{
							return string(); // no action
						};

						string downloadError;
						{ // downloading
							auto downloadProgress = getDownloadProgress(config);
							Manager downloadManager(config, downloadProgress);
							downloadError = downloadManager.download(
									vector< Manager::DownloadEntity >{ downloadEntity });

						}
						if (!downloadError.empty())
						{
							fatal("there were download errors");
						}

						auto viewResult = ::system((pagerProgram + ' ' + tempFilename).c_str());

						// remove the file
						if (unlink(tempFilename) == -1)
						{
							fatal("unable to delete file '%s': EEE", tempFilename);
						}

						// return non-zero code in case of some error
						if (viewResult)
						{
							return viewResult;
						}
					}
					catch (...)
					{
						unlink(tempFilename); // without checking errors
						throw;
					}
				}
				else
				{
					fatal("no info where to acquire %s for version '%s' of package '%s'",
							typeString.c_str(), version->versionString.c_str(),
							version->packageName.c_str());
				}
			}
		}
	}

	return 0;
}

