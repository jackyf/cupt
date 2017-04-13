my $warning_regex = qr/^W:/m;

sub test_option {
	my ($output, $option, $value) = @_;

	my $value_comment = ($value // '<not set>');

	subtest "$option => $value_comment" => sub {
		if (defined $value) {
			like($output, qr/^\Q$option "$value"\E/m, 'option set successfully');
			unlike($output, $warning_regex, 'no warnings');
		} else {
			unlike($output, qr/^\Q$option\E/m, 'option not present');
			like($output, $warning_regex, 'warning issued');
		}
	};
}

