use Test::More tests => 7;
use Term::ANSIColor;

sub compose_options {
	my ($color_value, $manual_value, $auto_value) = @_;
	my $result = " -o cupt::console::use-colors=$color_value";
	my $indicators_prefix = 'cupt::console::actions-preview::package-indicators';
	$result .= " -o ${indicators_prefix}::manually-installed=$manual_value" if defined($manual_value);
	$result .= " -o ${indicators_prefix}::automatically-installed=$auto_value" if defined($auto_value);
	return $result;
}

sub test {
	my ($desc, $options, $expected_printed_packages) = @_;

	my $cupt = setup('packages' => [
		compose_package_record('ppp', 1) . "Depends: qqq\n",
		compose_package_record('qqq', 2),
	]);
	my $command = 'install ppp';
	my $option_line = compose_options(@$options);
	my $output = get_first_offer("$cupt $command $option_line", 'disable-package-indicators'=>0);

	my ($expected_manual_package, $expected_auto_package) = @$expected_printed_packages;
	subtest $desc => sub {
		like($output, qr/^\Q$expected_manual_package\E /m, 'manually installed package');
		like($output, qr/^\Q$expected_auto_package\E /m, 'automatically installed package');
	};
}

test('coloring on, suffixes off' => ['yes', 'no', 'no'] =>
		[ colored('ppp', 'bold cyan'), colored('qqq', 'cyan') ]);
test('coloring on, suffixes on (but overridden by coloring)' => ['yes', 'auto', 'auto'] =>
		[ colored('ppp', 'bold cyan'), colored('qqq', 'cyan') ]);

test('coloring off, suffixes off' => ['no', 'no', 'no'] => ['ppp', 'qqq']);
test('coloring off, manual suffix on' => ['no', 'auto', 'no'] => ['ppp{m}', 'qqq']);
test('coloring off, auto suffix on' => ['no', 'no', 'auto'] => ['ppp', 'qqq{a}']);
test('coloring off, both suffixes on' => ['no', 'auto', 'auto'] => ['ppp{m}', 'qqq{a}']);

test('coloring off, default suffixes' => ['no', undef, undef] => ['ppp{m}', 'qqq']);

