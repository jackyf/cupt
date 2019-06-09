use Test::More tests => 2+3+4+3+4;
use CuptInteractive;

require(get_rinclude_path('actions'));
require(get_rinclude_path('color-check'));

sub test {
	my ($option_value, $is_tty, $term, $got_colors) = @_;
	my $desc = "option value: $option_value, tty: $is_tty, term: $term -> $got_colors";

	my ($cupt, $combined_command) = setup_for_actions('installed', '');
	$cupt = "TERM=$term $cupt";
	$combined_command .= " -o cupt::console::use-colors=$option_value -y -s";

	my $output = $is_tty
			? CuptInteractive->new("$cupt shell")->execute($combined_command)
			: stdall("$cupt $combined_command");

	my $color_string = color('bold cyan');
	my $checker = ($got_colors ? \&like : \&unlike);
	$checker->($output, qr/\Q$color_string\E/, $desc);
}

test('yes', 0, 'mainframe' => 1);
test('yes', 1, 'bsd' => 1);

test('no', 0, 'linux' => 0);
test('no', 1, 'linux' => 0);
test('no', 1, 'haiku' => 0);

test('auto', 0, 'xterm' => 0);
test('auto', 0, 'emacs' => 0);
test('auto', 1, 'xterm' => 1);
test('auto', 1, 'emacs' => 0);

test('auto', 1, 'xt' => 0);
test('auto', 1, 'linuzz' => 0);
test('auto', 1, 'linux' => 1);

test('auto', 1, 'xtermal' => 0);
TODO: {
	local $TODO = 'other xterm variants';
	test('auto', 1, 'xterm-color' => 1);
	test('auto', 1, 'xterm-256color' => 1);
	test('auto', 1, 'xterm-qwerty' => 1);
}

