use Test::More tests => 14;
use Term::ANSIColor;

require(get_rinclude_path('actions'));

sub apply_attributes {
	my ($str, $attrs) = @_;
	return $str unless (@$attrs);
	return colored($attrs, $str);
}

sub test {
	my ($action, $action_phrase, $attrs) = @_;

	my ($cupt, $combined_command) = setup_for_actions($action, '');

	my $expected_formatted_action = apply_attributes($action_phrase, $attrs);
	my $expected_regex = qr/ \Q$expected_formatted_action:\E/;
	my $printed_attrs = join(", ", @$attrs);

	my $output = get_first_offer("$cupt $combined_command -o cupt::console::use-colors=yes");
	like($output, $expected_regex, "color attributes for $action: '$printed_attrs'");
}

test('installed', 'will be installed' => []);
test('removed', 'will be removed' => ['bold']);
test('upgraded', 'will be upgraded' => []);
test('purged', 'will be purged' => ['bold']);
test('downgraded', 'will be downgraded' => ['bold']);
test('configured', 'will be configured' => []);
test('deconfigured', 'will be deconfigured', ['bold']);
test('triggers', 'will have triggers processed', []);
test('reinstalled', 'will be reinstalled', ['bold']);
test('not preferred', 'will have a not preferred version', ['bold']);
test('auto-removed', 'will be auto-removed', []);
test('auto-purged', 'will be auto-purged', []);
test('marked as auto', 'will be marked as automatically installed', []);
test('marked as manual', 'will be marked as manually installed', []);

