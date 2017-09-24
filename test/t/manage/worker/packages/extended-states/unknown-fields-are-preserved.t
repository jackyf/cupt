use Test::More tests => 1;

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

require(get_rinclude_path('common'));

my $cupt = setup(
	'dpkg_status' => [
		compose_installed_record('aa', 1) ,
		compose_installed_record('bb', 2) ,
	],
	'extended_states' => $initial_extended_states_content
);

subtest "the test" => sub {
	my $output = stdall(get_worker_command($cupt, 'unmarkauto aa --no-auto-remove', 'simulate'=>0));
	is($?, 0, 'unmarkauto succeeded')
			or diag($output);
	is(get_new_extended_states_content(), $expected_extended_states_content, 'unknown tags are preserved');
}

