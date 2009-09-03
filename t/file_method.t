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
use strict;
use warnings;

BEGIN { unshift @INC, q(.) }

use Test::More tests => 1;

use Cupt::Config;
use Cupt::Download::Progress;
use Cupt::Download::Manager;


my $config = Cupt::Config->new();
my $dmanager = Cupt::Download::Manager->new($config, Cupt::Download::Progress->new());

my $source_file = 'cupt';
my $target_file = 'cupt_copy';

my $sub_post_check = sub { return system("cmp $source_file $target_file") };

is($dmanager->download(
		{
			'uri-entries' => [ { 'uri' => "file:$source_file" } ],
			'filename' => $target_file,
			'post-action' => $sub_post_check
		})
		, 0, 'download a file');

