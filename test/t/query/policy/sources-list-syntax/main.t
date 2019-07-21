use Test::More tests => 6 + 2 + 3 + 3 + 4;

require(get_rinclude_path('common'));

sub test_valid {
	my ($desc, $hook, $output_regex, $error_regex) = @_;
	run_case_raw($desc, $hook => 1, $output_regex, $error_regex);
}
sub test_invalid {
	my ($desc, $hook, $error_regex) = @_;
	run_case_raw($desc, $hook => 0, undef, $error_regex);
}
sub test_nooutput {
	my ($desc, $hook) = @_;
	test_valid($desc, $hook => undef, qr/^$/s);
}
sub test_valid_same {
	my ($desc, $hook) = @_;
	test_valid($desc, $hook => qr//, undef);
}
sub test_valid_different {
	my ($desc, $hook) = @_;
	test_valid($desc, $hook => undef, qr/^W: no release file.*/);
}

sub append {
	my $appendage = shift;
	return sub { return $_[0] . $appendage; };
}
sub set_line {
	my $line = shift;
	return sub { return $line; }
}
sub set_source {
	my $source_type = shift;
	return sub { return $_[0] =~ s/.*? /$source_type /r; };
}
sub set_scheme {
	my $scheme = shift;
	return sub { return $_[0] =~ s% (.*?)://% $scheme://%r; };
}
sub set_dist_and_comps {
	my $dist = shift;
	my $dc = "file:///unk $dist";
	return sub { return $_[0] =~ s/ .*/ $dc/r; };
}

test_nooutput('empty line', set_line(''));
test_nooutput('whitespaces', set_line('   '));
test_nooutput('comment', set_line('# qwe'));
test_nooutput('whitespaces plus comment', set_line('   #Y!'));
test_valid_same('whitespace after a full line', append("  \t "));
test_valid_same('comment after a full line', append('# cheeeeese'));

test_invalid('only one token', set_line('deb-src') => qr/undefined source uri/);
test_invalid('only two tokens', set_line('deb-src file:///abc') => qr/undefined source distribution/);

test_valid_same('same source type', set_source('deb-src'));
test_nooutput('different source type', set_source('deb'));
test_invalid('invalid source type', set_source('bqpawxz') => qr/incorrect source type/);

test_valid_different('known scheme', set_scheme('ftp') => undef);
test_valid_different('unknown scheme', set_scheme('ummm') => undef);
test_valid_different('garbage scheme', set_scheme('%^&(:/://12B%') => undef);

test_valid_different('valid easy entry', set_dist_and_comps('l5mn/'));
test_invalid('invalid easy entry', set_dist_and_comps('l5mn') => qr/distribution doesn't end with a slash/);
test_valid_different('one component', set_dist_and_comps('abc c1'));
test_valid_different('four components', set_dist_and_comps('abc c1 c2 pyyyyykki c4'));

