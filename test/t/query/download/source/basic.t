use Test::More tests => 2;

require(get_rinclude_path('common'));

my $sp = get_default_source_package();
my $package = $sp->{'package'};
my $files = $sp->{'files'};
my ($dsc_file) = grep { m/dsc$/ } (map { $_->{'name'} } @$files);

sub test {
	my ($option_line, $dpkg_source_call_expected) = @_;

	subtest "options: '$option_line'" => sub {
		my $cupt = prepare($sp);
		my $output = stdall("$cupt source $option_line $package");
		foreach (@$files) {
			check_file($_);
		}
		if ($dpkg_source_call_expected) {
			like($output, qr/\Q[fakes\/dpkg-source]\E -x $dsc_file$/, 'dpkg-source call');
		} else {
			unlike($output, qr/dpkg-source/, 'no dpkg-source call');
		}
	}
}

test("", 1);
test("--download-only", 0);

