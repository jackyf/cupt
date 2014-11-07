use TestCupt;
use Test::More tests => 20;

use strict;
use warnings;

sub test {
	my ($preferences, $expected_result) = @_;

	my $cupt = TestCupt::setup('preferences' => $preferences);

	my $condensed_preferences = join(" & ", split(/\n/, $preferences));
	my $comment = "preferences: '$condensed_preferences', expected parse result: $expected_result";
	my $expected_exit_code_is_zero = ($expected_result ? 1 : 0);

	my $exit_code = exitcode("$cupt policy");

	my $verify_method = ($expected_result ? \&is : \&isnt);
	$verify_method->($exit_code, 0, $comment);
}

my $valid_record = "Package: ccc\nPin: version *\nPin-Priority: 150\n";
test($valid_record => 1);

test("Yiposda7" => 0);
test("kasd&\n" => 0);
test("uuu\nppp\nqwerqwer: a\n" => 0);

test("Package: aaa\n" => 0);
test("Package: bbb\nPin-Priority: 34\n" => 0);
test("Package: ddd\nPin: version *\n" => 0);
test("Pin-Priority: 25\n" => 0);
test("Pin: release a=yyy\nPin-Priority: 1000" => 0);

test("Package: aaa\nPackage: bbb\nPin: version *\nPin-Priority: 10\n" => 0);
test("Package: bbb\n\nPin: version *\nPin-Priority: 199\n" => 0);

test("  \n$valid_record" => 1);
test(" _ \n$valid_record" => 0);
test("${valid_record}Qwerty: asdf\n" => 0);
test("Some: field\n${valid_record}" => 0);
test("${valid_record}\n${valid_record}" => 1);

test("# this is comment\n${valid_record}" => 1);
test("Explanation: this is explanation\n${valid_record}" => 1);
test("     # this is another comment\nExplanation: explaining more" => 1);
test("# a lot of\nExplanation: different\n# stuff \nExplanation: can be put\n  ### before\n${valid_record}" => 1);

