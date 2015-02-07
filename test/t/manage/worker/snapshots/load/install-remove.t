use TestCupt;
use Test::More tests => 2;
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

save_snapshot('sn');
run3("tar -c var/lib/cupt", \undef, \my $snapshots_tar);

setup_with('aaa' => 1, 'ccc' => 3);

run3("tar -x", \$snapshots_tar);
my $offer = get_first_offer("$cupt snapshot load -V sn");

TODO: {
	local $TODO = 'fix (not) reinstalling same versions';
	is_deeply(get_offered_versions($offer), { 'bbb' => 2 }, 'bbb is removed')
			or diag($offer);
}

