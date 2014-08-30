package TestCupt;

use strict;
use warnings;

our @EXPORT = qw(
	get_inc_code
	exitcode
	stdout
	compose_installed_record
	compose_package_record
	compose_autoinstalled_record
	entail
	regex_offer
	regex_no_solutions
	get_first_offer
	get_all_offers
	get_offer_count
	get_empty_version
	get_unchanged_version
	get_offered_version
);
use Exporter qw(import);
use Cwd;
use IO::File;
use File::Path qw(make_path);
use File::Basename;
use File::Spec;

sub get_inc_path {
	my ($includee) = @_;
	my $test_dir = $INC[0];
	my $test_module_dir = (File::Spec->splitpath($0))[1];
	my $file = "$test_dir/$test_module_dir/$includee.inc";
	return $file;
}

sub get_inc_code {
	my $path = get_inc_path($_[0]);
	return `cat $path`;
}

sub setup {
	generate_environment(@_);
	return generate_binary_command(@_);
}

my $pre_conf = <<END;
dir "<dir>";
dir::state::status "../dpkg/status";
cupt::directory "<dir>";
cupt::console::use-colors "no";
cupt::console::actions-preview::show-summary "no";
cupt::console::actions-preview::show-empty-versions "yes";
cupt::console::show-progress-messages "no";
END

sub generate_environment {
	my %options = @_;

	if (cwd() !~ m/env$/) {
		mkdir 'env';
		chdir "env" or
				die "cannot change directory to 'env': $!";
	}
	my $cwd = cwd();

	$pre_conf =~ s/<dir>/$cwd/g;
	generate_file('pre.conf', $pre_conf);
	$ENV{'CUPT_PRE_CONFIG'} = "./pre.conf";

	generate_file('var/lib/apt/extended_states', $options{'extended_states'}//'');
	generate_file('var/lib/dpkg/status', $options{'dpkg_status'}//'');
	generate_file('etc/apt/sources.list', '');
	generate_file('etc/apt/preferences', $options{'preferences'}//'');
	generate_packages_sources($options{'packages'}//'', $options{'sources'}//'');
}

my $architecture = 'z128';

sub generate_binary_command {
	my %options = @_;

	my $command = $ARGV[0];
	if (defined $options{'packages'} or defined $options{'dpkg_status'}) {
		$command .= " -o apt::architecture=$architecture";
		$command .= " -o cupt::console::allow-untrusted=yes";
	}
	return $command;
}

sub generate_file {
	my ($target_path, $content, $mode) = @_;
	$mode = $mode // '>';

	make_path(dirname($target_path));

	my $fh = IO::File->new("$mode $target_path");
	defined($fh) or die "cannot open '$target_path' for writing: $!";

	print $fh $content;

	undef $fh;
}

my $scheme = 'file';
my $server = 'nonexistent';
my $archive = 'testing';
my $component = 'main';
my $list_prefix = "var/lib/cupt/lists/${scheme}____${server}_dists_${archive}";

sub generate_packages_sources {
	my ($packages_content, $sources_content) = @_;

	($packages_content ne '' or $sources_content ne '') or return;

	generate_release();
	
	my $sources_list_suffix = "$scheme:///$server $archive $component";

	if ($packages_content ne '') {
		generate_file('etc/apt/sources.list', "deb $sources_list_suffix\n", '>>');
		generate_file("${list_prefix}_${component}_binary-${architecture}_Packages", $packages_content);
	}

	if ($sources_content ne '') {
		generate_file('etc/apt/sources.list', "deb-src $sources_list_suffix\n", '>>');
		generate_file("${list_prefix}_${component}_source_Sources", $sources_content);
	}
}

sub generate_release {
	my $content = <<END;
Origin: Debian
Label: Debian
Suite: $archive
Codename: jessie
Date: Mon, 30 Sep 2013 14:44:53 UTC
Valid-Until: Mon, 07 Oct 2033 14:44:53 UTC
Architectures: $architecture all
Components: $component
END
	generate_file("${list_prefix}_Release", $content);
}

# helpers

sub exitcode {
	return system($_[0] . " >/dev/null 2>&1");
}

sub stdout {
	my $command = $_[0];
	return `$command 2>/dev/null`;
}

sub compose_installed_record {
	my ($package_name, $version_string) = @_;

	return "Package: $package_name\nStatus: install ok installed\nVersion: $version_string\nArchitecture: all\n";
}

sub compose_package_record {
	my ($package_name, $version_string) = @_;

	return "Package: $package_name\nVersion: $version_string\nArchitecture: all\nSHA1: abcdef\n";
}

sub compose_autoinstalled_record {
	my ($package_name) = @_;

	return "Package: $package_name\nAuto-Installed: 1\n";
}

sub entail {
	return $_[0] . "\n";
}

sub regex_offer {
	return qr/(?:Do you want to continue?|Nothing to do.)/;
}

sub regex_no_solutions {
	return qr/no solutions/;
}

sub get_first_offer {
	my ($command) = @_;
	return `echo 'q' | $command -s 2>&1`;
}

sub get_all_offers {
	my ($command) = @_;
	return `yes 'N' | $command -s 2>&1`;
}

sub get_offer_count {
	my ($input) = @_;

	my $r = regex_offer();
	my @offer_count = ($input =~ /$r/g);

	return scalar @offer_count;
}

sub get_unchanged_version {
	return "<unchanged>";
}

sub get_empty_version {
	return "<empty>";
}

sub get_offered_version {
	my ($offer, $package_name) = @_;

	my ($version) = ($offer =~ m/^$package_name \[.*? -> (.*?)\]/m);

	if (!defined($version) and ($offer =~ m/^$package_name/m)) {
		return "<no version info>";
	}

	return $version // get_unchanged_version();
}

1;

