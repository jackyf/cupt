use Test::More tests => 2;
use IPC::Run3;

require(get_rinclude_path('../../common'));

my $long_a = 'a' x 200;

sub get_many_packages {
	my $get_package = sub { entail(compose_package_record("${long_a}xyz$_", "$_")) };
	join('', map(&$get_package, (1..800)));
}

my $cupt = setup(
	'releases' => [{
		'packages' => get_many_packages(),
		'deb-caches' => 1,
	}]
);

sub test {
	my ($comment, $hook_command, $checker) = @_;

	my $hook_options = "-o dpkg::pre-install-pkgs::='$hook_command' ";
	my $command = get_worker_command("timeout 90s $cupt", "install '*xyz*' $hook_options", 'simulate'=>0);

	my $output;
	subtest $comment => sub {
		run3($command, \undef, \$output, \$output);

		is($?, 0, 'exit code indicates success');
		$checker->($output);
		unlike($output, qr/unable to close a part/, 'no fd closing warning messages');
	} or diag($output);
}

test('input is given',
		'cat',
		sub {
			my $output = shift;
			like($output, qr/xyz5_5_.*\.deb/, 'xyz line is present');
		});

test('does not hang when a hook does not accept an input',
		'/mrr/nobinary --option1 || exit 0; exec abc',
		sub {
			pass('execution continues');
		});

