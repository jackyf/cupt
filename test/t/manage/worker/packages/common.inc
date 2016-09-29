sub setup_for_worker {
	my (%p) = @_;
	my $dpkg_version = '1.16.15'; # Debian wheezy
	my $dpkg_record = entail(compose_installed_record('dpkg', $dpkg_version));
	$p{'dpkg_status'} .= $dpkg_record;
	return TestCupt::setup(%p);
}

sub get_worker_command {
	my ($cupt, $command, %params) = @_;

	my $options = '-y -o cupt::console::warnings::removal-of-essential=no';
	if ($params{'simulate'}//1) {
		$options .= ' -s';
	}

	return "$cupt $options $command";
}

sub parse_dpkg_commands {
	my $output = shift;

	my $skip_triggers = 1;
	my $skip_bad_path = 1;
	my $package_simplifier = sub {
		return (s!.*/((?:\w|-)+)_(\w+)_\w+\.deb$!<$1 $2>!r);
	};

	my $dpkg = get_dpkg_path();
	my @lines = ($output =~ m/^S: running command '$dpkg (.*)'$/mg);

	my $parse_line = sub {
		my @tokens = split(' ');

		my $subcommand = shift @tokens;
		if ($skip_triggers && ($subcommand =~ m/trigger/)) {
			return ();
		}

		my @options;
		my @packages;

		foreach (@tokens) {
			if ($skip_triggers && (m/trigger/)) {
				next;
			}
			if ($skip_bad_path && (m/--force-bad-path/)) {
				next;
			}

			my $target = (m/^--/ ? \@options : \@packages);
			push @$target, $_;
		}

		@packages = map(&$package_simplifier, @packages);

		return [$subcommand, \@options, \@packages];
	};

	return map(&$parse_line, @lines);
}

1;

