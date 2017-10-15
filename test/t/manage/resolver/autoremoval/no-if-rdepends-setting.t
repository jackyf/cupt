use TestCupt;
use Test::More tests => 4;

my $cupt = TestCupt::setup(
	'dpkg_status' =>
		entail(compose_installed_record('main', '1') . "Depends: slave1 | slave2 | slave3\n") .
		entail(compose_installed_record('slave1', '1')) .
		entail(compose_installed_record('slave2', '1')) .
		entail(compose_installed_record('slave3', '1')),
	'extended_states' =>
		entail(compose_autoinstalled_record('slave2')) .
		entail(compose_autoinstalled_record('slave3'))
);

my $offer = get_first_offer("$cupt install -o cupt::resolver::no-autoremove-if-rdepends-exist::=slave2*");

like($offer, regex_offer(), "no-action install succeeded");
is(get_offered_version($offer, 'slave1'), get_unchanged_version(), "'slave1' is not touched (manually installed)");
is(get_offered_version($offer, 'slave2'), get_unchanged_version(), "'slave2' is not touched (since rdependency exists)");
is(get_offered_version($offer, 'slave3'), get_empty_version(), "'slave3' is autoremoved (none of the above)");

