sub get_worker_command {
	my ($cupt, $command, %params) = @_;

	my $options = '-y -o cupt::console::warnings::removal-of-essential=no';
	if ($params{'simulate'}//1) {
		$options .= ' -s';
	}

	return "$cupt $options $command";
}

1;

