my %dep_letters = (
	'D' => 'Depends',
	'R' => 'Recommends',
	'S' => 'Suggests',
);

sub setup_cupt_from_links {
	my $links = shift;

	my %sorted_deps;
	my %autoflags;

	foreach my $link (@$links) {
		my ($from, $type, $tos) = split(' ', $link);

		my @tos = split('\|', $tos);
		foreach my $to (@tos) {
			if (length($to) > 2) {
				$autoflags{$to} = 1;
			}

			$sorted_deps{$to} //= {};
		}

		push @{$sorted_deps{$from}->{$type}}, $tos;
	}

	my $installed_packages = '';
	while (my ($package, $deplines) = each %sorted_deps) {
		$installed_packages .= compose_installed_record($package, 0);

		while (my ($dep_letter, $dependees) = each %$deplines) {
			$installed_packages .= $dep_letters{$dep_letter};
			$installed_packages .= ': ';
			$installed_packages .= join(', ', @$dependees);
			$installed_packages .= "\n";
		}

		$installed_packages .= "\n";
	}

	my $extended_states = join('', map { entail(compose_autoinstalled_record($_)) } keys %autoflags);

	$cupt = TestCupt::setup('dpkg_status' => $installed_packages, 'extended_states' => $extended_states);
}

