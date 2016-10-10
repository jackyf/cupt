package TestCupt;

use strict;
use warnings;

use Cwd;
my $start_dir;

sub enter_test_env {
	if (cwd() !~ m/\/env$/) {
		mkdir 'env';
		chdir "env" or
				die "cannot change directory to 'env': $!";
	}
}

BEGIN {
	$start_dir = cwd();
	enter_test_env();
}

our @EXPORT = qw(
	get_inc_code
	get_rinclude_path
	exitcode
	get_extended_states_path
	get_dpkg_path
	get_binary_architecture
	setup
	stdout
	stdall
	compose_installed_record
	compose_removed_record
	compose_package_record
	compose_autoinstalled_record
	compose_version_pin_record
	compose_pin_record
	entail
	regex_offer
	regex_no_solutions
	get_first_offer
	get_all_offers
	split_offers
	get_offer_count
	get_empty_version
	get_unchanged_version
	get_offered_version
	get_offered_versions
	get_version_priority
	to_one_line
	generate_file
	get_keyring_paths
);
use Exporter qw(import);
use IO::File;
use File::Path qw(make_path);
use File::Basename;
use File::Spec;

sub get_test_dir {
	return $INC[0];
}

sub get_inc_path {
	my ($includee) = @_;
	my $test_dir = get_test_dir();

	my $test_module_dir = (File::Spec->splitpath($0))[1];
	{
		my @parts = File::Spec->splitdir($test_module_dir);
		$parts[0] = 't';
		$test_module_dir = File::Spec->catdir(@parts);
	}

	my $file = "$test_dir/$test_module_dir/$includee.inc";
	return $file;
}

sub get_inc_code {
	my $path = get_inc_path($_[0]);
	return `cat $path`;
}

sub get_rinclude_path {
	my (undef, $from, undef) = caller();
	my ($includee) = @_;
	my $from_dir = (File::Spec->splitpath($from))[1];
	if (! File::Spec->file_name_is_absolute($from_dir)) {
		$from_dir = "$start_dir/$from_dir";
	}
	return "$from_dir/$includee.inc";
}

sub get_extended_states_path {
	return 'var/lib/apt/extended_states';
}

my $dpkg_path = '/bin/true';
sub get_dpkg_path {
	return $dpkg_path;
}

my $architecture = 'z128';
sub get_binary_architecture {
	return $architecture;
}

sub setup {
	generate_environment(@_);
	return generate_binary_command(@_);
}

my $pre_conf = <<END;
apt::architecture "$architecture";
dir "<dir>";
dir::bin::dpkg "$dpkg_path";
dir::state::status "../dpkg/status";
cupt::directory "<dir>";
cupt::console::use-colors "no";
cupt::console::actions-preview::show-summary "no";
cupt::console::actions-preview::show-empty-versions "yes";
cupt::console::show-progress-messages "no";
cupt::resolver::max-leaf-count "1000000";
END

