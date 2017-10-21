use Test::More tests => 11;

my $cupt = setup(
	'dpkg_status' => [
		compose_installed_record('p', '5'),
		compose_installed_record('a', '1') . "Essential: yes\n",
		compose_installed_record('i', '1') . "Important: yes\n",
		compose_installed_record('h', '3', 'on-hold'=>1),
	],
	'releases' => [
		{
			'trusted' => 0,
			'packages' => [
				compose_package_record('b', '2'),
				compose_package_record('h', '3'),
				compose_package_record('h', '4'),
			],
		},
	],
);

sub test_essential_important {
	my ($text, $arguments, $expected_essential_warning, $expected_important_warning) = @_;
	my $offer = get_first_offer("$cupt $arguments");

	my $remove_of_essential_warning_regex = qr/warning.*essential.*remov/i;
	my $remove_of_important_warning_regex = qr/warning.*important.*remov/i;

	subtest $text => sub {
		if ($expected_essential_warning) {
			like($offer, $remove_of_essential_warning_regex, 'is essential');
		} else {
			unlike($offer, $remove_of_essential_warning_regex, 'is not essential');
		}

		if ($expected_important_warning) {
			like($offer, $remove_of_important_warning_regex, 'is important');
		} else {
			unlike($offer, $remove_of_important_warning_regex, 'is not important');
		}
	}
}

test_essential_important("removing an essential package", "remove a" => 1, 0);
test_essential_important("removing an important package", "remove i" => 0, 1);
test_essential_important("removing a mundane package", "remove p" => 0, 0);
test_essential_important("removing an essential package, suppressing essentialness warnings",
		"remove a -o cupt::console::warnings::removal-of-essential=no" => 0, 0);
test_essential_important("removing an essential package, suppressing importantness warnings",
		"remove a -o cupt::console::warnings::removal-of-important=no" => 1, 0);
test_essential_important("removing an imporant package, suppressing importantness warnings",
		"remove i -o cupt::console::warnings::removal-of-important=no" => 0, 0);
test_essential_important("removing an imporant package, suppressing essentialness warnings",
		"remove i -o cupt::console::warnings::removal-of-essential=no" => 0, 1);

my $untrusted_warning_regex = qr/warning.*untrusted/i;
like(get_first_offer("$cupt install b"), $untrusted_warning_regex, "dealing with untrusted packages issues a warning");
unlike(get_first_offer("$cupt install b -o cupt::console::allow-untrusted=yes"),
		$untrusted_warning_regex, "no untrusted warning if explicitly allowed");

my $hold_warning_regex = qr/warning.*hold/i;
like(get_first_offer("$cupt install h=4"), $hold_warning_regex, "changing a version of on-hold package issues a warning");
unlike(get_first_offer("$cupt reinstall h"), $hold_warning_regex, "reinstall of on-hold package doesn't issue a warning");

