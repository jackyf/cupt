use TestCupt;
use Test::More tests => 11;

use strict;
use warnings;

sub setup_cupt {
	my $input = shift;
	my @packages;

	my $current_release = '';

	foreach my $record (@$input) {
		my $release = $record->[2];
		if ($release ne $current_release) {
			push @packages, { 'archive' => $release, 'hostname' => $release, 'content' => '' };
			$current_release = $release;
		}

		my $version = $record->[0];
		my $hash = $record->[1];
		$packages[-1]->{'content'} .= entail(compose_package_record('pkg', $version, 'sha' => $hash));
	}

	return TestCupt::setup('packages2' => \@packages);
}

sub extract_version_and_uri {
	my $record = shift;

	my ($version) = ($record =~ m/^Version: (.*)$/m);
	my (@uris) = ($record =~ m{^URI: .*//(.*)//$}mg);
	return [ $version, [ @uris ] ];
}

sub output_from_raw {
	my $input = shift;

	return [ map { extract_version_and_uri($_) } split("\n\n", $input) ];
}

sub test {
	my ($comment, $input, $expected_output) = @_;

	my $cupt = setup_cupt($input);
	my $raw_output = stdall("$cupt show -a pkg");

	my $output = output_from_raw($raw_output);

	is_deeply($output, $expected_output, $comment) or diag($raw_output);
}

test('one simple version',
	[
		[ '1', '22a', 'm' ]
	]
	=>
	[
		[ '1', [ 'm' ] ],
	]);

test('same version, same hash, different releases',
	[
		[ '3', '89b', 'x' ],
		[ '3', '89b', 'y' ]
	]
	=>
	[
		[ '3', [ 'x', 'y' ] ],
	]);

test('same release, different versions, different hashes',
	[
		[ '1', '111', 'x' ],
		[ '2', '222', 'x' ],
	]
	=>
	[
		[ '2', [ 'x' ] ],
		[ '1', [ 'x' ] ],
	]);

test('different versions, hashes and releases',
	[
		[ '1', '111', 'x' ],
		[ '2', '222', 'y' ],
	]
	=>
	[
		[ '2', [ 'y' ] ],
		[ '1', [ 'x' ] ],
	]);

test('different versions, same hash',
	[
		[ '1', 'ccc', 'x' ],
		[ '2', 'ccc', 'y' ],
	]
	=>
	[
		[ '2', [ 'y' ] ],
		[ '1', [ 'x' ] ],
	]);

test('same version, different hashes',
	[
		[ '1', 'eee', 'x' ],
		[ '1', 'fff', 'y' ],
	]
	=>
	[
		[ '1', [ 'x' ] ],
		[ '1^dhs0', [ 'y' ] ],
	]);

test('hash mismatch, third same version matches first one',
	[
		[ '1', 'eee', 'x' ],
		[ '1', 'fff', 'y' ],
		[ '1', 'eee', 'z' ],
	]
	=>
	[
		[ '1', [ 'x', 'z' ] ],
		[ '1^dhs0', [ 'y' ] ],
	]);

test('hash mismatch, third same version matches second one',
	[
		[ '1', 'eee', 'x' ],
		[ '1', 'fff', 'y' ],
		[ '1', 'fff', 'z' ],
	]
	=>
	[
		[ '1', [ 'x' ] ],
		[ '1^dhs0', [ 'y', 'z' ] ],
	]);

test("hash mismatch, third same version doesn't match previous ones",
	[
		[ '1', 'eee', 'x' ],
		[ '1', 'fff', 'y' ],
		[ '1', '880', 'z' ],
	]
	=>
	[
		[ '1', [ 'x' ] ],
		[ '1^dhs0', [ 'y' ] ],
		[ '1^dhs1', [ 'z' ] ],
	]);

test('4 same versions, hashes match for 3 of them',
	[
		[ '1', 'eee', 'x' ],
		[ '1', 'eee', 'y' ],
		[ '1', 'fff', 'z' ],
		[ '1', 'eee', 'p' ],
	]
	=>
	[
		[ '1', [ 'x', 'y', 'p' ] ],
		[ '1^dhs0', [ 'z' ] ],
	]);

test('hash mismatches, other versions in the middle',
	[
		[ '1', 'eee', 'x' ],
		[ '6', '666', 'x' ],
		[ '1', 'fff', 'y' ],
		[ '7', '777', 'y' ],
		[ '1', 'fff', 'z' ],
		[ '8', '888', 'z' ],
		[ '1', 'eee', 'p' ],
		[ '9', '999', 'p' ],
	]
	=>
	[
		[ '9', [ 'p' ] ],
		[ '8', [ 'z' ] ],
		[ '7', [ 'y' ] ],
		[ '6', [ 'x' ] ],
		[ '1', [ 'x', 'p' ] ],
		[ '1^dhs0', [ 'y', 'z' ] ],
	]);

