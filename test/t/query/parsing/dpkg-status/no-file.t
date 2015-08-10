use Test::More tests => 1;
use IPC::Run3;

my $cupt = setup();

subtest "the" => sub {
	run3("$cupt policy xyz -o dir::state::status=/doesntexist", \undef, \my $stdout, \my $stderr);
	isnt($?, 0, 'exit code');
	is($stdout, '', 'no result');
	like($stderr, qr/^E: unable to open the dpkg status file '\/doesntexist': .+$/m, "error message");
}

