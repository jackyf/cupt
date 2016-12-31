use Test::More tests => 4+7;

require(get_rinclude_path('common'));

my $cupt = setup();

sub test {
	my $full_command = shift;
	my ($command) = ($full_command =~ m/(.*?)(?: |$)/);
	my $cupt_shell = get_shell($cupt);

	# several runs to check that bad commands do not cause early exit or behavioral changes
	subtest $full_command => sub {
		for my $run (1..3) {
			my $answer = $cupt_shell->execute($full_command);
			chomp($answer);
			is($answer, "E: unrecognized command '$command'", "run $run");
		}
	}
}

test('installll');
test('dpkg');
test('dpkg -l');
test('ls --lat');

test('%abc def');
test('!abc def');
TODO: {
	local $TODO = 'fix escaping shell special characters';
	test('#br');
	test('dpkg -l | grep geo');
	test('$abc def');
	test('-abc def');
	test('(ls)');
}

