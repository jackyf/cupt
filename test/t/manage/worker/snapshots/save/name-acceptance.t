use TestCupt;
use Test::More tests => 20;

use strict;
use warnings;

my $cupt = TestCupt::setup();

eval get_inc_code('../common');


sub test_good_name {
	my ($name, $description) = @_;
	test_snapshot_command("save '$name'", undef, "good: $description");
}

test_good_name('abcdef', 'lower letters');
test_good_name('abc-def', 'dash');
test_good_name('abc_def', 'underscore');
test_good_name('ndjnvjodsnadsnvhansdvasodvnaosdhjqiuewohfqiuwe', 'long');


sub test_bad_name {
	my ($name, $description) = @_;
	my $snapshot_name_error_regex = qr/^E: the system snapshot name \Q'$name'\E cannot/m;
	test_snapshot_command("save '$name'", $snapshot_name_error_regex, "bad: $description");
}

TODO: {
	local $TODO = 'bug';
	test_bad_name('', 'empty');
}
test_bad_name('uuu www', 'space');
test_bad_name('.abc', 'dot at the beginning'); 
test_bad_name('../uuu', 'parent directory at the beginning');
TODO: {
	local $TODO = 'bug';
	test_bad_name('uuu/www', 'slash');
	test_bad_name('uuu/../www', 'parent directory at the middle');
	test_bad_name('xxx/..', 'parent directory at the end');
	test_bad_name('uUu', 'big letter');
	test_bad_name('uuu*www', 'asterisk');
	test_bad_name('uuu\www', 'backslash');
	test_bad_name('%s', 'percent');
	test_bad_name('name!', 'exclamation mark');
	test_bad_name('xyz?', 'question mark');
	test_bad_name('xyz|uuu', 'pipe');
	test_bad_name('qw;', 'semicolon');
	test_bad_name('#$TY(', 'garbage characters');
}

