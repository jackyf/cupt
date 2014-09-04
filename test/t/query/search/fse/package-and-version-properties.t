use TestCupt;
use Test::More tests => 10;

use strict;
use warnings;

my $installed = entail(compose_installed_record('i', '1') . <<END);
Priority: extra
Maintainer: One Two
Special-property-field: Special String
END

$installed .= entail(compose_installed_record('a', '3') . <<'END' );
Priority: standard
Section: doc
Maintainer: One Three <one.three@mail.tld>
END

my $packages =
		entail(compose_package_record('p', '2'));

my $cupt = TestCupt::setup(
	'dpkg_status' => $installed,
	'packages' => $packages,
	'extended_states' => compose_autoinstalled_record('a')
);

eval(get_inc_code('FSE'));

eis('package:installed()', qw(i a));
eis('package:automatically-installed()', qw(a));

eis('trusted', qw(p));
eis('priority(extra)', qw(i p));
eis('priority(standard)', qw(a));
eis('maintainer(/One.*/)', qw(a i));
eis('maintainer(.*mail.*)', qw(a));
eis('section(//)', qw(i p));
eis('section(doc)', qw(a));
eis('field(Special-property-field, /.+/)', qw(i));

