#!/usr/bin/perl
BEGIN { push @INC, q(../) }

use strict;
use warnings;

use Test::More tests => 1;

use Cupt::Config;
use Cupt::Download::Manager;

my $target = 'cupt_copy';

my $config = new Cupt::Config;
my $dmanager = new Cupt::Download::Manager($config, new Cupt::Download::Progress);

is($dmanager->download({ 'uri' => "file://../cupt", 'filename' => $target }), 0, "download a file");

