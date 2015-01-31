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
#ifndef CUPT_FWD_SEEN
#define CUPT_FWD_SEEN

namespace cupt {

class Config;
class Cache;
class File;
class RequiredFile;
class Pipe;
class HashSums;

namespace cache {

class Package;
class BinaryPackage;
class SourcePackage;
struct Version;
struct BinaryVersion;
class SourceVersion;
class ReleaseInfo;

struct Relation;
struct ArchitecturedRelation;
struct RelationExpression;
struct ArchitecturedRelationExpression;

}

namespace download {

class Manager;
class Method;
class Uri;
class Progress;
class ConsoleProgress;

}

namespace system {

class State;
class Resolver;
class NativeResolver;
class Worker;
class Snapshots;

}

}

#endif

