use Test::More tests => 3+1+4;

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

sub not_avail_regex {
	my ($type, $name, $version) = @_;
	return qr/\Q$type: the package '$name' cannot be reinstalled because there is no corresponding version ($version) available in repositories\E/;
}

my $pp_yes_regex = qr/will be reinstalled:\n\npp\s*\n/;

test('there is a reinstall candidate', 'pp', $pp_yes_regex);
test('there are no reinstall candidates (some other version available)', 'qq', not_avail_regex('E', 'qq', 2));
test('there are no reinstall candidates (no other versions available)', 'rr', not_avail_regex('E', 'rr', 0));

test('wish of a available reinstall', '--wish pp', $pp_yes_regex);

my $qq_soft_no_regex = not_avail_regex('W', 'qq', 2);
test('wish of an unavailable reinstall', '--wish qq', $qq_soft_no_regex);
test('try of an unavailable reinstall', '--try qq', $qq_soft_no_regex);
test('big priority request of an unavailable reinstall', '--importance=1000000 qq', $qq_soft_no_regex);
test('wish of a available and unavailable', '--wish pp qq', $pp_yes_regex);

