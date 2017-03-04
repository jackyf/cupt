use Test::More tests => 13;

require(get_rinclude_path('actions'));

sub test {
	my ($first, $second) = @_;

	my ($cupt, $combined_command) = setup_for_actions($first, $second);

	my $expected_regex = qr/ $first.*\n{2,}.* $second/s;
	my $output = get_first_offer("$cupt $combined_command");
	like($output, $expected_regex, "relative order: $first -> $second ($combined_command)");
}

test('marked as auto', 'marked as manual');
test('marked as manual', 'reinstalled');
test('reinstalled', 'installed');
test('installed', 'upgraded');
test('upgraded', 'removed');
test('removed', 'purged');
test('purged', 'downgraded');
test('downgraded', 'configured');
test('configured', 'triggers');
test('triggers', 'deconfigured');
test('deconfigured', 'not preferred');
test('not preferred', 'auto-removed');
test('not preferred', 'auto-purged');

