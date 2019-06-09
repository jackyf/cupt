use Test::More tests => 14;

require(get_rinclude_path('actions'));
require(get_rinclude_path('color-check'));

sub test {
	my ($action, $action_phrase, $phrase_attrs, $package_attrs) = @_;

	my ($cupt, $combined_command) = setup_for_actions($action, '');
	my $output = get_first_offer("$cupt $combined_command -o cupt::console::use-colors=yes");

	subtest "color attributes for $action" => sub {
		check_color_part('name', $output, $action_phrase, $phrase_attrs);
		check_color_part('packages', $output, qr/\w+/, $package_attrs);
	};
}

# for bold / not bold for packages we assume that the package is manually installed
# unless the action implies otherwise

test('installed', 'will be installed' => undef, 'bold cyan');
test('removed', 'will be removed' => 'bold', 'bold yellow');
test('upgraded', 'will be upgraded' => undef, 'bold green');
test('purged', 'will be purged' => 'bold', 'bold red');
test('downgraded', 'will be downgraded' => 'bold', 'bold magenta');
test('configured', 'will be configured' => undef, 'bold blue');
test('deconfigured', 'will be deconfigured' => 'bold', 'bold');
test('triggers', 'will have triggers processed' => undef, 'bold');
test('reinstalled', 'will be reinstalled', 'bold' => 'bold');
test('not preferred', 'will have a not preferred version' => 'bold', 'bold');
test('auto-removed', 'will be auto-removed' => undef, 'yellow');
test('auto-purged', 'will be auto-purged' => undef, 'red');
test('marked as auto', 'will be marked as automatically installed' => undef, undef );
test('marked as manual', 'will be marked as manually installed' => undef, undef);

