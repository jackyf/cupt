#!/usr/bin/perl
#***************************************************************************
#*   Copyright (C) 2008-2009 by Eugene V. Lyubimkin                        *
#*                                                                         *
#*   This program is free software; you can redistribute it and/or modify  *
#*   it under the terms of the GNU General Public License                  *
#*   (version 3 or above) as published by the Free Software Foundation.    *
#*                                                                         *
#*   This program is distributed in the hope that it will be useful,       *
#*   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
#*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
#*   GNU General Public License for more details.                          *
#*                                                                         *
#*   You should have received a copy of the GNU GPL                        *
#*   along with this program; if not, write to the                         *
#*   Free Software Foundation, Inc.,                                       *
#*   51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA               *
#*                                                                         *
#*   This program is free software; you can redistribute it and/or modify  *
#*   it under the terms of the Artistic License, which comes with Perl     *
#***************************************************************************
BEGIN { unshift @INC, q(./) }

use strict;
use warnings;

use Test::More;

use Cupt::Core;

my $test_count = 0;


my $sums1 = { 'md5sum' => 'aaa', 'sha1sum' => 'bbb', 'sha256sum' => 'ccc' };
my $sums2 = { 'md5sum' => 'aaa' };
my $sums3 = { 'sha1sum' => 'bbb', 'sha256sum' => 'ccc' };
my $sums_another = { 'md5sum' => 'aaa', 'sha256sum' => 'ddd' };

plan tests => 6;

ok(compare_hash_sums($sums1, $sums2));
ok(compare_hash_sums($sums1, $sums3));
ok(not compare_hash_sums($sums2, $sums3));
ok(compare_hash_sums($sums2, $sums_another));
ok(not compare_hash_sums($sums1, $sums_another));
ok(not compare_hash_sums($sums3, $sums_another));

