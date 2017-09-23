use Test::More tests => 4;

require(get_rinclude_path('common'));
set_parse_skip_triggers(0);

my $cupt = setup(
	'packages' => [ compose_package_record('aa', 1) ],
);

sub test {
	my ($params, $triggers_enabled, $name) = @_;

	# after-trigger command is given even when triggers are not deferred (see #766758)
	test_dpkg_sequence($cupt, "install aa $params",
			['--triggers-only', ['-a'], []],
			['--install', $triggers_enabled?['--no-triggers']:[], ['<aa 1>']],
			['--triggers-only', ['--pending'], []]);
}

test('-o cupt::worker::defer-triggers=no' => 0);
test('-o cupt::worker::defer-triggers=auto' => 1);
test('' => 1);
test('-o cupt::worker::defer-triggers=yes' => 1);

