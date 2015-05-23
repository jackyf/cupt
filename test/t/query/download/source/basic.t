use Test::More tests => 1;

require(get_rinclude_path('common'));

subtest 'default' => sub {
	my $sp = get_default_source_package();
	my $package = $sp->{'package'};
	my $files = $sp->{'files'};
	my ($dsc_file) = grep { m/dsc$/ } (map { $_->{'name'} } @$files);

	my $cupt = prepare($sp);
	my $output = stdall("$cupt source $package");
	foreach (@$files) {
		check_file($_);
	}
	like($output, qr/\Q[fakes\/dpkg-source]\E -x $dsc_file$/, 'dpkg-source call');
}

