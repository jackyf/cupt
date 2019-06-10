use strict;
use warnings;

my $apt_keyring_dir = 'etc/apt/trusted.gpg.d/';
my $apt_keyring_file = 'etc/apt/trusted.gpg';

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
	my ($files, $signer, $sign_variants, $hooks) = @_;
	$sign_variants //= ['orig','detached'];

	my $cupt = setup(
		'releases' => [
			{
				'packages' => [ compose_package_record('p', 1) ],
				'trusted' => 'check',
				'hooks' => {
					'sign' => {
						'input' => get_variant_filter_hook($sign_variants),
						'convert' => $signer,
						@$hooks,
					},
				},
			}
		]
	);

	mkdir $apt_keyring_dir or die;
	link_keyring($files->[0] => $apt_keyring_file);
	link_keyring($files->[1] => "$apt_keyring_dir/first.gpg");
	link_keyring($files->[2] => "$apt_keyring_dir/second.gpg");

	return stdall("$cupt show 'trusted()'");
}

