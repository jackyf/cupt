use Test::More tests => 3;
use IPC::Run3;

my $sane_version_regex = qr/2\.\d+\.\d+(~|\w|\+)*/;
my $cupt = setup();

foreach my $command (qw(version -v --version)) {
	subtest "invoking via $command" => sub {
		run3("$cupt $command", \undef, \my $stdout, \my $stderr);
		is($?, 0, 'exit code');
		like($stdout, qr/^executable: $sane_version_regex$/m, 'executable version');
		like($stdout, qr/^library: $sane_version_regex$/m, 'library version');
		is($stderr, '', 'no warnings/errors');
	}
}

