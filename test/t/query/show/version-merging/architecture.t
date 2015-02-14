use TestCupt;
use Test::More tests => 6;

use strict;
use warnings;


my $package = 'abc';
my $version = '1.2.3';

my $record = <<END;
Package: $package
Version: $version
Status: install ok installed
SHA1: 6713281
Architecture: %arch%
END


sub compose_record_with_arch {
	my $arch = shift;
	return ($record =~ s/%arch%/$arch/r);
}

sub setup_cupt {
	my ($is_installed, $arch) = @_;
	return TestCupt::setup(
		($is_installed ? 'dpkg_status' : 'packages') =>
			compose_record_with_arch($arch)
	);
}

sub test {
	my ($is_installed, $arch, $expected_result) = @_;

	my $cupt = setup_cupt($is_installed, $arch);

	my $output = stdall("$cupt show $package");

	my $comment_prefix = "(installed: $is_installed): arch $arch";
	if ($expected_result) {
		like($output, qr/^\QVersion: $version\E/m, "$comment_prefix: merged");
	} else {
		like($output, qr/^E:.* selected nothing/m, "$comment_prefix: not merged");
	}
}

my $binary_arch = get_binary_architecture();

sub test_group {
	my ($is_installed) = @_;

	test($is_installed, $binary_arch => 1);
	test($is_installed, 'dubidu' => $is_installed);
	test($is_installed, 'all' => 1);
}

test_group(1);
test_group(0);

