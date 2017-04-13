use Test::More tests => 25;

require(get_rinclude_path('../common'));

my $package = 'ppp';
my $old_version = 8;
my $new_version = 9;
my $standard_scheme = 'http';
my $standard_uri_prefix = 'debs.net/xyz';
my $uri_file_dir = 'qpr';
my $uri_file_path = "$uri_file_dir/im.deb";

sub generate_releases {
	my ($package_sources) = @_;

	my $generate_from_one_source = sub {
		my $source = $_;
		return {
			'packages' => [ compose_package_record($package, $new_version) . "Filename: $uri_file_path\n" ],
			'scheme' => $standard_scheme,
			'hostname' => $standard_uri_prefix,
			'archive' => ($source->{'archive'} // 'somearchive'),
			'label' => ($source->{'label'} // 'somelabel'),
			'vendor' => ($source->{'vendor'} // 'somevendor'),
		};
	};

	return [ map(&$generate_from_one_source, @$package_sources) ];
}

sub generate_expected_uri {
	my $expected_debdelta_variants = shift;

	my $result = "$standard_scheme://$standard_uri_prefix/$uri_file_path";

	for my $variant (@$expected_debdelta_variants) {
		$result .= " | debdelta:$variant/$uri_file_dir/${package}_${old_version}_${new_version}_all.debdelta";
	}

	return $result;
}

my $conf;

sub test {
	my ($comment, $package_sources, $expected_debdelta_variants) = @_;

	my $cupt = setup(
		'debdelta_conf' => $conf,
		'debpatch' => '',
		'dpkg_status' =>
			entail(compose_installed_record($package, $old_version)),
		'releases' => generate_releases($package_sources),
	);

	my $expected_uri = generate_expected_uri($expected_debdelta_variants);

	test_uris($comment, $cupt, "install $package", [ $expected_uri ]);
}

my $d1 = 'http://deltas.info/pub';
my $d2 = 'ftp://chunks.net/bar3/cafe5';
my $d3 = 'https://t.org/qwerty';


$conf = "\n";
test('no debdelta sources',
		[ {} ], []);

$conf = "[pah]\ndelta_uri=$d1\nLabel=lll\n\n";
test('debdelta source with label which does not match',
		[ {} ], []);
test('debdelta source with label which matches',
		[ {'label'=>'lll'} ], [ $d1 ]);

$conf = "[umm]\ndelta_uri=$d1\nArchive=aaa\n\n";
test('debdelta source with archive which does not match',
		[ {} ], []);
test('debdelta source with archive which matches',
		[ {'archive'=>'aaa'} ], [ $d1 ]);

$conf = "[doh]\ndelta_uri=$d1\nOrigin=vvv\n\n";
test('debdelta source with vendor which does not match',
		[ {} ], []);
test('debdelta source with vendor which matches',
		[ {'vendor'=>'vvv'} ], [ $d1 ]);

$conf = "[huh]\ndelta_uri=$d1\nArchive=aaa\nOrigin=vvv\n\n";
test('debdelta source with archive and vendor, nothing matches',
		[ {'archive'=>'boo', 'vendor'=>'boo'} ], []);
test('debdelta source with archive and vendor, only archive matches',
		[ {'archive'=>'aaa', 'vendor'=>'boo'} ], []);
test('debdelta source with archive and vendor, only vendor matches',
		[ {'archive'=>'boo', 'vendor'=>'vvv'} ], []);
test('debdelta source with archive and vendor, both match',
		[ {'archive'=>'aaa', 'vendor'=>'vvv'} ], [ $d1 ]);

test('multiple version sources, nothing matches',
		[ {'archive'=>'boo'}, {'vendor'=>'boo'} ], []);
test('multiple version sources, first matches',
		[ {'archive'=>'aaa', 'vendor'=>'vvv'}, {'label'=>'lll'} ], [ $d1 ]);
test('multiple version sources, second matches',
		[ {'archive'=>'arh'}, {'archive'=>'aaa', 'vendor'=>'vvv'} ], [ $d1 ]);
test('multiple version sources, matches across several sources',
		[ {'archive'=>'aaa'}, {'vendor'=>'vvv'} ], [ $d1 ]);

$conf = <<END;
[s1]
delta_uri=$d1
Archive=aaa

[s2]
delta_uri=$d2
Label=lll

[s3]
delta_uri=$d3
Origin=vvv
Archive=exp

END
test('debdelta multi-source, no matches 1',
		[ {} ], []);
test('debdelta multi-source, no matches 2',
		[ {'archive'=>'boo', 'label'=>'boo'} ], []);
test('debdelta multi-source, s1 matches',
		[ {'archive'=>'aaa'} ], [ $d1 ]);
test('debdelta multi-source, s2 matches',
		[ {'label'=>'lll'} ], [ $d2 ]);
test('debdelta multi-source, s3 matches',
		[ {'archive'=>'exp', 'vendor'=>'vvv'} ], [ $d3 ]);
test('debdelta multi-source, s1 and s2 match',
		[ {'archive'=>'aaa', 'label'=>'lll'} ], [ $d1, $d2 ]);
test('debdelta multi-source, s2 and s3 match',
		[ {'archive'=>'exp', 'vendor'=>'vvv', 'label'=>'lll'} ], [ $d2, $d3 ]);
test('debdelta multi-source, s1 and s2 and s3 match',
		[ {'archive'=>'aaa'}, {'archive'=>'exp', 'label'=>'lll'}, {'vendor'=>'vvv'} ], [ $d1, $d2, $d3 ]);


$conf = <<END;
[s1]
Archive=aaa

[s2]
Archive=aaa
delta_uri=$d2

END
test('debdelta source blocking issues, delta_uri field missing',
		[ {'archive'=>'aaa'} ], [ $d2 ]);

$conf = <<END;
[s1]
delta_uri=$d2
SomeField=someValue

[s2]
delta_uri=$d3

END
test('debdelta source blocking issues, unknown field prevents usage',
		[ {} ], [ $d3 ]);

