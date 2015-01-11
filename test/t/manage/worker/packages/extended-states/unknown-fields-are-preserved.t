use TestCupt;
use Test::More tests => 1;

use strict;
use warnings;

my $initial_extended_states_content = <<END;
Package: aa
Auto-Installed: 1
Architecture: q96

Package: bb
Auto-Installed: 1
Some-Aptitude-Tag: some-value

Package: vv
Somename: 0

END

(my $expected_extended_states_content = $initial_extended_states_content)
		=~ s/(Package: aa\n)Auto-Installed: 1\n/$1/;

eval get_inc_code('common');

my $cupt = setup_for_worker(
	'dpkg_status' =>
		entail(compose_installed_record('aa', 1)) .
		entail(compose_installed_record('bb', 2)),
	'extended_states' =>
		$initial_extended_states_content
);

local $TODO = 'not implemented';

subtest "the test" => sub {
	my $output = stdall(get_worker_command($cupt, 'unmarkauto aa --no-auto-remove', 'simulate'=>0));
	is($?, 0, 'unmarkauto succeeded')
			or diag($output);
	is(get_new_extended_states_content(), $expected_extended_states_content, 'unknown tags are preserved');
}

