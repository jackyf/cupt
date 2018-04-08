use IPC::Run3;

use strict;
use warnings FATAL => 'all';

sub get_binary_search_result {
	my ($cupt, $pattern) = @_;

	my $out = '';
	run3("$cupt search --fse '$pattern'", \undef, \$out, \$out);
	my @sout = split(/\n/, $out);
	s/ -.*// for @sout;

	return ($out, @sout);
}

sub get_source_search_result {
	my ($cupt, $pattern) = @_;

	my $out = '';
	run3("$cupt showsrc '$pattern'", \undef, \$out, \$out);
	my @sout = ($out =~ m/^Package: (.*)$/mg);

	return ($out, @sout);
}

sub test_fse_pattern {
	my ($cupt, $is_binary, $pattern, @expected) = @_;

	@expected = sort @expected;

	my $out;
	my @sout;
	if ($is_binary) {
		($out, @sout) = get_binary_search_result($cupt, $pattern);
	} else {
		($out, @sout) = get_source_search_result($cupt, $pattern);
	}

	is_deeply(\@sout, \@expected, "search of '$pattern' returns '@expected'") or
			diag($out);
}

sub eis {
	my $cupt = shift;
	test_fse_pattern($cupt, 1, @_);
}

sub eis_source {
	my $cupt = shift;
	test_fse_pattern($cupt, 0, @_);
}

sub pn {
	return 'package:name(' . $_[0] . ')';
}

1;

