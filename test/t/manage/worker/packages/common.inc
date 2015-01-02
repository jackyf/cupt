sub setup_for_worker {
	my (%p) = @_;
	my $dpkg_record = entail(compose_installed_record('dpkg', 0));
	$p{'dpkg_status'} .= $dpkg_record;
	return TestCupt::setup(%p);
}

