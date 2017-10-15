use Test::More tests => 9;

require(get_rinclude_path('common'));

my $good_keyring = get_keyring_path('good-1');
my $other_good_keyring = get_keyring_path('good-2');
my $expired_keyring = get_keyring_path('expired');
my $good_keyring_revoked = get_keyring_path('revoke-for-good-1');

sub test {
	my ($input, $expected_error) = @_;

	my $output = get_output(@$input);
	subtest $expected_error => sub {
		unlike($output, qr/Package/, 'validation failed');
		like($output, qr/$expected_error/m, 'error message is right');
	}
}

my $key_not_found_message = "public key '.*' is not found";
test([[], get_good_signer($good_keyring)] => $key_not_found_message);
test([[$other_good_keyring], get_good_signer($good_keyring)] => $key_not_found_message);

my $expired_sig_options = '--faked-system-time 20161020T154812 --default-sig-expire 2016-10-22';
test([[$good_keyring], get_good_signer($good_keyring, $expired_sig_options)] => "expired signature");

my $expired_key_options = '--faked-system-time 20150220T154812';
test([[$expired_keyring], get_good_signer($expired_keyring, $expired_key_options)] => "expired key");

test([[$good_keyring_revoked], get_good_signer($good_keyring)] => 'revoked key');

test([[$good_keyring], \&bad_signer] => 'empty signature');

sub other_input_hook {
	my ($variant, undef, undef, $content) = @_;
	if ($variant eq 'orig') {
		return $content;
	} elsif ($variant eq 'detached') {
		$content =~ s/.//;
		return $content;
	} else {
		return undef;
	}
}
test([[$good_keyring], get_good_signer($good_keyring), undef, ['input' => \&other_input_hook ]] => 'bad signature');

sub remove_read_permission {
	my ($variant, undef, undef, $path) = @_;
	if ($variant eq 'detached') {
		chmod(0220, $path);
	}
}
test([[$good_keyring], get_good_signer($good_keyring), ['orig', 'detached'], ['file' => \&remove_read_permission ]] => 'empty signature');

sub unknown_signature_hash_algorighm {
	my ($variant, undef, undef, $content) = @_;
	return $content unless $variant eq 'inline';
	$content =~ s/^Hash: .*/Hash: SHA377/m;
	return $content;
}
test([[$good_keyring], get_good_signer($good_keyring), ['inline'], ['seal' => \&unknown_signature_hash_algorighm]] =>
		'could not verify a signature');
