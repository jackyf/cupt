use TestCupt;
use Test::More tests => 6;

use strict;
use warnings;

sub generate_100_installed_packages {
	return join('', map { entail(compose_installed_record("p$_", 1)) } (1..100));
}

sub generate_n_package_list {
	my ($count) = @_;
	return join(', ', map { "p$_" } (1..$count));
}

sub setup_cupt {
	return TestCupt::setup(
		'dpkg_status' =>
			generate_100_installed_packages(),
		'packages2' =>
			[
				{
					'archive' => 'eexxpp',
					'not-automatic' => 1,
					'content' =>
						entail(compose_package_record('aa', '66')),
				},
				{
					'archive' => 'normal',
					'content' =>
						entail(compose_package_record('aa', '5') . 'Breaks: ' . generate_n_package_list(@_) . "\n")
				},
			],
	);
}

sub test {
	my ($count, $expected_exp) = @_;

	my $cupt = setup_cupt($count);
	my $offer = get_first_offer("$cupt install --select=flexible aa -V -o debug::resolver=yes");

	my $expected_aa_version = $expected_exp ? 66 : 5;
	my $comment = "breaks $count packages, expecting package from eexxpp: $expected_exp";
	is(get_offered_version($offer, 'aa'), $expected_aa_version, $comment) or diag($offer);
}

test(1 => 0);
test(2 => 0);
test(3 => 1);
test(5 => 1);
test(10 => 1);
test(20 => 1);
					
