use TestCupt;
use Test::More tests => 3;

use strict;
use warnings;

my $config_files_record = <<END;
Package: libsvn-perl
Status: deinstall ok config-files
Architecture: amd64
Version: 1.7.9-1+nmu4
END

my $dpkg_status =
		entail(compose_installed_record('abc', '1')) .
		entail($config_files_record);

my $cupt = TestCupt::setup('dpkg_status' => $dpkg_status);

is(exitcode("$cupt show abc"), 0, "there is 'abc' binary package");
isnt(exitcode("$cupt show abd"), 0, "there is no 'abd' binary package");
isnt(exitcode("$cupt show libsvn-perl"), 0, "there is no 'svn-perl' binary package");
