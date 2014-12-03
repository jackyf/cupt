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
use Test::More;
use TestCupt;

use strict;
use warnings;

my @data = (
	[ '1.2.3', '1.2.3', 0 ], # identical
	[ '4.4.3-2', '4.4.3-2', 0 ], # identical
	[ '1:2ab:5', '1:2ab:5', 0 ], # this is correct...
	[ '7:1-a:b-5', '7:1-a:b-5', 0 ], # and this
	[ '57:1.2.3abYZ+~-4-5', '57:1.2.3abYZ+~-4-5', 0 ], # and those too
	[ '1.2.3', '0:1.2.3', 0 ], # zero epoch
	[ '1.2.3', '1.2.3-0', 0 ], # zero revision
	[ '009', '9', 0 ], # zeroes...
	[ '009ab5', '9ab5', 0 ], # there as well
	[ '1.2.3', '1.2.3-1', -1 ], # added non-zero revision
	[ '1.2.3', '1.2.4', -1 ], # just bigger
	[ '1.2.4', '1.2.3', 1 ], # order doesn't matter
	[ '1.2.24', '1.2.3', 1 ], # bigger, eh?
	[ '0.10.0', '0.8.7', 1 ], # bigger, eh?
	[ '3.2', '2.3', 1 ], # major number rocks
	[ '1.3.2a', '1.3.2', 1 ], # letters rock
	[ '0.5.0~git', '0.5.0~git2', -1 ], # numbers rock
	[ '2a', '21', -1 ], # but not in all places
	[ '1.3.2a', '1.3.2b', -1 ], # but there is another letter
	[ '1:1.2.3', '1.2.4', 1 ], # epoch rocks
	[ '1:1.2.3', '1:1.2.4', -1 ], # bigger anyway
	[ '1.2a+~bCd3', '1.2a++', -1 ], # tilde doesn't rock
	[ '1.2a+~bCd3', '1.2a+~', 1 ], # but first is longer!
	[ '5:2', '304-2', 1 ], # epoch rocks
	[ '5:2', '304:2', -1 ], # so big epoch?
	[ '25:2', '3:2', 1 ], # 25 > 3, obviously
	[ '1:2:123', '1:12:3', -1 ], # 12 > 2
	[ '1.2-5', '1.2-3-5', -1 ], # 1.2 < 1.2-3
	[ '5.10.0', '5.005', 1 ], # preceding zeroes don't matters
	[ '3a9.8', '3.10.2', -1 ], # letters are before all letter symbols
	[ '3a9.8', '3~10', 1 ], # but after the tilde
	[ '1.4+OOo3.0.0~', '1.4+OOo3.0.0-4', -1 ], # another tilde check
	[ '2.4.7-1', '2.4.7-z', -1 ], # revision comparing
	[ '1.002-1+b2', '1.00', 1 ], # whatever...
	[ '2.2.4-47978_Debian_lenny', '2.2.4-47978_Debian_lenny', 0 ], # underscore in revision
	[ '2_4', '2_5', -1 ], # underscore in upstream
);

plan tests => (scalar @data * 3);


sub test_is_correct {
	my $version = shift;

	my $cupt = TestCupt::setup('packages' => entail(compose_package_record('bb', $version)));
	
	my $command = "$cupt show bb";
	is(0, exitcode($command), "correctness of version '$version'")
			or diag(stdall($command));
}

sub test_comparison {
	my ($v1, $v2, $expected_result) = @_;

	my %relations = (0 => '=', 1 => '>>', -1 => '<<');
	my $relation = $relations{$expected_result};

	my $cupt = TestCupt::setup(
		'packages' =>
			entail(compose_package_record('eee', 0) . "Depends: fff ($relation $v2)\n") .
			entail(compose_package_record('fff', $v1))
	);

	my $output = stdout("$cupt depends eee --recurse");

	like($output, qr/^\Qfff $v1\E/m, "'$v1' $relation '$v2'");
}

foreach (@data) {
	my ($v1, $v2, $expected_result) = @$_;

	local $TODO = 'fix underscores' if ($v1 =~ m/_/);

	test_is_correct($v1);
	test_is_correct($v2);
	test_comparison($v1, $v2, $expected_result);
}

