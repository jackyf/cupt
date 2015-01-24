use TestCupt;
use Test::More tests => 1;

use strict;
use warnings;

eval get_inc_code('../common');
die($@) if $@;

my $cupt = setup_for_worker(
	'packages' =>
		entail(compose_package_record('aa', 1)),
);

sub test {
	my ($user_command, @expected) = @_;

	my $output = stdall(get_worker_command($cupt, $user_command));

	my @parsed_output = parse_dpkg_commands($output);
	is_deeply(\@parsed_output, \@expected, $user_command) or
			diag($output);
}

test('install aa' => ['--install', [], ['<aa 1>']]);

