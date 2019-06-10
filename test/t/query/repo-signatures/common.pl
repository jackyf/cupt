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
	my %params = %{shift()};

	my $keyrings = $params{'keyrings'}//[];
	my $signer = $params{'signer'};
	my $sign_variants = $params{'sign-variants'}//['orig','detached'];
	my $hooks = $params{'hooks'}//[];
	my $trusted = $params{'trusted'}//'check';

	my $cupt = setup(
		'releases' => [
			{
				'packages' => [ compose_package_record('p', 1) ],
				'trusted' => $trusted,
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
	link_keyring($keyrings->[0] => $apt_keyring_file);
	link_keyring($keyrings->[1] => "$apt_keyring_dir/first.gpg");
	link_keyring($keyrings->[2] => "$apt_keyring_dir/second.gpg");

	return stdall("$cupt show 'trusted()'");
}

