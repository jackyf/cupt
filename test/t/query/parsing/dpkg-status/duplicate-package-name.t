use Test::More tests => 3;
use IPC::Run3;

sub setup_from {
	my @input = @_;
	my @records = map { entail(compose_installed_record(@$_)) } @input;
	return setup('dpkg_status' => join("", @records));
}

sub test {
	my ($name, @installed_data) = @_;

	my $cupt = setup_from(@installed_data);
	my $package = $installed_data[0]->[0];

	my $output;
	subtest $name => sub {
		run3("$cupt policy $package", \undef, \$output, \$output);
		is($?, 0, "exit code");
		like($output, qr/^E: error while merging the version '.*?' for the package '$package'$/m, 'error with package name');
		like($output, qr/^E: more than one installed version per package is not supported$/m, 'error explanation');
	} or diag($output);
}

test("same version", ['abc', 1], ['abc', 1]);
test("different versions", ['def', 3], ['def', 4]);
test("non-contiguous", ['mnl', 5], ['xyz', 0], ['mnl', 5]);

