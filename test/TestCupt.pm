package TestCupt;

use strict;
use warnings;

our @EXPORT = qw(
	get_inc_code
	exitcode
	get_extended_states_path
	get_dpkg_path
	get_binary_architecture
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
);
use Exporter qw(import);
use Cwd;
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

	if (cwd() !~ m/\/env$/) {
		mkdir 'env';
		chdir "env" or
				die "cannot change directory to 'env': $!";
	}
	if (cwd() =~ m/\/env$/ && -e 'pre.conf' && -d 'etc' && -d 'var') {
		system("rm -r *") == 0
				or die ("cannot clean the environment");
	}

	setup_fakes();

	my $cwd = cwd();

	$pre_conf =~ s/<dir>/$cwd/g;
	generate_file('pre.conf', $pre_conf);
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

	my $command = $ARGV[0];
	if (defined $options{'packages'} or
		defined $options{'packages2'} or
		defined $options{'dpkg_status'}) {
		$command .= " -o apt::architecture=$architecture";
	}
	return $command;
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

sub generate_packages_sources {
	foreach my $entry (@_) {
		my %e = %$entry;
		my $archive = $e{'archive'} // $default_archive;
		my $codename = $e{'codename'} // $default_codename;
		my $label = $e{'label'} // $default_label;
		my $component = $e{'component'} // $default_component;
		my $version = $e{'version'} // $default_version;
		my $vendor = $e{'vendor'} // $default_vendor;
		my $scheme = $e{'scheme'} // $default_scheme;
		my $server = $e{'hostname'} // $default_server;
		my $not_automatic = $e{'not-automatic'} // 0;
		my $but_automatic_upgrades = $e{'but-automatic-upgrades'} // 0;
		my $valid_until = $e{'valid-until'} // 'Mon, 07 Oct 2033 14:44:53 UTC';
		generate_release($scheme, $server,
				$archive, $codename,
				$component, $vendor, $version, $label,
				$not_automatic, $but_automatic_upgrades, $valid_until);
	
		my $is_trusted = $e{'trusted'}//1;
		my $content = $e{'content'};
		my $list_prefix = get_list_prefix($scheme, $server, $archive);

		my $sources_list_suffix = ($is_trusted ? '[ trusted=yes ] ' : '[ trusted=no ] ');
		$sources_list_suffix .= "$scheme://$server $archive $component";

		if ($e{'type'} eq 'packages') {
			generate_file('etc/apt/sources.list', "deb $sources_list_suffix\n", '>>');
			generate_file("${list_prefix}_${component}_binary-${architecture}_Packages", $content);
			if ($e{'downloads'}) {
				generate_downloads($content);
			}
		} else {
			generate_file('etc/apt/sources.list', "deb-src $sources_list_suffix\n", '>>');
			generate_file("${list_prefix}_${component}_source_Sources", $content);
		}
	}
}

sub get_list_prefix {
	my ($scheme, $server, $archive) = @_;
	$server =~ s{/}{_}g;
	return "var/lib/cupt/lists/${scheme}___${server}_dists_${archive}";
}

sub generate_release {
	my ($scheme, $server, $archive, $codename, $component, $vendor, $version, $label,
			$not_automatic, $but_automatic_upgrades, $valid_until) = @_;

	my $content = <<END;
Origin: $vendor
Version: $version
Label: $label
Suite: $archive
Codename: $codename
Date: Mon, 30 Sep 2013 14:44:53 UTC
Valid-Until: $valid_until
Architectures: $architecture all
Components: $component
END
	if ($not_automatic) {
		$content .= "NotAutomatic: yes\n";
		if ($but_automatic_upgrades) {
			$content .= "ButAutomaticUpgrades: yes\n";
		}
	}
	my $list_prefix = get_list_prefix($scheme, $server, $archive);
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
	return "Package: $package_name\nVersion: $version_string\nArchitecture: all\nSHA1: $sha\n";
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

	my ($result) = ($policy_output =~ m/\Q$version\E.* (-?\d+)/a);

	return ($result // '');
}

1;

