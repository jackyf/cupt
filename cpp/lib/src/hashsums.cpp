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
#include <gcrypt.h>

#include <cupt/hashsums.hpp>
#include <cupt/file.hpp>

namespace cupt {

namespace {

struct Source
{
	enum Type { File, Buffer };
};

class GcryptHasher
{
	gcry_md_hd_t __gcrypt_handle;
	size_t __digest_size;
 public:
	GcryptHasher(int gcryptAlgorithm)
	{
		gcry_error_t gcryptError;
		if ((gcryptError = gcry_md_open(&__gcrypt_handle, gcryptAlgorithm, 0)))
		{
			fatal2(__("unable to open a gcrypt hash handle: %s"), gcry_strerror(gcryptError));
		}
		__digest_size = gcry_md_get_algo_dlen(gcryptAlgorithm);
	}
	void process(const char* buffer, size_t size)
	{
		gcry_md_write(__gcrypt_handle, buffer, size);
	}
	string getResult() const
	{
		auto binaryResult = gcry_md_read(__gcrypt_handle, 0);
		string result;

		result.reserve(__digest_size * 2);
		// converting to hexadecimal string
		for (size_t i = 0; i < __digest_size; ++i)
		{
			static const char fourBitToHex[] = "0123456789abcdef";
			unsigned int c = binaryResult[i];
			result += fourBitToHex[c >> 4]; // high halfbit
			result += fourBitToHex[c & 0xf]; // low halfbit
		}

		return result;
	}
	~GcryptHasher()
	{
		gcry_md_close(__gcrypt_handle);
	}
};

string __get_hash(HashSums::Type hashType, Source::Type sourceType, const string& source)
{
	static bool initialized = false;
	if (!initialized)
	{
		gcry_check_version(NULL);
		gcry_control (GCRYCTL_DISABLE_SECMEM, 0);
		gcry_control (GCRYCTL_INITIALIZATION_FINISHED, 0);
		initialized = true;
	}

	int gcryptAlgorithm;
	switch (hashType)
	{
		case HashSums::MD5: gcryptAlgorithm = GCRY_MD_MD5; break;
		case HashSums::SHA1: gcryptAlgorithm = GCRY_MD_SHA1; break;
		case HashSums::SHA256: gcryptAlgorithm = GCRY_MD_SHA256; break;
		default:
			gcryptAlgorithm = 0; // to not see 'maybe used uninitialized' warning
			fatal2(__("unsupported hash type '%zu'"), size_t(hashType));
	}

	string result;
	try
	{
		GcryptHasher gcryptHasher(gcryptAlgorithm);

		if (sourceType == Source::File)
		{
			RequiredFile file(source, "r");

			while (auto rawBuffer = file.getBlock(8192))
			{
				gcryptHasher.process(rawBuffer.data, rawBuffer.size);
			}
		}
		else // string
		{
			gcryptHasher.process(source.c_str(), source.size());
		}

		result = gcryptHasher.getResult();
	}
	catch (Exception& e)
	{
		static string strings[HashSums::Count] = { "md5", "sha1", "sha256" };
		string description = string(sourceType == Source::File ? "file" : "string") +
				" '" + source + "'";
		fatal2(__("unable to compute hash sums '%s' on '%s'"), strings[hashType], description);
	}

	return result;
}

void __assert_not_empty(const HashSums* hashSums)
{
	if (hashSums->empty())
	{
		fatal2(__("no hash sums specified"));
	}
}

}

bool HashSums::empty() const
{
	for (size_t type = 0; type < Count; ++type)
	{
		if (!values[type].empty())
		{
			return false;
		}
	}
	return true;
}

string& HashSums::operator[](const Type& type)
{
	return values[type];
}

const string& HashSums::operator[](const Type& type) const
{
	return values[type];
}

bool HashSums::verify(const string& path) const
{
	__assert_not_empty(this);

	size_t sumsCount = 0;

	for (size_t type = 0; type < Count; ++type)
	{
		if (values[type].empty())
		{
			// skip
			continue;
		}

		++sumsCount;

		string fileHashSum = __get_hash(static_cast<Type>(type), Source::File, path);

		if (fileHashSum != values[type])
		{
			// wrong hash sum
			return false;
		}
	}

	return true;
}

void HashSums::fill(const string& path)
{
	for (size_t type = 0; type < Count; ++type)
	{
		values[type]= __get_hash(static_cast<Type>(type), Source::File, path);
	}
}

bool HashSums::match(const HashSums& other) const
{
	__assert_not_empty(this);
	__assert_not_empty(&other);

	size_t comparesCount = 0;

	for (size_t i = 0; i < Count; ++i)
	{
		if (values[i].empty() || other.values[i].empty())
		{
			continue;
		}

		++comparesCount;
		if (values[i] != other.values[i])
		{
			return false;
		}
	}

	return comparesCount;
}

string HashSums::getHashOfString(const Type& type, const string& pattern)
{
	return __get_hash(type, Source::Buffer, pattern);
}

}

