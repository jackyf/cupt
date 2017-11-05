use Test::More tests => 2;

sub setup_cupt {
	my ($available_version) = @_;

	return setup(
		'dpkg_status' => [ compose_installed_record('pp', '4.5-1') ],
		'packages' => [ compose_package_record('pp', $available_version) ],
	);
}

sub test {
	my ($comment, $available_version, $regex) = @_;

	my $cupt = setup_cupt($available_version);
	my $offer = get_first_offer("$cupt -o cupt::console::actions-preview::show-versions=no reinstall pp");

	like($offer, $regex, $comment);
}

test("there is a reinstall candidate", '4.5-1', qr/will be reinstalled:\n\npp\s*\n/);
test("there are no reinstall candidates", '4.5-2',
		qr/\QE: the package 'pp' cannot be reinstalled because there is no corresponding version (4.5-1) available in repositories\E/);

