use Test::More tests => 4;
use IPC::Run3;

my $cupt = setup();

foreach my $command ('help', '-h', '--help', '') {
	subtest "invoking via $command" => sub {
		run3("$cupt $command", \undef, \my $stdout, \my $stderr);
		is($?, 0, 'exit code');
		like($stdout, qr/^Usage/, "prints usage");
		is($stderr, '', "no errors/warnings");
	}
}

