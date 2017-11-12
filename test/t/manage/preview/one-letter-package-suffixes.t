use Test::More tests => 7 + 5 + 5 + 5 + 5;

my $cupt = setup(
	'dpkg_status' => [
		compose_installed_record('aa', 0) ,
		compose_installed_record('mm++', 1) ,
		compose_installed_record('nn--', 2) ,
	],
	'packages' => [
		compose_package_record('bb', 3) ,
		compose_package_record('oo++', 4) ,
		compose_package_record('pp--', 5) ,
	]
);

my $nv = get_empty_version();

sub test {
	my ($args, $expected_changes) = @_;
	my $offer = get_first_offer("$cupt $args");
	is_deeply(get_offered_versions($offer), $expected_changes, $args);
}

test('install oo++' => {'oo++' => 4});
test('install pp--' => {'pp--' => 5});
test('remove mm++' => {'mm++' => $nv});
test('remove nn--' => {'nn--' => $nv});
test('install bb --remove nn--' => {'bb' => 3, 'nn--' => $nv});
test('install pp-- --remove mm++' => {'pp--' => 5, 'mm++' => $nv});
test('remove pp-- --install mm++' => {});

test('install bb aa-' => {'aa' => $nv, 'bb' => 3});
test('install pp-- aa- bb' => {'aa' => $nv, 'bb' => 3, 'pp--' => 5});
test('install aa-' => {'aa' => $nv});
test('install mm++-' => {'mm++' => $nv});
test('install nn---' => {'nn--' => $nv});

test('install aa+' => {});
test('install bb+' => {'bb' => 3});
test('install mm+++' => {});
test('install oo+++' => {'oo++' => 4});
test('install pp--+' => {'pp--' => 5});

test('remove aa bb+' => {'aa' => $nv, 'bb' => 3});
test('remove bb+' => {'bb' => 3});
test('remove oo+++' => {'oo++' => 4});
test('remove pp--+' => {'pp--' => 5});
test('remove aa oo+++ mm++' => {'aa' => $nv, 'oo++' => 4, 'mm++' => $nv});

test('remove aa-' => {'aa' => $nv});
test('remove bb-' => {});
test('remove mm++-' => {'mm++' => $nv});
test('remove nn---' => {'nn--' => $nv});
test('remove pp---' => {});

