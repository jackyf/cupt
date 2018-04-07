use Test::More tests => 1;

require(get_rinclude_path('common'));

my $cupt = setup(
	'dpkg_status' => [
		compose_installed_record('subbroken1', '5') ,
		compose_installed_record('subbroken2', '6') ,
		compose_installed_record('broken', '4') . "Depends: subbroken1 | subbroken2\n" ,
	],
	'packages' => [
		compose_package_record('subalt', '7') . "Breaks: subbroken1, subbroken2\n" ,
	],
);

my $answer = <<'END';
broken: broken 4^installed depends on 'subbroken3 | subbroken3'
  subbroken3: subalt 7 breaks 'subbroken3'
    subalt: user request: install subalt | for package 'subalt'
  subbroken3: subalt 7 breaks 'subbroken3'
    subalt: user request: install subalt | for package 'subalt'
END
my $deterministic_reason_chain = get_reason_chain($cupt, 'subalt' => 'broken');
$deterministic_reason_chain =~ s/[12]/3/g;

chomp($answer);
is($deterministic_reason_chain, $answer);

