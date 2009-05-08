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
BEGIN { unshift @INC, q(../) }

use strict;
use warnings;

use Test::More;

use Cupt::Core;

my $test_count = 0;

my @nonnative_version_strings = (
	'4.4.3-2', '7:1-a:b-5', '57:1.2.3abYZ+~-4-5', '0.10.0-0ubuntu1', '0.8.7-12',
	'1.2-5', '1.2-3-5', '1.4+OOo3.0.0-4', '2.4.7-1', '2.4.7-z', '1.002-1+b2'
);

my @native_version_strings = (
	'1.2.3', '304:2', '1.2a+~bCd3', '1.2a++', '1.2a+~', '009ab5', '9ab5', '0',
	'1:2ab:5', '009', '1.2.3+nmu2'
);

$test_count += scalar @native_version_strings;
$test_count += scalar @nonnative_version_strings;

plan tests => $test_count;

foreach my $item (@native_version_strings) {
	ok(Cupt::Core::is_version_string_native($item));
}
foreach my $item (@nonnative_version_strings) {
	ok(not Cupt::Core::is_version_string_native($item));
}
