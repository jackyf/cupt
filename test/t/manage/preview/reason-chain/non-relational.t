use Test::More tests => 5;

require(get_rinclude_path('common'));

my $cupt = setup(
	'dpkg_status' => [ compose_installed_record('unneeded', '3.4-5') ],
	'extended_states' => [ compose_autoinstalled_record('unneeded') ],
);

is(get_reason_chain($cupt, '', 'unneeded'), 'unneeded: auto-removal');


$cupt = setup(
	'packages' => [ compose_package_record('newpkg', '2.15') . "Provides: bar\n" ],
);

is(get_reason_chain($cupt, 'newpkg', 'newpkg'), "newpkg: user request: install newpkg | for package 'newpkg'");
is(get_reason_chain($cupt, '--satisfy bar', 'newpkg'), "newpkg: user request: satisfy 'bar'");

$cupt = setup(
	'dpkg_status' => [
		compose_installed_record('libfoo1', '1') . "Source: foo\n" ,
		compose_installed_record('libfoo-bin', '1') . "Source: foo\n" ,
	],
	'packages' => [
		compose_package_record('libfoo1', '4') . "Source: foo\n" ,
		compose_package_record('libfoo-bin', '4') . "Source: foo\n" ,
	],
	'sources' => [
		compose_package_record('foo', 4) . "Binary: libfoo1, libfoo-bin\n" ,
	],
);

my $upgrade_libfoo_request = 'libfoo1 -o cupt::resolver::synchronize-by-source-versions=hard';
my $libfoo1_reason = "libfoo1: user request: install libfoo1 | for package 'libfoo1'";
is(get_reason_chain($cupt, $upgrade_libfoo_request, 'libfoo1'), $libfoo1_reason);
is(get_reason_chain($cupt, $upgrade_libfoo_request, 'libfoo-bin'), "libfoo-bin: libfoo-bin: synchronization with libfoo1 4\n  $libfoo1_reason");

