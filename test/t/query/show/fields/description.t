use Test::More tests => 5+3+14+3;

my $hash1 = '456af';
my $hash2 = 'bc890d7777';
my $hash3 = '512bba';

my $desc_engb = 'colourful specialised app';
my $desc_enus = 'colorful specialized app';
my $desc_fi = "värikäs räätälöity sovellus\n Ja paljon muuta.";
my $desc_da = "sø";

my $hash_tag = 'Description-md5';
my $desc_tag = 'Description';

sub compose_translation_record {
	my ($package, $lang, $hash, $desc) = @_;
	my $result = '';
	if (defined($package)) {
		$result .= "Package: $package\n";
	}
	$result .= "$hash_tag: $hash\n";
	$result .= "$desc_tag-$lang: $desc\n";
}

sub compose_desc_addendum {
	my ($hash, $desc) = @_;

	my $result = '';
	if (defined($hash)) {
		$result .= "$hash_tag: $hash\n";
	}
	if (defined($desc)) {
		$result .= "$desc_tag: $desc\n";
	}
	return $result;
}

sub our_setup {
	my $record_addendum = shift;
	return setup(
		'releases' => [{
			'packages' => [
				compose_package_record('aa', 0) . $record_addendum ,
			],
			'translations' => {
				'en' => [
					compose_translation_record('aa', 'en', $hash1, $desc_engb),
				],
				'en_US' => [
					compose_translation_record('aa', 'en_US', $hash1, $desc_enus),
					compose_translation_record('aa', 'en_US', $hash2, 'second thing'),
				],
				'fi' => [
					compose_translation_record('ihansama', 'fi', $hash1, $desc_fi),
					compose_translation_record('aa', 'fi', $hash3, 'pienehkö paketti'),
				],
				'da' => [
					compose_translation_record(undef, 'da', $hash1, $desc_da)
				]
			}
		}]
	);
}

my $order = undef;

sub test {
	my ($i, $expected_desc, $comment) = @_;

	$ENV{LC_ALL} = $i->{env}//'C';
	my $cupt = our_setup(compose_desc_addendum($i->{hash}, $i->{desc}));
	my $options = defined($i->{order}) ? " -o cupt::languages::indexes=$i->{order}" : '';
	my $output = stdall("$cupt show aa $options");

	subtest $comment => sub {
		unlike($output, qr/$hash_tag/, 'no hash in output');
		if (defined($expected_desc)) {
			like($output, qr/^\w+: $expected_desc$/m, 'description is right');
		} else {
			unlike($output, qr/Description/, 'no description');
		}
	}
}

test({} => undef, 'no description information');
test({hash=>'78bce'} => undef, 'unknown hash');
test({desc=>'dum'} => 'dum', 'original description, no hash');
test({hash=>'712ef',desc=>'dum'} => 'dum', 'original description, unknown hash');
test({desc=>"short\n And long.\n"} => "short\n And long.", 'original long description');

test({hash=>$hash3} => undef, 'language missing in the order');
test({hash=>$hash3,order=>'fi'} => 'pienehkö paketti', 'language present in overridden order');
test({hash=>$hash3,desc=>'uh'} => 'uh', 'language missing, original description present');

test({hash=>$hash1} => $desc_engb, 'translation found');
test({hash=>$hash1,order=>'environment'} => undef, 'only enviroment language, no match');
test({hash=>$hash1,order=>''} => undef, 'no languages in order');
test({hash=>$hash1,order=>'environment',env=>'da_DK.UTF-8'} => $desc_da, 'only enviroment language, there is a match');
test({hash=>$hash1,order=>'en_US'} => $desc_enus, 'specific country order');
test({hash=>$hash1,order=>'en,en_US'} => $desc_engb, 'order has general translation before country-specific one');
test({hash=>$hash1,env=>'en_US.UTF-8'} => $desc_enus, 'specific locale country');
test({hash=>$hash1,order=>'en,environment', env=>'en_US.UTF-8'} => $desc_engb, 'first in order wins even if local matches perfectly');
test({hash=>$hash1,order=>'bla'} => undef, 'no language match, no original description');
test({hash=>$hash1,desc=>'uh',order=>'bla'} => 'uh', 'no language match, original description present');
test({hash=>$hash1,order=>'fi,bla'} => $desc_fi, 'first language matched');
test({hash=>$hash1,order=>'bla,fi'} => $desc_fi, 'second language matched');
test({hash=>$hash1,order=>'bla,da,fi'} => $desc_da, 'second and third language matched');
test({hash=>$hash1,order=>'bla,none,da,fi'} => $desc_da, 'none is ignored');

test({hash=>$hash2} => undef, 'no language match in default order');
test({hash=>$hash2,order=>'fi,bla,da'} => undef, 'no language match in specific order');
test({hash=>$hash2,env=>'en_US.UTF-8'} => 'second thing', 'language match for country-only translation');

