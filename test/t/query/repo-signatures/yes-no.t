use Test::More tests => 5 + 5*3 + 7*5;

require(get_rinclude_path('common'));

my $keyring1 = get_keyring_path('good-1');
my $keyring2 = get_keyring_path('good-2');

sub test {
	my ($input, $expected_result, $desc) = @_;
	$desc .= " --> " . ($expected_result?"trusted":"untrusted");

	my $output = get_output($input);
	if ($expected_result) {
		like($output, qr/^Version: 1$/m, $desc);
	} else {
		like($output, qr/^E: .*selected nothing/m, $desc);
	}
}

test({'signer'=>\&no_signer}, 0, 'no signature, no keyrings');
test({'signer'=>\&bad_signer}, 0, 'bad signature, no keyrings');
test({'keyrings'=>[$keyring1], 'signer'=>\&no_signer}, 0, 'no signature, keyring 1');
test({'keyrings'=>[$keyring1], 'signer'=>\&bad_signer}, 0, 'bad signature, keyring 1');
test({'keyrings'=>[$keyring1, $keyring2], 'signer'=>\&bad_signer}, 0, 'bad signature, keyring 1+2');

sub test_good_signer {
	my ($files, $key, $expected_result, $desc) = @_;

	my $keyring = $key == 1 ? $keyring1 : $keyring2;
	my $signer = get_good_signer($keyring);
	my $test_variants = sub {
		my ($variants, $sub_expected_result, $sub_desc) = @_;
		my $input = {'keyrings'=>$files, 'signer'=>$signer, 'sign-variants'=>$variants};
		test($input => $sub_expected_result, $sub_desc);
	};

	$test_variants->(undef, $expected_result, "key $key (detached), $desc");
	$test_variants->(['orig','inline'] => $expected_result, "key $key (inline), $desc");
	$test_variants->(['orig','detached','inline'] => $expected_result, "key $key (inline + detached), $desc");
	if ($expected_result) {
		$test_variants->(['detached'] => 0, "key $key (detached without original), $desc");
		$test_variants->(['inline'] => $expected_result, "key $key (inline without original), $desc");
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

