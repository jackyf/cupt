use TestCupt;
use Test::More tests => 2;

my $cupt = TestCupt::setup(
	'dpkg_status' => compose_installed_record('abc', '1'),
	'extended_states' => entail(compose_autoinstalled_record('abc'))
);

subtest "autoremoval of leaf package if not disabled" => sub {
	my $offer = get_first_offer("$cupt install");
	like($offer, regex_offer(), "no-action install succeeded");
	is(get_offered_version($offer, 'abc'), get_empty_version(), "'abc' is autoremoved") or
			diag($offer);
};

subtest "no autoremoval of leaf package if disabled" => sub {
	my $offer = get_first_offer("$cupt install --no-auto-remove");
	like($offer, regex_offer(), "no-action install succeeded");
	is(get_offered_version($offer, 'abc'), get_unchanged_version(), "'abc' is unchanged");
};

