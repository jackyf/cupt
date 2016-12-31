use Test::More tests => 2;

my $cupt;
eval get_inc_code('common');
set_parse_skip_triggers(0);

$cupt = setup_for_worker(
	'packages' => entail(compose_package_record('aa', 1))
);

sub test {
	my ($params, $name) = @_;

	my $output = stdall(get_worker_command($cupt, "install aa $params"));
	my @commands = parse_dpkg_commands($output);

	is_deeply($commands[-1], ['--triggers-only', ['--pending'], []], $name);
}

test('', 'after-trigger command is given by default');
test('-o cupt::worker::defer-triggers=no', 'after-trigger command is given even when triggers are not deferred (see #766758)');

