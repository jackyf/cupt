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

use Test::More tests => 1;

use Cupt::Config;
use Cupt::Download::Progress;
use Cupt::Download::Manager;

my $target = 'cupt_copy';

my $config = new Cupt::Config;
my $dmanager = new Cupt::Download::Manager($config, new Cupt::Download::Progress);

is($dmanager->download({ 'uri' => "file:cupt", 'filename' => $target }), 0, "download a file");

