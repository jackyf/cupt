use Test::More tests => 4;
use List::Util qw(first);

require(get_rinclude_path('common'));

my $sp = get_default_source_package();
my $package = $sp->{'package'};
my $files = $sp->{'files'};

sub test {
	my ($option_line, $expected_file_names_sub) = @_;

	my $cupt = prepare($sp);
	my $ec = exitcode("$cupt source --download-only $option_line $package");

	my (@expected_file_names) = grep(&$expected_file_names_sub, (map { $_->{'name'} } @$files));
	my @got_file_names = glob("$package*");

	subtest "options: '$option_line'" => sub {
		is($ec, 0, "operation was successful");
		is_deeply(\@got_file_names, \@expected_file_names, "downloaded right selection of files");
		foreach my $name (@got_file_names) {
			my $record = first { $_->{'name'} eq $name } @$files;
			check_file($record);
		}
	}
}

test("" => sub { 1 });
test("--dsc-only" => sub { m/\Q.dsc\E$/ });
test("--diff-only", => sub { m/\Q.diff.gz\E$/ });
test("--tar-only", => sub { m/\Q.tar.\E.*$/ });

