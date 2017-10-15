# bug: https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=754480

use TestCupt;
use Test::More tests => 5;

use strict;
use warnings;

my $bad_relation = "Recommends: bad1, bad2\n";

sub test {
	my ($lower_pin) = @_;

	my $cupt = TestCupt::setup(
		'dpkg_status' =>
			entail(compose_installed_record('ppp', 1)),
		'packages' =>
			entail(compose_package_record('ppp', 2) . $bad_relation) .
			entail(compose_package_record('ppp', 5) . "Depends: newdep\n" . $bad_relation) .
			entail(compose_package_record('newdep', 3)),
		'preferences' =>
			compose_version_pin_record('ppp', 5, $lower_pin),
	);

	my $offer = get_first_offer("$cupt full-upgrade -o debug::resolver=yes");

	my $comment = "bringing new normal-pin dependency doens't give order advantage to lower-pin ($lower_pin) upgrade";
	is(get_offered_version($offer, 'ppp'), 2, $comment) or diag($offer);
}

test(120);
test(200);
test(300);
test(400);
test(495);

