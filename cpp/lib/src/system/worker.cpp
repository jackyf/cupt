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
#include <internal/worker/archives.hpp>
#include <internal/worker/metadata.hpp>
#include <internal/worker/setupandpreview.hpp>
#include <internal/worker/packages.hpp>

namespace cupt {
namespace internal {

class WorkerImpl: public ArchivesWorker, public MetadataWorker, public SetupAndPreviewWorker, public PackagesWorker
{
 public:
	WorkerImpl(const shared_ptr< const Config >& config, const shared_ptr< const Cache >& cache);
};

WorkerImpl::WorkerImpl(const shared_ptr< const Config >& config, const shared_ptr< const Cache >& cache)
	: WorkerBase(config, cache)
{}

}

namespace system {

Worker::Worker(const shared_ptr< const Config >& config, const shared_ptr< const Cache >& cache)
	: __impl(new internal::WorkerImpl(config, cache))
{}

Worker::~Worker()
{
	delete __impl;
}

void Worker::setDesiredState(const Resolver::SuggestedPackages& desiredState)
{
	__impl->setDesiredState(desiredState);
}

shared_ptr< const Worker::ActionsPreview > Worker::getActionsPreview() const
{
	return __impl->getActionsPreview();
}

map< string, ssize_t > Worker::getUnpackedSizesPreview() const
{
	return __impl->getUnpackedSizesPreview();
}

pair< size_t, size_t > Worker::getDownloadSizesPreview() const
{
	return __impl->getDownloadSizesPreview();
}

void Worker::setAutomaticallyInstalledFlag(const string& packageName, bool value)
{
	__impl->markAsAutomaticallyInstalled(packageName, value);
}

void Worker::changeSystem(const shared_ptr< download::Progress >& progress)
{
	__impl->changeSystem(progress);
}

void Worker::updateReleaseAndIndexData(const shared_ptr< download::Progress >& progress)
{
	__impl->updateReleaseAndIndexData(progress);
}

vector< pair< string, shared_ptr< const BinaryVersion > > > Worker::getArchivesInfo() const
{
	return __impl->getArchivesInfo();
}

void Worker::deleteArchive(const string& path)
{
	return __impl->deleteArchive(path);
}

}
}

