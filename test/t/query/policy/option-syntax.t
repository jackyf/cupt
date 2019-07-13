use Test::More tests => 5 + 5 + 4;

use IPC::Run3;

sub run_case {
	my ($line, $success, $stderr_regex) = @_;

	my $hook = sub { return $line };
	my $cupt = setup('releases' => [{'packages' => [], 'options-hook' => $hook}]);
	run3("$cupt policy", \undef, \undef, \my $stderr);

	$stderr =~ s/^.*signature.*$//mg; # ignore a warning no longer suppressed by the default option

	subtest "$line => $stderr_regex" => sub {
		($success ? \&is : \&isnt)->($?, '0', 'exit code');
		like($stderr, $stderr_regex, 'errors/warnings');
	};
};

sub test_good {
	run_case(@_, 1, qr/^$/);
}

sub test_unrecognised {
	run_case(@_, 1, qr/^W: no release file present/m);
}

sub test_bad {
	my ($line, $message) = @_;
	my $regex = qr/^E: \Q$message\E\n.*\nE: unable to parse the sources list\n/s;
	run_case($line, 0, $regex);
}

test_good('');
test_good('[ abc=xcv ]');
test_good('[ abc=def uio=a8 ]');
TODO: {
	local $TODO = 'broken';
	test_good('[ abc=aaa,bbb,7 ]');
}
test_good('[ YuP=nMp ]');

test_unrecognised('[]');
test_unrecognised('[abc=xcv]');
test_unrecognised('{ x=0 }');
test_unrecognised('{ x=0 ]');
test_unrecognised(']');

my $nct = "no closing token (']') for options";
test_bad('[', $nct);
test_bad('[ x=0 }', $nct);
test_bad('[ xxx=w', $nct);
test_bad('[ abc ]', "no key-value separator ('=') in the option token 'abc'");

