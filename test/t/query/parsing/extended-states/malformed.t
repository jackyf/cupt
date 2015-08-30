use Test::More tests => 6;
use IPC::Run3;

sub test {
	my ($record, $main_error) = @_;

	my $cupt = setup('extended_states' => $record);
	run3("$cupt show xyz", \undef, \undef, \my $errors);

	subtest to_one_line($record) => sub {
		like($errors, qr/^E: \Q$main_error\E$/m, 'specific error');
		like($errors, qr/^E: unable to parse extended states$/m, 'general failure message');
	}
}

sub get_bad_first_tag_error {
	my $tag = shift;
	return "wrong tag: expected 'Package', got '$tag'";
}
test("Pkg: xyz\n", get_bad_first_tag_error('Pkg'));
my $valid_ai = "Auto-Installed: 1\n";
test("Pkg: xyz\n$valid_ai", get_bad_first_tag_error('Pkg'));
test("UYIOa: pm\n", get_bad_first_tag_error('UYIOa'));

sub get_bad_value_error {
	my $value = shift;
	return "bad value '$value' (should be 0 or 1) for the package 'abc'";
}
my $valid_start = "Package: abc\n";
test("${valid_start}Auto-Installed: 2", get_bad_value_error('2'));
test("${valid_start}Auto-Installed: -5", get_bad_value_error('-5'));
test("${valid_start}Auto-Installed: quwe", get_bad_value_error("quwe"));

