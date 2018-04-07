use Test::More tests => 5;

require(get_rinclude_path('common'));

my $cupt = TestCupt::setup(
	'dpkg_status' => [
		compose_installed_record('unwanted', '4') ,
	],
	'packages' => [
		compose_package_record('top', '1') . "Depends: middle\n" ,
		compose_package_record('middle', '2') . "Pre-Depends: bottom\n" ,
		compose_package_record('bottom', '3') . "Breaks: unwanted\n" ,
	],
);

my $answer = '';

sub add_answer_level {
	my ($line) = @_;

	$answer =~ s/^./  $&/gm;
	$answer = "$line\n$answer";
}


$answer = "top: user request: install top | for package 'top'";
is(get_reason_chain($cupt, 'top' => 'top'), $answer);
add_answer_level("middle: top 1 depends on 'middle'");
is(get_reason_chain($cupt, 'top' => 'middle'), $answer);
add_answer_level("bottom: middle 2 pre-depends on 'bottom'");
is(get_reason_chain($cupt, 'top' => 'bottom'), $answer);
add_answer_level("unwanted: bottom 3 breaks 'unwanted'");
is(get_reason_chain($cupt, 'top' => 'unwanted'), $answer);

like(get_reason_chain($cupt, 'middle' => 'top'), qr/extraction failed/);

