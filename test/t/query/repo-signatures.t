use Test::More tests => 5+5+7+5;

my $apt_keyring_dir = 'etc/apt/trusted.gpg.d/';
my $apt_keyring_file = 'etc/apt/trusted.gpg';

my ($keyring1, $keyring2) = get_keyring_paths();

sub get_good_signer {
	my ($keyring, $is_inline) = @_;
	$is_inline //= 0;
	my $command = ($is_inline ? '--clearsign' : '--detach-sign');
	return sub {
		my ($file) = @_;
		my $output = `gpg2 --no-default-keyring --keyring $keyring --output - $command $file`;
		return ($is_inline, $output);
	};
}

sub bad_signer {
	return (0, 'garbage');
}

sub link_keyring {
	my ($source, $target) = @_;
	if (defined $source) {
		symlink($source => $target);
	} else {
		unlink($target);
	}
}

sub get_output {
	my ($files, $signer) = @_;

	my $cupt = setup(
		'packages2' => [
			{
				'content' => entail(compose_package_record('p', 1)),
				'signer' => $signer,
				'trusted' => 'check'
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
	my ($files, $signer, $expected_result, $desc) = @_;
	$desc .= " --> " . ($expected_result?"trusted":"untrusted");

	my $output = get_output($files, $signer);
	if ($expected_result) {
		like($output, qr/^Version: 1$/m, $desc);
	} else {
		like($output, qr/^E: .*selected nothing/m, $desc);
	}
}

test([], undef, 0, 'no signature, no keyrings');
test([], \&bad_signer, 0, 'bad signature, no keyrings');
test([$keyring1], undef, 0, 'no signature, keyring 1');
test([$keyring1], \&bad_signer, 0, 'bad signature, keyring 1');
test([$keyring1, $keyring2], \&bad_signer, 0, 'bad signature, keyring 1+2');

test([], get_good_signer($keyring1), 0, 'key 1, no keyrings');
test([], get_good_signer($keyring2), 0, 'key 2, no keyrings');
test([$keyring1], get_good_signer($keyring2), 0, 'key 2, keyring 1');
test([$keyring2], get_good_signer($keyring1), 0, 'key 1, keyring 2');
test([undef, $keyring2], get_good_signer($keyring1), 0, 'key1, keyring 2 (location 2)');

my $key1_signer = get_good_signer($keyring1);
test([$keyring1], $key1_signer, 1, 'key 1, keyring 1');
test([undef, $keyring1], $key1_signer, 1, 'key 1, keyring 1 (location 2)');
test([undef, undef, $keyring1], $key1_signer, 1, 'key 1, keyring 1 (location 3)');
test([$keyring1, $keyring2], $key1_signer, 1, 'key 1, keyring 1+2');
test([undef, $keyring1, $keyring2], $key1_signer, 1, 'key 1, keyring 1+2 (location 2)');
test([$keyring2, $keyring1], $key1_signer, 1, 'key 1, keyring 2+1');
test([$keyring2, undef, $keyring1], $key1_signer, 1, 'key 1, keyring 2+1 (location 2)');

my $key1_inline_signer = get_good_signer($keyring1, 1);
test([], $key1_inline_signer, 0, 'key 1 (inline), no keyrings)');
test([$keyring2], $key1_inline_signer, 0, 'key 1 (inline), keyring 2');
test([undef, $keyring2], $key1_inline_signer, 0, 'key 1 (inline), keyring 2 (location 2)');
test([$keyring1], $key1_inline_signer, 1, 'key 1 (inline), keyring 1');
test([$keyring1, undef, $keyring2], $key1_inline_signer, 1, 'key 1 (inline), keyring 1+2');

