use TestCupt;
use Test::More tests => 19;

use strict;
use warnings;

my $cupt = TestCupt::setup(
	'dpkg_status' =>
		entail(compose_installed_record('eip', '1') . "Essential: yes\n") .
		entail(compose_installed_record('mip', '0')) .
		entail(compose_installed_record('aip', '0')) ,
	'extended_states' =>
		entail(compose_autoinstalled_record('aip')) .
		entail(compose_autoinstalled_record('eip')),
);

sub compose_score_argument {
	my ($name, $value) = @_;
	return defined $value ? "-o cupt::resolver::score::$name=$value" : '';
}

sub get_printable_score {
	my ($value) = @_;
	return $value // 'default';
}

my $request_type;

sub get_first_offer_for {
	my ($r_score, $ra_score, $re_score) = @_;
	
	my $score_arguments = 
			compose_score_argument('removal', $r_score) . ' ' .
			compose_score_argument('removal-of-autoinstalled', $ra_score) . ' ' .
			compose_score_argument('removal-of-essential', $re_score);

	return get_first_offer("$cupt remove $request_type '*' -V $score_arguments --no-auto-remove -o debug::resolver=yes"); 
}

sub eis {
	my ($offer, $package, $result) = @_;

	my $expected_version = $result ? get_unchanged_version() : get_empty_version();
	is(get_offered_version($offer, $package), $expected_version, $package) or diag($offer);
}

sub test {
	my ($r_score, $ra_score, $re_score, $eip_result, $mip_result, $aip_result) = @_;
	my ($package, $score) = @_;

	my $r_score_printable = get_printable_score($r_score);
	my $ra_score_printable = get_printable_score($ra_score);
	my $re_score_printable = get_printable_score($re_score);

	my $comment = "request type: $request_type, " .
			"scores: r=$r_score_printable,ra=$ra_score_printable,re=$re_score_printable, " .
			"expected states: eip=$eip_result, mip=$mip_result, aip=$aip_result";

	my $offer = get_first_offer_for($r_score, $ra_score, $re_score);

	subtest $comment => sub {
		eis($offer, 'eip', $eip_result);
		eis($offer, 'mip', $mip_result);
		eis($offer, 'aip', $aip_result);
	};

}

TODO: {
	local $TODO = 'increase removal-of-automatic option';

	$request_type = '--wish';
	test(undef, undef, undef, 1, 1, 0);
}

$request_type = '--try';
test(undef, undef, undef, 1, 0, 0);

$request_type = '--must';
test(undef, undef, undef, 0, 0, 0);

$request_type = '--importance=0';
test(undef, undef, undef, 1, 1, 1);
test(500, undef, undef, 1, 0, 0);
test(-1000, 1200, undef, 1, 1, 0);
test(undef, undef, -200, 1, 1, 1);
test(50, 50, -50, 0, 0, 0);
test(undef, undef, 2000, 0, 1, 1);
test(-500, undef, 2000, 0, 1, 0);

test(-200, 20, 20, 1, 1, 1);
test(-500, 700, -300, 1, 1, 0);
test(-300, 700, -300, 0, 1, 0);
test(500, 200, -1000, 1, 0, 0);
test(100, -200, 0, 1, 0, 1);
test(4000, 4000, -10000, 1, 0, 0);
test(-2000, 1500, 5000, 0, 1, 1);
test(100, -1000, 1200, 0, 0, 1);
test(100, 200, 300, 0, 0, 0);

