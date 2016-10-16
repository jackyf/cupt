package TestCupt;

use strict;
use warnings;

use Digest::SHA qw(sha1_hex sha256_hex);
use Cwd;
use IPC::Run3;

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
	update_remote_releases
	stdout
	stdall
	compose_installed_record
	compose_removed_record
	compose_package_record
	compose_autoinstalled_record
	compose_version_pin_record
	compose_pin_record
	compose_translation_record
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
	get_good_signer
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

	make_path('var/log');
	make_path('var/lib/cupt');

	generate_file(get_extended_states_path(), $options{'extended_states'}//'');
	generate_file('var/lib/dpkg/status', $options{'dpkg_status'}//'');
	generate_file('etc/apt/sources.list', '');
	generate_file('etc/apt/preferences', $options{'preferences'}//'');
	generate_file('etc/debdelta/sources.conf', $options{'debdelta_conf'});
	generate_file('usr/bin/debpatch', $options{'debpatch'});

	my @releases = unify_releases(\%options);
	generate_sources_list(@releases);
	generate_packages_sources(@releases);
}

sub update_remote_releases {
	foreach (@_) {
		die if $_->{location} ne 'remote';
		fill_ps_entry($_);
	}
	generate_packages_sources(@_);
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

sub convert_v1_to_releases {
	my ($options, $type) = @_;

	my $content_of_default_archive = $options->{$type}//'';
	if ($content_of_default_archive) {
		return { $type => $content_of_default_archive };
	} else {
		return ();
	}
}

sub convert_v2_to_releases {
	my ($options, $type) = @_;

	my $entries = $options->{"${type}2"};
	foreach my $entry (@$entries) {
		$entry->{$type} = $entry->{content};
		delete $entry->{content};
	}
	return @$entries;
}

sub convert_older_option_structures_to_releases {
	my ($options, $type) = @_;
	return (convert_v1_to_releases($options, $type),
			convert_v2_to_releases($options, $type));
}

sub unify_packages_and_sources_option {
	my ($options) = @_;

	return (@{$options->{"releases"}//[]},
			convert_older_option_structures_to_releases($options, 'packages'),
			convert_older_option_structures_to_releases($options, 'sources'));
}

sub unify_releases {
	my @result = unify_packages_and_sources_option(@_);
	foreach my $entry (@result) {
		fill_ps_entry($entry);
	}
	return @result;
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

sub generate_deb_caches {
	my ($packages_content) = @_;

	for my $record (split(/\n\n/, $packages_content)) {
		my ($package) = ($record =~ m/^Package: (.*)$/m);
		my ($version) = ($record =~ m/^Version: (.*)$/m);
		my ($architecture) = ($record =~ m/^Architecture: (.*)$/m);

		generate_file("var/cache/apt/archives/${package}_${version}_${architecture}.deb", '');
	}
}

my $default_scheme = 'copy';
my $default_server = './localrepo';

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
	$e->{'location'} //= 'local';
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

	$e->{'variants'}->{'sign'} //= ['orig'];
	$e->{'variants'}->{'compress'} //= ['orig'];
	my @keyrings = get_keyring_paths();
	$e->{'hooks'}->{'signer'} //= get_good_signer($keyrings[0]);
	$e->{'hooks'}->{'file'} //= sub { return $_[3]; };
	$e->{'callback'} = $e->{location} eq 'remote' ? \&remote_ps_callback : \&local_ps_callback;
}

sub get_content_sums {
	my $content = shift;
	return {
		'SHA1' => sha1_hex($content),
		'SHA256' => sha256_hex($content),
	}
}

sub compose_sums_record {
	my %qqq;
	my $records = shift;
	foreach my $record (@$records) {
		while (my ($name, $value) = each %{$record->{sums}}) {
			$qqq{$name} //= "$name:\n";
			$qqq{$name} .= " $value $record->{size} $record->{path}\n";
		}
	}
	return join('', values %qqq);
}

sub local_ps_callback {
	my ($kind, $entry, undef) = @_;
	my %e = %$entry;

	my $list_prefix = get_list_prefix($e{scheme}, $e{server}, $e{archive});

	if ($kind eq 'Packages') {
		return "${list_prefix}_$e{component}_binary-$e{architecture}_Packages";
	} elsif ($kind eq 'Sources') {
		return "${list_prefix}_$e{component}_source_Sources";
	} elsif ($kind =~ m/Release/) {
		return "${list_prefix}_$kind";
	} elsif ($kind =~ m/Translation/) {
		return "${list_prefix}_$e{component}_i18n_$kind";
	} else {
		die "wrong kind $kind";
	}
}

sub remote_ps_callback {
	my ($kind, $entry, $content) = @_;
	my %e = %$entry;

	my $subpath;
	if ($kind =~ m/Release/) {
		$subpath = $kind;
	} elsif ($kind =~ m/Packages/) {
		$subpath = "$e{component}/binary-$e{architecture}/$kind";
	} elsif ($kind eq 'Sources') {
		$subpath = "$e{component}/source/$kind";
	} elsif ($kind =~ m/Translation/) {
		$subpath = "$e{component}/i18n/$kind";
	} else {
		die "wrong kind $kind";
	}

	if (defined $content) {
		push @{$entry->{_ps_files}}, {
			'path' => $subpath,
			'size' => length($content),
			'sums' => get_content_sums($content),
		};
	}

	return "$e{hostname}/dists/$e{archive}/$subpath";
}

sub join_records_if_needed {
	my $input = shift;
	if (ref($input) eq 'ARRAY') {
		return join("\n", @$input);
	} else {
		return $input;
	}
}

sub call_file_callback_with_hooks {
	my ($kind, $entry, $pre_content) = @_;
	my $hook = $entry->{hooks}->{file};

	my $main_content = $hook->('pre', $kind, $entry, $pre_content);
	my $path = $entry->{callback}->($kind, $entry, $main_content);
	my $post_content = $hook->('post', $kind, $entry, $main_content);
	generate_file($path, $post_content);
	return ($path, $main_content);
}

sub generate_file_with_variants {
	my ($kind, $entry, $pre_content) = @_;
	my $variants = $entry->{variants}->{compress};

	my ($path, $content) = call_file_callback_with_hooks($kind, $entry, $pre_content);
	if (not in_array('orig', $variants)) {
		unlink($path);
	}
	my $compress_via = sub {
		my ($variant, $compressor) = @_;
		if (in_array($variant, $variants)) {
			run3("$compressor -c", \$content, \my $compressed, \my $stderr);
			die "$compressor: $stderr" if $?;
			call_file_callback_with_hooks("$kind.$variant", $entry, $compressed);
		}
	};
	$compress_via->('xz', 'xz');
	$compress_via->('gz', 'gzip');
	$compress_via->('bz2', 'bzip2');
}

sub generate_sources_list {
	my $result = '';
	foreach my $e (@_) {
		my $sources_list_suffix = get_trusted_option_string($e->{trusted});
		$sources_list_suffix .= "$e->{scheme}://$e->{server} $e->{archive} $e->{component}";

		if (defined $e->{packages}) {
			$result .= "deb $sources_list_suffix\n";
		}
		if (defined $e->{sources}) {
			$result .= "deb-src $sources_list_suffix\n";
		}
	}
	generate_file('etc/apt/sources.list', $result);
}

sub generate_packages_sources {
	foreach my $entry (@_) {
		my %e = %$entry;

		if (defined $e{packages}) {
			my $content = join_records_if_needed($e{packages});
			generate_file_with_variants('Packages', $entry, $content);
			if ($e{'deb-caches'}) {
				generate_deb_caches($content);
			}
		}

		if (defined $e{sources}) {
			generate_file_with_variants('Sources', $entry, join_records_if_needed($e{sources}));
		}

		while (my ($lang, $content) = each %{$e{translations}}) {
			generate_file_with_variants("Translation-$lang", $entry, join_records_if_needed($content));
		}

		generate_release($entry);
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

	my $pre_content = <<END;
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
		$pre_content .= "NotAutomatic: yes\n";
		if ($e{'but-automatic-upgrades'}) {
			$pre_content .= "ButAutomaticUpgrades: yes\n";
		}
	}
	$pre_content .= compose_sums_record($e{_ps_files});

	my ($path, $content) = call_file_callback_with_hooks('Release', $entry, $pre_content);

	my $variants = $e{variants}{sign};
	if (not in_array('orig', $variants)) {
		unlink($path);
	}
	my $signer = $e{hooks}{signer};
	if (in_array('inline', $variants)) {
		call_file_callback_with_hooks('InRelease', $entry, $signer->(1, $content));
	}
	if (in_array('detached', $variants)) {
		call_file_callback_with_hooks('Release.gpg', $entry, $signer->(0, $content));
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

sub compose_translation_record {
	my ($package, $lang, $hash, $desc) = @_;
	my $result = '';
	if (defined($package)) {
		$result .= "Package: $package\n";
	}
	$result .= "Description-md5: $hash\n";
	$result .= "Description-$lang: $desc\n";
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

sub get_good_signer {
	my $keyring = shift;
	return sub {
		my ($is_inline, $input) = @_;
		my $command = ($is_inline ? '--sign' : '--detach-sign');
		run3("gpg2 --no-default-keyring --keyring $keyring --output - --armor $command", \$input, \my $output);
		return $output;
	};
}

sub in_array {
	my ($elem, $array) = @_;
	return grep { $_ eq $elem } @$array;
}

1;

