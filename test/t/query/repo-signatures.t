use Test::More tests => 5 + 5*3 + 7*5;

my $apt_keyring_dir = 'etc/apt/trusted.gpg.d/';
my $apt_keyring_file = 'etc/apt/trusted.gpg';

my ($keyring1, $keyring2) = get_keyring_paths();

sub bad_signer {
	return 'garbage';
}

sub no_signer {
	return undef;
}

sub link_keyring {
	my ($source, $target) = @_;
	if (defined $source) {
		symlink($source => $target);
	} else {
		unlink($target);
	}
}

sub get_variant_filter_hook {
	my $variants = shift;
	return sub {
		my ($variant, undef, undef, $content) = @_;
		my $found = grep { $variant eq $_ } @$variants;
		return $found ? $content : undef;
	}
}

sub get_output {
	my ($files, $signer, $sign_variants) = @_;
	$sign_variants //= ['orig','detached'];

	my $cupt = setup(
		'packages2' => [
			{
				'content' => entail(compose_package_record('p', 1)),
				'trusted' => 'check',
				'hooks' => {
					'sign' => {
						'input' => get_variant_filter_hook($sign_variants),
						'convert' => $signer,
					},
				},
			}
		]
	);

	mkdir $apt_keyring_dir or die;
	link_keyring($files->[0] => $apt_keyring_file);
	link_keyring($files->[1] => "$apt_keyring_dir/first.gpg");
	link_keyring($files->[2] => "$apt_keyring_dir/second.gpg");

	return stdall("$cupt -o debug::gpgv=yes show 'trusted()'");
}

sub test {
	my ($input, $expected_result, $desc) = @_;
	$desc .= " --> " . ($expected_result?"trusted":"untrusted");

	my $output = get_output(@$input);
	if ($expected_result) {
		like($output, qr/^Version: 1$/m, $desc);
	} else {
		like($output, qr/^E: .*selected nothing/m, $desc);
	}
}

test([[], \&no_signer], 0, 'no signature, no keyrings');
test([[], \&bad_signer], 0, 'bad signature, no keyrings');
test([[$keyring1], \&no_signer], 0, 'no signature, keyring 1');
test([[$keyring1], \&bad_signer], 0, 'bad signature, keyring 1');
test([[$keyring1, $keyring2], \&bad_signer], 0, 'bad signature, keyring 1+2');

sub test_good_signer {
	my ($files, $key, $expected_result, $desc) = @_;

	my $keyring = $key == 1 ? $keyring1 : $keyring2;
	my $signer = get_good_signer($keyring);
	test([$files, $signer] => $expected_result, "key $key (detached), $desc");
	test([$files, $signer, ['orig','inline']] => $expected_result, "key $key (inline), $desc");
	test([$files, $signer, ['orig','detached','inline']] => $expected_result, "key $key (inline + detached), $desc");
	if ($expected_result) {
		test([$files, $signer, ['detached']] => 0, "key $key (detached without original), $desc");
		test([$files, $signer, ['inline']] => $expected_result, "key $key (inline without original), $desc");
	}
}

test_good_signer([], 1, 0, 'no keyrings');
test_good_signer([], 2, 0, 'no keyrings');
test_good_signer([$keyring1], 2, 0, 'keyring 1');
test_good_signer([$keyring2], 1, 0, 'keyring 2');
test_good_signer([undef, $keyring2], 1, 0, 'keyring 2 (location 2)');

test_good_signer([$keyring1], 1, 1, 'keyring 1');
test_good_signer([undef, $keyring1], 1, 1, 'keyring 1 (location 2)');
test_good_signer([undef, undef, $keyring1], 1, 1, 'keyring 1 (location 3)');
test_good_signer([$keyring1, $keyring2], 1, 1, 'keyring 1+2');
test_good_signer([undef, $keyring1, $keyring2], 1, 1, 'keyring 1+2 (location 2)');
test_good_signer([$keyring2, $keyring1], 1, 1, 'keyring 2+1');
test_good_signer([$keyring2, undef, $keyring1], 1, 1, 'keyring 2+1 (location 2)');

