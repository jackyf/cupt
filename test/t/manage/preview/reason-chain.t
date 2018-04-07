use Test::More tests => 6;

my $cupt = TestCupt::setup(
	'dpkg_status' => [
		compose_installed_record('broken', '4') . "Depends: subbroken1 | subbroken2\n" ,
		compose_installed_record('subbroken1', '5') ,
		compose_installed_record('subbroken2', '6') ,
	],
	'packages' => [
		compose_package_record('top', '1') . "Depends: middle\n" ,
		compose_package_record('middle', '2') . "Pre-Depends: bottom\n" ,
		compose_package_record('bottom', '3') . "Breaks: broken\n" ,
		compose_package_record('subalt', '7') . "Breaks: subbroken1, subbroken2\n" ,
	],
);

sub get_reason_chain {
	my ($package_to_install, $package_down_chain) = @_;

	my $input = "rc\n$package_down_chain\n";
	my $output = `echo '$input' 2>&1 | $cupt -s install $package_to_install 2>&1`;

	my ($result) = ($output =~ m/to show reason chain.*?\n((?:$package_down_chain).+?)\n\n/s);

	if (not defined $result) {
		return "Reason chain extraction failed, full output:\n" . $output;
	}

	return $result;
}

my $answer = '';

sub add_answer_level {
	my ($line) = @_;

	$answer =~ s/^./  $&/gm;
	$answer = "$line\n$answer";
}


$answer = "top: user request: install top | for package 'top'";
is(get_reason_chain('top' => 'top'), $answer);
add_answer_level("middle: top 1 depends on 'middle'");
is(get_reason_chain('top' => 'middle'), $answer);
add_answer_level("bottom: middle 2 pre-depends on 'bottom'");
is(get_reason_chain('top' => 'bottom'), $answer);
add_answer_level("broken: bottom 3 breaks 'broken'");
is(get_reason_chain('top' => 'broken'), $answer);

like(get_reason_chain('middle' => 'top'), qr/extraction failed/);

$answer = <<'END';
broken: broken 4^installed depends on 'subbroken3 | subbroken3'
  subbroken3: subalt 7 breaks 'subbroken3'
    subalt: user request: install subalt | for package 'subalt'
  subbroken3: subalt 7 breaks 'subbroken3'
    subalt: user request: install subalt | for package 'subalt'
END
my $deterministic_reason_chain = get_reason_chain('subalt' => 'broken');
$deterministic_reason_chain =~ s/[12]/3/g;

chomp($answer);
is($deterministic_reason_chain, $answer);

