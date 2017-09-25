use TestCupt;
use Test::More tests => 1 + 8;
use IPC::Run3;

use strict;
use warnings;

my $cupt;

eval get_inc_code('../common');


sub compose_dpkg_status {
	my %input = @_;
	my @records = map { entail(compose_installed_record($_, $input{$_})) } (keys %input);
	return join('', @records);
}

sub setup_with {
	$cupt = TestCupt::setup('dpkg_status' => compose_dpkg_status(@_));
}


setup_with('aaa' => 1, 'bbb' => 2, 'ccc' => 3);

save_snapshot($cupt, 'sn');
run3("tar -c var/lib/cupt", \undef, \my $snapshots_tar);


sub test_load_changes {
	my ($setup_params, $expected_offered_versions, $description) = @_;

	setup_with(@$setup_params);

	run3("tar -x", \$snapshots_tar);
	my $offer = get_first_offer("$cupt snapshot load -V sn");

	is_deeply(get_offered_versions($offer), $expected_offered_versions, $description)
			or diag($offer);
}

my $ev = get_empty_version();

test_load_changes(
		[ 'aaa' => 1, 'ccc' => 3 ],
		{ 'bbb' => 2 },
		'bbb installed'
);
test_load_changes(
		[ 'aaa' => 1 ],
		{ 'bbb' => 2, 'ccc' => 3 },
		'bbb and ccc installed'
);
test_load_changes(
		[ 'aaa' => 1, 'bbb' => 2, 'ccc' => 3, 'ddd' => 4 ],
		{ 'ddd' => $ev },
		'ddd removed'
);
test_load_changes(
		[ 'aa0' => 10, 'aaa' => 1, 'bbb' => 2, 'ccc' => 3, 'xxx' => 4 ],
		{ 'aa0' => $ev, 'xxx' => $ev },
		'aa0 and xxx removed'
);
test_load_changes(
		[ 'bbb' => 2, 'ddd' => 4 ],
		{ 'aaa' => 1, 'ccc' => 3, 'ddd' => $ev },
		'aaa and ccc restored, ddd removed'
);
test_load_changes(
		[ 'aaa' => 1, 'bbb' => 20, 'ccc' => 3 ],
		{ 'bbb' => 2 },
		'bbb downgraded',
);
test_load_changes(
		[ 'aaa' => '1~rc5', 'bbb' => 2, 'ccc' => 3 ],
		{ 'aaa' => 1 },
		'aaa upgraded'
);
test_load_changes(
		[ 'aaa' => 1, 'ccc' => 30, 'ddd' => 4 ],
		{ 'bbb' => 2, 'ccc' => 3, 'ddd' => $ev },
		'the mix'
);

