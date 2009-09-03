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

use Test::More tests => 2;

use Cupt::Config;
use Cupt::Download::Method qw(get_acquire_suboption_for_uri);

my $config = Cupt::Config->new();
$config->set_regular_var('acquire::http::proxy' => 'http://host1.com');
$config->set_regular_var('acquire::http::proxy::debian.org.ua' => 'http://otherhost.com');

is(get_acquire_suboption_for_uri($config, 'http://ftp.ua.debian.org', 'proxy'), 'http://host1.com', 'general proxy');
is(get_acquire_suboption_for_uri($config, 'http://debian.org.ua', 'proxy'), 'http://otherhost.com', 'host-specific proxy');

