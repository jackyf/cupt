use strict;
use warnings FATAL => 'all';

require(get_rinclude_path('../FSE'));

sub compose_error_messages {
	return join("", map { "E: $_\n" } @_);
}

sub upq {
	my $pattern = shift;
	return "unable to parse the query '$pattern'";
}

sub test_binary {
	my ($cupt, $pattern, $expected_messages) = @_;
	my $expected_errors = compose_error_messages(
		@$expected_messages,
		upq($pattern),
		"error performing the command 'search'"
	);

	(my $out, undef) = get_binary_search_result($cupt, $pattern);
	is($out, $expected_errors, $pattern);
}

sub test_source {
	my ($cupt, $pattern, $expected_messages) = @_;
	my $expected_errors = compose_error_messages(
		@$expected_messages,
		upq($pattern),
		"error performing the command 'showsrc'"
	);

	(my $out, undef) = get_source_search_result($cupt, $pattern);
	is($out, $expected_errors, $pattern);
}

1;

