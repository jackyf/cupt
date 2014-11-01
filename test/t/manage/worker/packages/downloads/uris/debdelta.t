use TestCupt;
use Test::More tests => 2;

use strict;
use warnings;

eval get_inc_code('common');

my $debdelta_conf = <<END;
[our archive]
delta_uri=http://deltas.info/pub

END

my %parameters = (
	'debdelta_conf' => $debdelta_conf,
	'packages2' => [
		{
			'content' =>
				entail(compose_package_record('bbb', 3) . "Filename: pool/b/bbb5.deb\n") .
				entail(compose_package_record('ccc', 4) . "Filename: pool/c/ccc4.deb\n"),
			'scheme' => 'https',
			'hostname' => 'debs.net',
		},
	],
	'expected_ccc' => 'https://debs.net/pool/c/ccc4.deb',
);

sub test {
	my ($debpatch_present) = @_;

	$parameters{'debpatch'} = ($debpatch_present ? '' : undef);

	my $debdelta_addendum = ' | debdelta:http://deltas.info/pub/pool/b/bbb_2_3_all.debdelta';
	$parameters{'expected_bbb'} = 'https://debs.net/pool/b/bbb5.deb' . ($debpatch_present ? $debdelta_addendum : '');

	$parameters{'comment'} = "debpatch is present: $debpatch_present";

	test_uris(%parameters);
}

test(0);
test(1);

