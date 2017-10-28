use Test::More tests => 4;

my $orig = "aaa(<<  3.2~xyz+1 )";
my $parsed = "aaa (<< 3.2~xyz+1)";

sub test {
	my ($type) = @_;

	my $cupt = setup(
		'sources' => [
			compose_package_record('ee', 0) . "$type: $orig\n" , 
		],
	);

	my $output = stdall("$cupt showsrc ee");

	subtest $type => sub {
		like($output, qr/^$type: \Q$parsed\E$/m, 'parsed relation is printed');

		my $count = () = $output =~ m/Build-/;
		is($count, 1, 'only one relation printed');
	}
}

test('Build-Depends');
test('Build-Depends-Indep');
test('Build-Conflicts');
test('Build-Conflicts-Indep');

