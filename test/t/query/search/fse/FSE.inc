sub eis {
	my ($pattern, @expected) = @_;

	my $out = `$cupt search --fse '$pattern' 2>&1`;
	my @sout = split(/\n/, $out);
	s/ -.*// for @sout;
	is_deeply(\@sout, \@expected, "search of '$pattern' returns '@expected'") or
			diag($out);
}

sub pn {
	return 'package:name(' . $_[0] . ')';
}

