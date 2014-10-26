sub setup_cupt {
	my ($parameters) = @_;

	my %p = %{$parameters};

	return TestCupt::setup(
		'packages2' =>
			[
				[
					'trusted' => 0,
					'content' => entail(compose_package_record($p{'package'}, $p{'version'}) . $p{'package_content'}),
				],
			],
		'preferences' => compose_pin_record($p{'first_pin_line'}, $p{'pin_expression'}, 927),
	);
}

my %match_mapper = (
	-1 => [ '', "isn't considered: broken" ],
	0  => [ 500, "doesn't match" ],
	1  => [ 927, 'matches' ],
);

sub test_pinning {
	my ($parameters, $match_expected) = @_;

	my $cupt = setup_cupt($parameters);

	my $package = $parameters->{'package'};
	my $version = $parameters->{'version'};
	my $package_comment = $parameters->{'package_comment'};
	my $first_pin_line = $parameters->{'first_pin_line'};
	my $pin_expression = $parameters->{'pin_expression'};

	my $output = stdall("$cupt policy $package");

	my $expected_priority = $match_mapper{$match_expected}->[0];

	my $match_comment = $match_mapper{$match_expected}->[1];
	my $comment = "'$package_comment' $match_comment '$first_pin_line & Pin: $pin_expression'";

	is(get_version_priority($output, $version), $expected_priority, $comment)
			or diag($output);
}


