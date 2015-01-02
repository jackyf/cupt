use TestCupt;
use Test::More tests => 2;

use strict;
use warnings;

eval get_inc_code('../common');

sub compose_dpkg_aux_command {
	my ($command) = @_;
	return "/usr/bin/dpkg $command";
}

my $cupt = setup_for_worker(
	'packages' => entail(compose_package_record('aa', 1))
);

sub test {
	my ($params, $name) = @_;

	my $output = stdall(get_worker_command($cupt, "install aa $params"));

	my @commands = ($output =~ m/^S: running command '(.*)'/mg);

	is($commands[-1], compose_dpkg_aux_command('--triggers-only --pending'), $name);
}

test('', 'after-trigger command is given by default');
test('-o cupt::worker::defer-triggers=no', 'after-trigger command is given even when triggers are not deferred (see #766758)');

