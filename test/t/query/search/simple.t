use TestCupt;
use Test::More tests => 3;

use strict;
use warnings;

my $description1 = "Good big package.";
my $description2 = "Small package.\n Contains 5 modules.";

sub descript {
	return "Description: " . $_[0] . "\n";
}

my $packages = 
		entail(compose_package_record('aadz', '1')) .
		entail(compose_package_record('zzbq', '2')) .
		entail(compose_package_record('aadm', '38') . descript($description1)) .
		entail(compose_package_record('dupl', '10') . descript($description1)) .
		entail(compose_package_record('dupl', '20') . descript($description2));

my $cupt = TestCupt::setup('packages' => $packages);

sub get_line_count {
	return ($_[0] =~ tr/\n//);
}

my $options = '';

sub get_search_result {
	my ($pattern) = @_;
	return `$cupt search $options '$pattern'`;
}

sub eis {
	my ($pattern, $expected) = @_;

	my $out = get_search_result($pattern);
	my $lcount = get_line_count($out);
	is($lcount, $expected, "search of '$pattern' (options: '$options') returns $expected") or
			diag($out);
}

subtest "name search" => sub {
	$options = '-n';

	eis('a', 2);
	eis('aad', 2);
	eis('q', 1);
	eis('y', 0);
	eis('zz', 1);
	eis('zb', 1);

	eis('.*', 4);
	eis('z.', 1);
	eis('b|m', 2);
	eis('aa{2}', 0);
	eis('a{2}', 2);

	eis('dupl', 1);
};

subtest "name and description search" => sub {
	$options = '';

	eis('a', 4);
	eis('package', 3);
	eis('Good', 2);
	eis('good', 2);
	eis('big|small', 3);
	eis('package.*contains', 1);
};

subtest "name and description search, case-sensitive" => sub {
	$options = '--case-sensitive';

	eis('good', 0);
	eis('A', 0);
	eis('Small', 1);
};

