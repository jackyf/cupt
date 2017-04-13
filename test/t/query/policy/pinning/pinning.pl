my %match_mapper = (
	-1 => [ '', "isn't considered: broken" ],
	0  => [ 500, "doesn't match" ],
	1  => [ 810, 'matches first' ],
	2  => [ 820, 'matches second' ],
	3  => [ 830, 'matches third' ],
);

sub compose_preferences {
	my ($pin_records) = @_;

	my $result;

	foreach my $index (0 .. scalar @$pin_records - 1) {
		my $input = $pin_records->[$index];
		my $target_priority = ($match_mapper{$index+1}[0] // -2);

		$result .= compose_pin_record($input->[0], $input->[1], $target_priority);
	}

	return $result;
}

sub setup_cupt {
	my ($parameters, $pin_records) = @_;

	my %p = %{$parameters};

	return setup(
		'releases' => [
			{
				'trusted' => 0,
				'packages' => [ compose_package_record($p{'package'}, $p{'version'}) . ($p{'package_content'}//'') ],
				%{$p{'release_properties'} // {}},
			},
		],
		'preferences' => compose_preferences($pin_records),
	);
}

sub stringify_pin_records {
	my ($pin_records) = @_;

	return join(" ; ", map { $_->[0] . ' & Pin: ' . $_->[1] } @$pin_records);
}

sub test_pinning_multiple_records {
	my ($parameters, $pin_records, $match_expected) = @_;

	my $cupt = setup_cupt($parameters, $pin_records);

	my $package = $parameters->{'package'};
	my $version = $parameters->{'version'};
	my $package_comment = $parameters->{'package_comment'};
	my $first_pin_line = $parameters->{'first_pin_line'};
	my $pin_expression = $parameters->{'pin_expression'};

	my $output = stdall("$cupt policy $package");

	my $expected_priority = $match_mapper{$match_expected}->[0];

	my $match_comment = $match_mapper{$match_expected}->[1];
	my $pin_records_comment = stringify_pin_records($pin_records);
	my $comment = "'$package_comment' $match_comment '$pin_records_comment'";

	is(get_version_priority($output, $version), $expected_priority, $comment)
			or diag($output);
}

sub test_pinning {
	my ($parameters, $match_expected) = @_;
	test_pinning_multiple_records(
			$parameters,
			[ [ $parameters->{'first_pin_line'}, $parameters->{'pin_expression'} ] ],
			$match_expected);
}

