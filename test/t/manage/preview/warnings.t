use TestCupt;
use Test::More tests => 5;

use strict;
use warnings;

my $cupt = TestCupt::setup(
	'dpkg_status' =>
		entail(compose_installed_record('a', '1') . "Essential: yes\n") .
		entail(compose_installed_record('h', '3', 'on-hold'=>1)),
	'packages2' =>
		[
			[
				'trusted' => 0,
				'content' =>
					entail(compose_package_record('b', '2')) .
					entail(compose_package_record('h', '3')) .
					entail(compose_package_record('h', '4')),
			],
		],
);

like(get_first_offer("$cupt remove a"), qr/warning.*essential.*remov/i, "removing an essential package issues a warning");

my $untrusted_warning_regex = qr/warning.*untrusted/i;
like(get_first_offer("$cupt install b"), $untrusted_warning_regex, "dealing with untrusted packages issues a warning");
unlike(get_first_offer("$cupt install b -o cupt::console::allow-untrusted=yes"),
		$untrusted_warning_regex, "no untrusted warning if explicitly allowed");

my $hold_warning_regex = qr/warning.*hold/i;
like(get_first_offer("$cupt install h=4"), $hold_warning_regex, "changing a version of on-hold package issues a warning");
unlike(get_first_offer("$cupt reinstall h"), $hold_warning_regex, "reinstall of on-hold package doesn't issue a warning");

