use Test::More tests => 5 + 5 + 4;

require(get_rinclude_path('common'));

sub run_case {
	my ($opts, $success, $recognised, $error_regex) = @_;

	my $hook = sub {
		my $line = shift;
		$line =~ s/\[.*\]//;
		$line =~ s/ / $opts/;
		return $line;
	};

	my $desc = "$opts => ($success, $recognised)";
	my $output_regex = $recognised ? qr// : undef;
	run_case_raw($desc, $hook => $success, $output_regex, $error_regex); 
};

sub test_good {
	run_case(@_, 1, 1, undef);
}

sub test_unrecognised {
	run_case(@_, 1, 0, qr/^W: no release file present/m);
}

sub test_bad {
	my ($line, $message) = @_;
	my $regex = qr/^E: \Q$message\E\n.*\nE: unable to parse the sources list\n/s;
	run_case($line, 0, 0, $regex);
}

test_good('');
test_good('[ abc=xcv ]');
test_good('[ abc=def uio=a8 ]');
test_good('[ abc=aaa,bbb,7 ]');
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

