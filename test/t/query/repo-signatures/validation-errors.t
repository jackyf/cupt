use Test::More tests => 3;

require(get_rinclude_path('common'));

my ($good_keyring, $other_good_keyring) = get_keyring_paths();

sub test {
	my ($input, $expected_error) = @_;

	my $output = get_output(@$input);
	subtest $expected_error => sub {
		unlike($output, qr/Package/, 'validation failed');
		like($output, qr/$expected_error/m, 'error message is right');
	}
}

test([[], get_good_signer($good_keyring)] => 'unable to read keyring file');

test([[$other_good_keyring], get_good_signer($good_keyring)] => "public key '.*' is not found");

my $expired_sig_options = '--faked-system-time 20161020T154812 --default-sig-expire 2016-10-22';
test([[$good_keyring], get_good_signer($good_keyring, $expired_sig_options)] => "expired signature");

