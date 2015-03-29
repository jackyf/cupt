use TestCupt;
use Test::More tests => 7;

use strict;
use warnings;

require(get_rinclude_path(__FILE__, '../common'));

my $package = 'ppp';
my $old_version = 8;
my $new_version = 9;
my $standard_scheme = 'http';
my $standard_uri_prefix = 'debs.net/xyz';
my $uri_file_dir = 'qpr';
my $uri_file_path = "$uri_file_dir/im.deb";

sub generate_packages2 {
	my ($package_sources) = @_;

	my $generate_from_one_source = sub {
		my $source = $_;

		return {
			'content' => entail(compose_package_record($package, $new_version) . "Filename: $uri_file_path\n"),
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

sub test {
	my ($comment, $conf, $package_sources, $expected_debdelta_variants) = @_;

	my $cupt = setup_for_worker(
		'debdelta_conf' => $conf,
		'debpatch' => '',
		'dpkg_status' =>
			entail(compose_installed_record($package, $old_version)),
		'packages2' => generate_packages2($package_sources),
	);

	my $expected_uri = generate_expected_uri($expected_debdelta_variants);

	test_uris($comment, $cupt, "install $package", [ $expected_uri ]);
}

my $d1 = 'http://deltas.info/pub';
my $d2 = 'ftp://chunks.net/bar3/cafe5';


test('no debdelta sources',
		"\n", [ {} ], []);

test('debdelta source with label which does not match',
		"[pah]\ndelta_uri=$d1\nLabel=lll\n\n", [ {} ], []);
test('debdelta source with label which matches',
		"[pah]\ndelta_uri=$d1\nLabel=lll\n\n", [ {'label'=>'lll'} ], [ $d1 ]);
test('debdelta source with archive which does not match',
		"[umm]\ndelta_uri=$d1\nArchive=aaa\n\n", [ {} ], []);
test('debdelta source with archive which matches',
		"[umm]\ndelta_uri=$d1\nArchive=aaa\n\n", [ {'archive'=>'aaa'} ], [ $d1 ]);
test('debdelta source with vendor which does not match',
		"[doh]\ndelta_uri=$d1\nOrigin=vvv\n\n", [ {} ], []);
test('debdelta source with vendor which matches',
		"[doh]\ndelta_uri=$d1\nOrigin=vvv\n\n", [ {'vendor'=>'vvv'} ], [ $d1 ]);

