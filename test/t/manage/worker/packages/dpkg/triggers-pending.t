use TestCupt;
use Test::More tests => 1;

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

my $output = stdall(get_worker_command($cupt, 'install aa'));

my @commands = ($output =~ m/^S: running command '(.*)'/mg);

is($commands[-1], compose_dpkg_aux_command('--triggers-only --pending'),
		'after-trigger command is given by default');

