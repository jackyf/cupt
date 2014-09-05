use TestCupt;
use Test::More tests => 7;

use strict;
use warnings;

my $nlist = 'ne, ny, nobs';

my $cupt = TestCupt::setup(
	'dpkg_status' =>
		entail(compose_installed_record('e', '1')) .
		entail(compose_installed_record('y', '1')) .
		entail(compose_installed_record('obs', '1')),
	'packages' =>
		entail(compose_package_record('e', '1')) .
		entail(compose_package_record('rz', '30') . "Recommends: z\n") .
		entail(compose_package_record('z', '3')) ,
	'packages2' =>
		[
			[
				'archive' => 'x1y2',
				'content' =>
					entail(compose_package_record('y', '1')) .
					entail(compose_package_record('f', '0.9') . "Depends: $nlist\n") .
					entail(compose_package_record('f', '1.0') . "Recommends: $nlist\n") .
					entail(compose_package_record('f', '1.1') . "Suggests: $nlist\n") .
					entail(compose_package_record('ne', '2') . "Breaks: e\n") .
					entail(compose_package_record('ny', '2') . "Breaks: y\n") .
					entail(compose_package_record('nobs', '2') . "Breaks: obs\n"),
			],
		],
	'extended_states' =>
		entail(compose_autoinstalled_record('e')) .
		entail(compose_autoinstalled_record('y')) .
		entail(compose_autoinstalled_record('obs')),
				
);

my $cupt_options = <<'END';
-V
-o apt::default-release=x1y2
-o apt::install-suggests=yes
-o cupt::resolver::keep-suggests=yes
--no-auto-remove
-o debug::resolver=yes
END
$cupt_options =~ s/\n/ /g;

sub test_f {
	my ($f_version, $expected_e_version, $expected_y_version) = @_;

	my $command = "install f=$f_version";
	subtest $command => sub {
		my $output = get_first_offer("$cupt $cupt_options $command");
		is(get_offered_version($output, 'e'), $expected_e_version, 'e') or diag($output);
		is(get_offered_version($output, 'y'), $expected_y_version, 'y') or diag($output);
		is(get_offered_version($output, 'obs'), get_empty_version(), 'obs') or diag($output);
	}
} 

sub test_z {
	my ($command) = @_;

	my $output = get_first_offer("$cupt $cupt_options $command");
	is(get_offered_version($output, 'z'), 3, "z: $command") or diag($output);
}

test_f('0.9', get_empty_version(), get_empty_version());
test_f('1.0', get_empty_version(), get_unchanged_version());
test_f('1.1', get_unchanged_version(), get_unchanged_version());

test_z('--try install z');
test_z('--wish install z');
test_z('--try install rz');
test_z('--wish install rz');

