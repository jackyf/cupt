use Test::More tests => 27;
use IPC::Run3;

use strict;
use warnings;

# ger = "Generate Error Regex"

sub ger_line_part {
	my $line = shift;
	return qr/^E: \(at the file '.*?', line $line\)$/m;
}

sub ger_desc_part {
	my $desc = shift;
	return qr/^E: $desc$/m;
}

sub test {
	my ($preferences, $expected_result, $expected_error_data) = @_;

	my $cupt = TestCupt::setup('preferences' => $preferences);

	my $condensed_preferences = join(" & ", split(/\n/, $preferences));
	my $comment = "preferences: '$condensed_preferences', expected parse result: $expected_result";
	my $expected_exit_code_is_zero = ($expected_result ? 1 : 0);

	run3("$cupt policy", \undef, \undef, \my $errors);
	my $exit_code = $?;

	subtest $comment => sub {
		my $verify_method = ($expected_result ? \&is : \&isnt);
		$verify_method->($exit_code, 0, 'exit code');

		if (defined $expected_error_data) {
			my ($line, $desc) = @$expected_error_data;
			like($errors, ger_desc_part($desc), 'error description');
			like($errors, ger_line_part($line), 'error line');
		}
	}
}


my $valid_record = "Package: ccc\nPin: version *\nPin-Priority: 150\n";
test($valid_record => 1);

my $e_inv_ps = "invalid package/source line";
test("Yiposda7" => 0, [1, $e_inv_ps]);
test("kasd&\n" => 0, [1, $e_inv_ps]);
test("uuu\nppp\nqwerqwer: a\n" => 0, [1, $e_inv_ps]);
test("\nHmm" => 0, [2, $e_inv_ps]);
test("\n\n#abc\n\nHmm" => 0, [5, $e_inv_ps]);

my $e_inv_pin = "invalid pin line";
test("Package: aaa\n" => 0, [2, "no pin line"]);
test("Package: bbb\nPin-Priority: 34\n" => 0, [2, $e_inv_pin]);
test("Package: ddd\nPin: version *\n" => 0, [3, "no priority line"]);
test("Pin-Priority: 25\n" => 0, [1, $e_inv_ps]);
test("Pin: release a=yyy\nPin-Priority: 1000" => 0, [1, $e_inv_ps]);

test("Package: aaa\nPackage: bbb\nPin: version *\nPin-Priority: 10\n" => 0, [2, $e_inv_pin]);
test("Package: bbb\n\nPin: version *\nPin-Priority: 199\n" => 0, [2, $e_inv_pin]);

test("  \n$valid_record" => 1);
test(" _ \n$valid_record" => 0, [1, $e_inv_ps]);
test("${valid_record}Qwerty: asdf\n" => 0, [4, $e_inv_ps]);
test("Some: field\n${valid_record}" => 0, [1, $e_inv_ps]);
test("${valid_record}\n${valid_record}" => 1);

test("# this is comment\n${valid_record}" => 1);
test("Explanation: this is explanation\n${valid_record}" => 1);
test("     # this is another comment\nExplanation: explaining more" => 1);
test("# a lot of\nExplanation: different\n# stuff \nExplanation: can be put\n  ### before\n${valid_record}" => 1);

my $valid_first_two_lines = "Package: eee\nPin: version *\n";
my $e_inv_priority = "invalid priority line";
test("${valid_first_two_lines}In-priority: 123\n" => 0, [3, $e_inv_priority]);
test("${valid_first_two_lines}%*Raj" => 0, [3, $e_inv_priority]);
test("${valid_first_two_lines}Pin-Priority: ehh\n" => 0, [3, "invalid integer 'ehh'"]);
test("${valid_first_two_lines}Pin-Priority: -22m\n" => 0, [3, "invalid integer '-22m'"]);
test("${valid_first_two_lines}Pin-Priority: --33\n" => 0, [3, "invalid integer '--33'"]);

