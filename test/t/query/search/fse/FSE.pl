use IPC::Run3;

sub eis {
	my ($cupt, $pattern, @expected) = @_;

	@expected = sort @expected;

	my $out = '';
	run3("$cupt search --fse '$pattern'", \undef, \$out, \$out);
	my @sout = split(/\n/, $out);
	s/ -.*// for @sout;
	is_deeply(\@sout, \@expected, "search of '$pattern' returns '@expected'") or
			diag($out);
}

sub pn {
	return 'package:name(' . $_[0] . ')';
}