sub generate_environment {
	my %options = @_;

	if (cwd() =~ m/\/env$/ && -e 'pre.conf' && -d 'etc' && -d 'var') {
		system("rm -r *") == 0
				or die ("cannot clean the environment");
	}

	setup_fakes();

	my $cwd = cwd();

	$pre_conf =~ s/<dir>/$cwd/g;
	generate_file('pre.conf', $pre_conf);
	$ENV{'APT_CONFIG'} = '';
	$ENV{'CUPT_PRE_CONFIG'} = "./pre.conf";

	generate_file(get_extended_states_path(), $options{'extended_states'}//'');
	generate_file('var/lib/dpkg/status', $options{'dpkg_status'}//'');
	generate_file('etc/apt/sources.list', '');
	generate_file('etc/apt/preferences', $options{'preferences'}//'');
	generate_file('etc/debdelta/sources.conf', $options{'debdelta_conf'});
	generate_file('usr/bin/debpatch', $options{'debpatch'});
	generate_packages_sources(unify_packages_and_sources_option(\%options));
	generate_file('var/log/cupt.log', '');
	generate_file('var/lib/cupt/lock', '');
}

sub setup_fakes {
	my $test_dir = get_test_dir();
	$ENV{'PATH'} = "$test_dir/fakes:" . $ENV{'PATH'};
}

my $default_archive = 'testing';
my $default_codename = 'jessie';
my $default_label = 'Debian';
my $default_component = 'main';
my $default_version = '18.1';
my $default_vendor = 'Debian';

sub unify_ps_option {
	my ($options, $type) = @_;

	my $result = $options->{"${type}2"}//[];

	my $content_of_default_archive = $options->{$type}//'';
	if ($content_of_default_archive) {
		push @$result, { 'archive' => $default_archive, 'content' => $content_of_default_archive }; 
	}
	foreach my $entry (@$result) {
		$entry->{'type'} = $type;
		$entry->{'downloads'} = ($options->{'downloads'} // 0);
	}

	return @$result;
}

sub unify_packages_and_sources_option {
	my ($options) = @_;

	return (unify_ps_option($options, 'packages'),
			unify_ps_option($options, 'sources'));
}

sub generate_binary_command {
	my %options = @_;
	return $ARGV[0];
}

sub generate_file {
	my ($target_path, $content, $mode) = @_;
	return if not defined $content;

	$mode = $mode // '>';

	make_path(dirname($target_path));

	my $fh = IO::File->new("$mode $target_path");
	defined($fh) or die "cannot open '$target_path' for writing: $!";

	print $fh $content;

	undef $fh;
}

sub generate_downloads {
	my ($packages_content) = @_;

	for my $record (split(/\n\n/, $packages_content)) {
		my ($package) = ($record =~ m/^Package: (.*)$/m);
		my ($version) = ($record =~ m/^Version: (.*)$/m);
		my ($architecture) = ($record =~ m/^Architecture: (.*)$/m);

		generate_file("var/cache/apt/archives/${package}_${version}_${architecture}.deb", '');
	}
}

my $default_scheme = 'file';
my $default_server = '/nonexistent';

sub get_trusted_option_string {
	my $is_trusted = shift;
	$is_trusted //= 1;
	if ($is_trusted eq 'check') {
		return '';
	} else {
		return $is_trusted ? '[ trusted=yes ] ' : '[ trusted=no ] ';
	}
}

sub fill_ps_entry {
	my $e = shift;
	$e->{'archive'} //= $default_archive;
	$e->{'codename'} //= $default_codename;
	$e->{'label'} //= $default_label;
	$e->{'component'} //= $default_component;
	$e->{'version'} //= $default_version;
	$e->{'vendor'} //= $default_vendor;
	$e->{'scheme'} //= $default_scheme;
	$e->{'hostname'} //= $default_server;
	$e->{'server'} = $e->{hostname};
	$e->{'architecture'} //= $architecture;
	$e->{'not-automatic'} //= 0;
	$e->{'but-automatic-upgrades'} //= 0;
	$e->{'valid-until'} //= 'Mon, 07 Oct 2033 14:44:53 UTC';
}

sub generate_packages_sources {
	foreach my $entry (@_) {
		fill_ps_entry($entry);
		my %e = %$entry;

		generate_release($entry);

		my $list_prefix = get_list_prefix($e{scheme}, $e{server}, $e{archive});

		my $sources_list_suffix = get_trusted_option_string($e{trusted});
		$sources_list_suffix .= "$e{scheme}://$e{server} $e{archive} $e{component}";

		if ($e{type} eq 'packages') {
			generate_file('etc/apt/sources.list', "deb $sources_list_suffix\n", '>>');
			generate_file("${list_prefix}_$e{component}_binary-$e{architecture}_Packages", $e{content});
			if ($e{downloads}) {
				generate_downloads($e{content});
			}
		} else {
			generate_file('etc/apt/sources.list', "deb-src $sources_list_suffix\n", '>>');
			generate_file("${list_prefix}_$e{component}_source_Sources", $e{content});
		}
	}
}

sub get_list_prefix {
	my ($scheme, $server, $archive) = @_;
	$server =~ s{/}{_}g;
	return "var/lib/cupt/lists/${scheme}___${server}_dists_${archive}";
}

sub generate_release {
	my $entry = shift;
	my %e = %$entry;

	my $content = <<END;
Origin: $e{vendor}
Version: $e{version}
Label: $e{label}
Suite: $e{archive}
Codename: $e{codename}
Date: Mon, 30 Sep 2013 14:44:53 UTC
Valid-Until: $e{'valid-until'}
Architectures: $e{architecture} all
Components: $e{component}
END
	if ($e{'not-automatic'}) {
		$content .= "NotAutomatic: yes\n";
		if ($e{'but-automatic-upgrades'}) {
			$content .= "ButAutomaticUpgrades: yes\n";
		}
	}
	my $list_prefix = get_list_prefix($e{scheme}, $e{server}, $e{archive});
	my $path = "${list_prefix}_Release";
	generate_file($path, $content);

	if (defined $e{signer}) {
		my ($is_inline, $signature) = $e{signer}->($path);
		if ($is_inline) {
			generate_file("${list_prefix}_InRelease", $signature);
			unlink($path);
		} else {
			generate_file("$path.gpg", $signature);
		}
	}
}

# helpers

sub exitcode {
	return system($_[0] . " >/dev/null 2>&1");
}

sub stdout {
	my $command = $_[0];
	return `$command 2>/dev/null`;
}

sub stdall {
	my ($command) = @_;
	return `$command 2>&1`;
}

sub compose_status_record {
	my ($package_name, $status, $version_string) = @_;

	return "Package: $package_name\nStatus: $status\nVersion: $version_string\nArchitecture: all\n";
}

sub compose_installed_record {
	my ($package_name, $version_string, %options) = @_;

	my $want = ($options{'on-hold'}//0) ? 'hold' : 'install';

	return compose_status_record($package_name, "$want ok installed", $version_string);
}

sub compose_removed_record {
	my ($package_name) = @_;

	return compose_status_record($package_name, 'deinstall ok config-files' , 0);
}

sub compose_package_record {
	my ($package_name, $version_string, %options) = @_;

	my $sha1_of_empty_file = 'da39a3ee5e6b4b0d3255bfef95601890afd80709';
	my $sha = ($options{'sha'} // $sha1_of_empty_file);

	my $arch = ($options{'architecture'} // 'all');

	return "Package: $package_name\nVersion: $version_string\nArchitecture: $arch\nSHA1: $sha\n";
}

sub compose_autoinstalled_record {
	my ($package_name) = @_;

	return "Package: $package_name\nAuto-Installed: 1\n";
}

sub compose_pin_record {
	my ($first_line, $pin_line, $priority) = @_;
	return "$first_line\nPin: $pin_line\nPin-Priority: $priority\n\n";
}

sub compose_version_pin_record {
	my ($package_name, $version_string, $priority) = @_;
	return compose_pin_record("Package: $package_name", "version $version_string", $priority);
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

sub split_offers {
	my @result = split(regex_offer(), $_[0]);
	pop @result; # last one will be 'no solutions anymore'
	return @result;
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

my $offer_version_regex = qr/(?:\(a\))? \[.*? -> (.*?)\]/;

sub get_offered_versions {
	my ($offer) = @_;

	my %pairs = ($offer =~ m/^(.*?)$offer_version_regex/mg);
	return \%pairs;
}

sub get_offered_version {
	my ($offer, $package_name) = @_;

	my ($version) = ($offer =~ m/^$package_name$offer_version_regex/m);

	return $version // get_unchanged_version();
}

sub get_version_priority {
	my ($policy_output, $version) = @_;

	my ($result) = ($policy_output =~ m/ \Q$version\E.* (-?\d+)$/ma);

	return ($result // '');
}

sub to_one_line {
	my $t = shift;
	$t =~ s/\n/{newline}/g;
	return $t;
}

sub get_keyring_paths {
	my $dir = get_test_dir() . '/gpg';
	return ("$dir/mock1k.gpg", "$dir/mock4k.gpg");
}

1;

