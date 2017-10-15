use TestCupt;
use Test::More tests => 100;

use strict;
use warnings;

sub is_hit {
	my ($index, $density) = @_;

	my $float_density = $density / 100;
	return int($index*$float_density) != int(($index+1)*$float_density);
}

sub generate_space {
	my ($count, $installed_density, $experimental_density, $conflicts_density) = @_;
	my %result;

	my @installed;
	my @normal;
	my @experimental;
	my %expected;

	my %by_type_indexes;

	my @packages = qw(na1 na2);
	foreach my $package (@packages) {
		push @normal, compose_package_record($package, 1);
	}
	foreach my $index (0..$count-1) {
		my $package = sprintf("p%02d", $index);

		my $is_experimental = is_hit($index, $experimental_density);
		my $by_type_index = $by_type_indexes{$is_experimental} // 0;
		$by_type_indexes{$is_experimental}++;

		my $is_installed = is_hit($by_type_index, $installed_density);
		if ($is_experimental) {
			$is_installed = 1;
		}

		if ($is_installed) {
			$package .= 'i';
		}
		if ($is_experimental) {
			$package .= 'e';
		}

		if ($is_installed) {
			push @installed, compose_installed_record($package, 1);
		}
		push @normal, compose_package_record($package, 2);
		if ($is_experimental) {
			my $record = compose_package_record($package, 3);
			$record .= "Recommends: " . $packages[-1] . ', ' . $packages[-2] . "\n";

			my $has_conflicts = is_hit($by_type_index, $conflicts_density);
			if ($has_conflicts) {
				$record .= "Depends: failp\n";
				$expected{$package} = 2;
			} else {
				$expected{$package} = 3;
			}

			push @experimental, $record;
		}

		push @packages, $package;
	}

	$result{'dpkg_status'} = \@installed;
	$result{'releases'} = [
		{ 'archive' => 'unstable', 'packages' => \@normal },
		{ 'archive' => 'experimental', 'packages' => \@experimental },
	];
	$result{'expected'} = \%expected;

	return %result;
}

sub test {
	my %space = generate_space(@_);
	my $cupt = TestCupt::setup(%space);

	my $offer = get_first_offer("$cupt -t experimental full-upgrade");
	my $comment = "c: " . $_[0] . ", ed: " . $_[2] . ", cd: " . $_[3] . ", id: " . $_[1];

	subtest $comment => sub {
		like($offer, regex_offer(), 'resolving succeeded');

		my %expected = %{$space{'expected'}};
		foreach my $package (sort keys %expected) {
			my $expected_version = $expected{$package};
			is(get_offered_version($offer, $package), $expected_version, "$package --> $expected_version")
		}
	}
}

for my $count (100) {
	for my $experimental_density (2, 8, 20, 50, 90) {
		for my $conflicts_density (5, 10, 20, 40) {
			for my $installed_density (99, 90, 80, 60, 20) {
				test($count, $installed_density, $experimental_density, $conflicts_density);
			}
		}
	}
}

