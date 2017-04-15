use Test::More tests => 8;

require(get_rinclude_path('pinning'));

sub test {
	my ($pin_records, $expected_result) = @_;

	test_pinning_multiple_records(
		{
			'package' => 'rrr',
			'version' => '1.2-3',
			'package_comment' => '',
			'release_properties' => {
				'label' => 'Latest and greatest',
				'vendor' => 'Earth',
			},
		},
		$pin_records,
		$expected_result
	);
}

my $any_record = [ 'Package: *', 'version *' ];
my $no_record = [ 'Package: uuu', 'version *' ];
my $broken_record = [ 'T&(', '%%%*' ];

test([ $any_record, $any_record ] => 1);
test([ $no_record, $any_record ] => 2);
test([ $no_record, $no_record, $any_record ] => 3);
test([ $any_record, $broken_record, $any_record ] => -1);
test([ $no_record, $no_record ] => 0);
test([ $any_record, $no_record ] => 1);

test(
	[
		[ 'Package: rrr', 'version 1.1-3' ],
		[ 'Package: ppp', 'release o=Earth' ],
		[ 'Package: rrr', 'release o=Earth' ],
	]
	=> 3
);

test(
	[
		$no_record,
		[ 'Package: rrr', 'release l=*great*' ],
		$any_record
	]
	=> 2
);

