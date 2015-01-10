sub setup_for_worker {
	my (%p) = @_;
	my $dpkg_version = '1.16.15'; # Debian wheezy
	my $dpkg_record = entail(compose_installed_record('dpkg', $dpkg_version));
	$p{'dpkg_status'} .= $dpkg_record;
	return TestCupt::setup(%p);
}

sub get_worker_command {
	my ($cupt, $command, %params) = @_;

	my $options = '-y';
	if ($params{'simulate'}//1) {
		$options .= ' -s';
	}

	return "$cupt $options $command";
}

