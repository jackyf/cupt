use TestCupt;
use Test::More tests => 6;

use strict;
use warnings;

eval get_inc_code('../common');
die($@) if $@;

my $cupt = setup_for_worker(
	'dpkg_status' =>
		entail(compose_installed_record('bb', 2) . "Depends: dd\n") .
		entail(compose_installed_record('dd', 6)) ,
	'packages' =>
		entail(compose_package_record('aa', 1)) .
		entail(compose_package_record('bb', 3) . "Depends: dd\n") .
		entail(compose_package_record('c2', 4) . "Depends: bb (>= 3)\n") .
		entail(compose_package_record('c1', 5) . "Depends: c2\n") ,
);

sub test {
	my ($user_command, @expected) = @_;

	my $output = stdall(get_worker_command($cupt, $user_command));

	my @parsed_output = parse_dpkg_commands($output);
	is_deeply(\@parsed_output, \@expected, $user_command) or
			diag($output);
}

test('install aa' => ['--install', [], ['<aa 1>']]);
test('remove bb' => ['--remove', [], ['bb']]);
test('install bb' => ['--install', [], ['<bb 3>']]);

test('install c2' =>
		['--install', [], ['<bb 3>']],
		['--install', [], ['<c2 4>']]);
test('install c1' =>
		['--install', [], ['<bb 3>']],
		['--install', [], ['<c2 4>']],
		['--install', [], ['<c1 5>']]);
test('remove dd' =>
		['--remove', [], ['bb']],
		['--remove', [], ['dd']]);

