use TestCupt;
use Test::More tests => 3;

my $cupt = TestCupt::setup(
	'dpkg_status' =>
		entail(compose_installed_record('abc', '1')) .
		entail(compose_installed_record('def', '2')),
	'extended_states' =>
		entail(compose_autoinstalled_record('abc')) .
		entail(compose_autoinstalled_record('def'))
);

my $offer = get_first_offer("$cupt install -o apt::neverautoremove::=ab.*");

like($offer, regex_offer(), "no-action install succeeded");
is(get_offered_version($offer, 'abc'), get_unchanged_version(), "'abc' is not touched");
is(get_offered_version($offer, 'def'), get_empty_version(), "'def' is autoremoved");

