use Test::More tests => 4;
use Storable qw(dclone);

require(get_rinclude_path('../common'));

sub get_corrupter {
	my $kind_regex = shift;
	return sub {
		my (undef, $kind, undef, $content) = @_;
		return $content unless ($kind =~ $kind_regex);
		return '30urjak;sdas';
	}
}

sub prepare {
	my $release = shift;
	$release->{location} = 'remote';
	return setup('releases' => [ dclone($release) ]);
}

sub check {
	my ($cupt, $release, $corrupter, $expected_result, $desc) = @_;
	$release = dclone($release);

	$release->{hooks}->{diff}->{write} = $corrupter;
	update_remote_releases($release);

	my $output = stdall("$cupt update -o cupt::languages::indexes=de,nl");
	my $checker = $expected_result ? \&unlike : \&like;
	$checker->($output, qr/W: failed to download/, $desc) or diag($output);
}

sub test {
	my ($release, $kind_regex, $desc) = @_;
	my $corrupter = get_corrupter($kind_regex);

	subtest $desc => sub {
		my $cupt = prepare($release);
		check($cupt, $release, $corrupter, 0, 'corrupted remote file, no local file');
		check($cupt, $release, undef, 1, 'fine remote file, no local file');
		check($cupt, $release, $corrupter, 1, 'corrupted remote file but local file present');
	}
}

test({ 'packages' => [] }, qr/Packages/, 'packages');
test({ 'sources' => [] }, qr/Sources/, 'sources');
test({ 'packages' => [], 'translations' => { 'de' => [] } },
		qr/Translation/, 'translations');
test({ 'packages' => [], 'translations' => { 'de' => [], 'nl' => [] } },
		qr/Translation-de/, 'one of translations');

