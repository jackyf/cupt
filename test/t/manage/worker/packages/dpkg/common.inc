eval get_inc_code('../common');
die($@) if $@;

my $skip_triggers = 1;
sub set_parse_skip_triggers {
	$skip_triggers = shift;
}

sub parse_dpkg_commands {
	my $output = shift;

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

sub test_dpkg_sequence {
	my ($user_command, @expected) = @_;

	my $comment = $user_command;
	$user_command =~ s/#.*//;

	my $output = stdall(get_worker_command($cupt, $user_command));

	my @parsed_output = parse_dpkg_commands($output);
	is_deeply(\@parsed_output, \@expected, $comment) or
			diag($output);
}

