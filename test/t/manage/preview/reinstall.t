use Test::More tests => 3;

my $cupt = setup(
	'dpkg_status' => [
		compose_installed_record('pp', '4.5-1') ,
		compose_installed_record('qq', '2') ,
		compose_installed_record('rr', 0) ,
	],
	'packages' => [
		compose_package_record('pp', '4.5-1'),
		compose_package_record('qq', '3'),
	],
);

sub test {
	my ($comment, $arguments, $regex) = @_;

	my $offer = get_first_offer("$cupt -o cupt::console::actions-preview::show-versions=no reinstall $arguments");

	like($offer, $regex, $comment);
}

test('there is a reinstall candidate', 'pp', qr/will be reinstalled:\n\npp\s*\n/);
test('there are no reinstall candidates (some other version available)', 'qq',
		qr/\QE: the package 'qq' cannot be reinstalled because there is no corresponding version (2) available in repositories\E/);
test('there are no reinstall candidates (no other versions available)', 'rr',
		qr/\QE: the package 'rr' cannot be reinstalled because there is no corresponding version (0) available in repositories\E/);

