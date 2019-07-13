package TestCupt;

use strict;
use warnings;

use Digest::SHA qw(sha1_hex sha256_hex);
use Cwd;
use IPC::Run3;
use List::MoreUtils qw(part each_array);
# use MIME::QuotedPrint;

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
	compose_status_record
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
	get_keyring_path
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

sub get_rinclude_path {
	my (undef, $from, undef) = caller();
	my ($includee) = @_;
	my $from_dir = (File::Spec->splitpath($from))[1];
	if (! File::Spec->file_name_is_absolute($from_dir)) {
		$from_dir = "$start_dir/$from_dir";
	}
	return "$from_dir/$includee.pl";
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
cupt::console::actions-preview::show-versions "yes";
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

	generate_file(get_extended_states_path(), join_records_if_needed($options{'extended_states'}//''));
	generate_file('var/lib/dpkg/status', join_records_if_needed($options{'dpkg_status'}//''));
	generate_file('etc/apt/sources.list', '');
	generate_file('etc/apt/preferences', $options{'preferences'}//'');
	generate_file('etc/debdelta/sources.conf', $options{'debdelta_conf'});
	generate_file('usr/bin/debpatch', $options{'debpatch'});

	my @releases = unify_releases(unify_packages_and_sources_option(\%options));
	generate_sources_list(@releases);
	generate_packages_sources(@releases);
}

sub update_remote_releases {
	my @releases = unify_releases(@_);
	foreach (@releases) {
		die if $_->{location} ne 'remote';
	}
	generate_packages_sources(@releases);
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

sub convert_older_option_structures_to_releases {
	my ($options, $type) = @_;
	return convert_v1_to_releases($options, $type);
}

sub unify_packages_and_sources_option {
	my ($options) = @_;

	return (@{$options->{"releases"}//[]},
			convert_older_option_structures_to_releases($options, 'packages'),
			convert_older_option_structures_to_releases($options, 'sources'));
}

sub unify_releases {
	my $component_unifier = sub {
		return $_ if defined $_->{components};
		my $the_component = {};
		foreach my $key (qw(component packages sources translations previous deb-caches)) {
			$the_component->{$key} = $_->{$key};
			delete $_->{$key};
		}
		$_->{components} = [];
		push @{$_->{components}}, $the_component;
		return $_;
	};
	my @result = map(&$component_unifier, @_);
	foreach my $release (@result) {
		fill_release($release);
	}
	return @result;
}

sub generate_binary_command {
	my %options = @_;
	return $ARGV[0];
}

sub generate_file {
	my ($target_path, $content) = @_;
	return if not defined $content;

	make_path(dirname($target_path));

	my $fh = IO::File->new($target_path, "w");
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

sub get_sources_list_option_string_predefined {
	my $e = shift;

	my @parts;

	my $is_trusted = $e->{'trusted'} // 1;
	if ($is_trusted ne 'check') {
		push @parts, ($is_trusted ? 'trusted=yes' : 'trusted=no');
	}

	my $check_valid_until = $e->{'check-valid-until'};
	if (defined($check_valid_until)) {
		push @parts, "check-valid-until=$check_valid_until";
	}

	if (scalar @parts) {
		return "[ " . join(' ', @parts) . " ]";
	} else {
		return '';
	}
}

sub get_sources_list_option_string {
	my $e = shift;

	my $result = get_sources_list_option_string_predefined($e);
	if (defined $e->{'options-hook'}) {
		$result = $e->{'options-hook'}->($result);
	}
	$result =~ s/ ?$/ /;

	return $result;
}

sub fill_hook {
	my ($e, $key, $converter) = @_;
	my $pass = sub { return $_[3]; };
	my $orig_only = sub { return ($_[0] eq 'orig' ? $_[3] : undef); };

	$e->{hooks}->{$key} //= {};
	my $h = $e->{hooks}->{$key};
	$h->{input} //= $orig_only;
	$h->{convert} //= $converter;
	$h->{seal} //= $pass;
	$h->{write} //= $pass;
	$h->{file} //= $pass;
}

sub get_compressor_by_variant {
	my $ext = shift;
	my $map = { gz => 'gzip', 'bz2' => 'bzip2', xz => 'xz' };
	return $map->{$ext};
}

sub get_path_extension {
	my $input = shift;
	my ($extension) = ($input =~ m/.*\.(.*)$/);
	return $extension;
}

sub get_ed_diff {
	my ($from, $to) = @_;
	my $from_fh = File::Temp->new();
	print $from_fh $from;
	my $to_fh = File::Temp->new();
	print $to_fh $to;
	return `diff --ed $from_fh $to_fh`;
}

sub fill_hooks {
	my $e = shift;

	fill_hook($e, 'sign', get_good_signer(get_keyring_path('good-1')));

	fill_hook($e, 'compress', sub {
		my ($variant, undef, undef, $content) = @_;
		my $compressor = get_compressor_by_variant($variant);
		die $variant unless defined $compressor;
		run3("$compressor -c", \$content, \my $compressed, \my $stderr);
		die "$compressor: $stderr" if $?;
		return $compressed;
	});

	fill_hook($e, 'diff', sub {
		my (undef, $kind, $entry, $content) = @_;
		if ($kind =~ m!(.*)/Index$!) {
			return compose_diff_index($1, $entry, $content);
		} else {
			my ($from, $to) = @$content;
			if ($kind !~ m/z$/) { # not compressed
				push @{$entry->{release}{_diff_history}}, {
					'path' => $kind,
					'size' => length($from),
					'sums' => get_content_sums($from),
				};
			}
			return get_ed_diff($from, $to);
		}
	});
}

sub fill_release {
	my $e = shift;
	$e->{'location'} //= 'local';
	$e->{'archive'} //= $default_archive;
	$e->{'codename'} //= $default_codename;
	$e->{'label'} //= $default_label;
	foreach (@{$e->{'components'}}) {
		$_->{'component'} //= $default_component;
	}
	$e->{'version'} //= $default_version;
	$e->{'vendor'} //= $default_vendor;
	$e->{'scheme'} //= $default_scheme;
	$e->{'hostname'} //= $default_server;
	$e->{'server'} = $e->{hostname};
	$e->{'architecture'} //= $architecture;
	$e->{'not-automatic'} //= 0;
	$e->{'but-automatic-upgrades'} //= 0;
	$e->{'valid-until'} //= 'Mon, 07 Oct 2033 14:44:53 UTC';
	$e->{'callback'} = $e->{location} eq 'remote' ? \&remote_ps_callback : \&local_ps_callback;
	fill_hooks($e);
}

sub get_content_sums {
	my $content = shift;
	return {
		'SHA1' => sha1_hex($content),
		'SHA256' => sha256_hex($content),
	}
}

sub compose_sums_record {
	my ($records, $header_suffix) = @_;
	$header_suffix //= '';

	my %qqq;
	foreach my $record (@$records) {
		while (my ($name, $value) = each %{$record->{sums}}) {
			$qqq{$name} //= "$name$header_suffix:\n";
			$qqq{$name} .= " $value $record->{size} $record->{path}\n";
		}
	}
	return join('', values %qqq);
}

sub local_ps_callback {
	my ($kind, $entry, undef) = @_;
	my %e = %{$entry->{release}};
	my $component = $entry->{component};

	my $list_prefix = get_list_prefix($e{scheme}, $e{server}, $e{archive});

	if ($kind eq 'Packages') {
		return "${list_prefix}_${component}_binary-$e{architecture}_Packages";
	} elsif ($kind eq 'Sources') {
		return "${list_prefix}_${component}_source_Sources";
	} elsif ($kind =~ m/Release/) {
		return "${list_prefix}_$kind";
	} elsif ($kind =~ m/Translation/) {
		return "${list_prefix}_${component}_i18n_$kind";
	} else {
		die "wrong kind $kind";
	}
}

sub remote_ps_callback {
	my ($kind, $entry, $content) = @_;
	my %e = %{$entry->{release}};
	my $component = $entry->{component};

	my $subpath;
	if ($kind =~ m/Release/) {
		$subpath = $kind;
	} elsif ($kind =~ m/Packages/) {
		$subpath = "$component/binary-$e{architecture}/$kind";
	} elsif ($kind eq 'Sources') {
		$subpath = "$component/source/$kind";
	} elsif ($kind =~ m/Translation/) {
		$subpath = "$component/i18n/$kind";
	} else {
		die "wrong kind $kind";
	}

	if (defined $content) {
		my $section = ($kind =~ /diff/ and $kind !~ m/Index$/) ? '_diff_files' : '_ps_files';
		push @{$entry->{release}{$section}}, {
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
		return join('', map { "$_\n" } @$input);
	} else {
		return $input;
	}
}

sub call_hooks {
	my ($kind, $entry, $pre_input, @apply) = @_;
	my $release = $entry->{release};

	my $wrap_hook = sub {
		my ($group, $name, $variant) = @_;
		my $hook = $release->{hooks}->{$group}->{$name};
		die "no hook: $group, $name" unless defined $hook;
		return sub {
			my ($in) = @_;
			my $out = $hook->($variant, $kind, $entry, $in);
			# my $printable = defined($out) ? encode_qp(to_one_line(substr($out, 0, 90)), '') : "<undef>";
			# print "--- Hook: $kind -> $variant:$group/$name -> $printable\n";
			return $out;
		};
	};

	my $path;
	my $path_hook = sub {
		my $content = shift;
		$path = $release->{callback}->($kind, $entry, $content);
		return $content;
	};

	my @process_queue;
	my @write_queue;
	foreach (@apply) {
		my ($group, $variant) = @$_;
		push @process_queue, $wrap_hook->($group, 'input', $variant);
		if ($variant ne 'orig') {
			push @process_queue, $wrap_hook->($group, 'convert', $variant);
		}
		push @process_queue, $wrap_hook->($group, 'seal', $variant);
		push @write_queue, $wrap_hook->($group, 'write', $variant);
	}

	my $content = $pre_input;
	foreach my $hook (@process_queue, $path_hook, @write_queue) {
		$content = $hook->($content);
		return unless defined $content;
	}

	generate_file($path, $content);

	my ($last_group, $last_variant) = @{$apply[-1]};
	$wrap_hook->($last_group, 'file', $last_variant)->($path);
}

sub zip_adjacent {
	my @shifted = @_;
	shift @shifted;
	pop @_;

	return each_array(@_, @shifted);
}

sub compose_diff_index {
	my ($kind, $entry, $target_content) = @_;
	my $release = $entry->{release};

	my $filter_diff_kind = sub {
		return () unless ($_->{path} =~ m!\Q$kind/\E(.*)!);
		$_->{path} = $1;
		return $_;
	};

	my $get_diff_records = sub {
		my $files = shift;
		my @result = map(&$filter_diff_kind, @$files);
		return \@result;
	};

	my $result = sprintf("SHA1-Current: %s %s\n", sha1_hex($target_content), length($target_content));

	$result .= compose_sums_record($get_diff_records->($release->{_diff_history}), '-History');

	my $patch_records = $get_diff_records->($release->{_diff_files});
	my ($orig_patch_records, $compressed_patch_records) =
			part { defined(get_path_extension($_->{path})) ? 1 : 0 } @$patch_records;
	$result .= compose_sums_record($orig_patch_records, '-Patches');
	$result .= compose_sums_record($compressed_patch_records, '-Download');

	return $result;
}

sub generate_file_with_variants {
	my ($kind, $entry, $pre_input, $previous) = @_;

	call_hooks($kind, $entry, $pre_input, [qw(diff orig)], [qw(compress orig)]);
	call_hooks("$kind.gz", $entry, $pre_input, [qw(diff orig)], [qw(compress gz)]);
	call_hooks("$kind.bz2", $entry, $pre_input, [qw(diff orig)], [qw(compress bz2)]);
	call_hooks("$kind.xz", $entry, $pre_input, [qw(diff orig)], [qw(compress xz)]);

	if (defined $previous) {
		my @keys = sort keys %$previous;
		my $adjacent_it = zip_adjacent(@keys);
		my $last_content;
		while (my ($from_key, $to_key) = $adjacent_it->()) {
			my $diffkind = "$kind.diff/$to_key";
			my $from_content = join_records_if_needed($previous->{$from_key});
			my $to_content = join_records_if_needed($previous->{$to_key});
			my $diff_input = [$from_content,$to_content];
			call_hooks($diffkind, $entry, $diff_input, [qw(diff diff)], [qw(compress orig)]);
			call_hooks("$diffkind.gz", $entry, $diff_input, [qw(diff diff)], [qw(compress gz)]);
			$last_content = $to_content;
		}
		call_hooks("$kind.diff/Index", $entry, $last_content, [qw(diff diff)], [qw(compress orig)]);
	}
}

sub generate_sources_list {
	my $result = '';
	foreach my $e (@_) {
		my $get_components_for = sub {
			my $key = shift;
			return map { defined($_->{$key}) ? ($_->{component}) : () } @{$e->{components}}; 
		};

		my $common_line = get_sources_list_option_string($e);
		$common_line .= "$e->{scheme}://$e->{server} $e->{archive}";

		my @components = $get_components_for->('packages');
		if (@components) {
			$result .= "deb $common_line @components\n";
		}

		@components = $get_components_for->('sources');
		if (@components) {
			$result .= "deb-src $common_line @components\n";
		}
	}
	generate_file('etc/apt/sources.list', $result);
}

sub generate_packages_sources {
	foreach my $release (@_) {
		foreach my $c (@{$release->{components}}) {
			my $entry = { 'release' => $release, 'component' => $c->{component} };

			if (defined $c->{packages}) {
				my $content = join_records_if_needed($c->{packages});
				generate_file_with_variants('Packages', $entry, $content, $c->{previous}{packages});
				if ($c->{'deb-caches'}) {
					generate_deb_caches($content);
				}
			}

			if (defined $c->{sources}) {
				generate_file_with_variants('Sources', $entry, join_records_if_needed($c->{sources}));
			}

			while (my ($lang, $content) = each %{$c->{translations}}) {
				generate_file_with_variants("Translation-$lang", $entry,
						join_records_if_needed($content), $c->{previous}{translations}{$lang});
			}
		}

		generate_release($release);
	}
}

sub get_list_prefix {
	my ($scheme, $server, $archive) = @_;
	$server =~ s{/}{_}g;
	return "var/lib/cupt/lists/${scheme}___${server}_dists_${archive}";
}

sub generate_release {
	my $release = shift;
	my %e = %$release;

	my @components = map { $_->{component} } @{$e{components}}; 
	my $pre_content = <<END;
Origin: $e{vendor}
Version: $e{version}
Label: $e{label}
Suite: $e{archive}
Codename: $e{codename}
Date: Mon, 30 Sep 2013 14:44:53 UTC
Valid-Until: $e{'valid-until'}
Architectures: $e{architecture} all
Components: @components
END
	if ($e{'not-automatic'}) {
		$pre_content .= "NotAutomatic: yes\n";
		if ($e{'but-automatic-upgrades'}) {
			$pre_content .= "ButAutomaticUpgrades: yes\n";
		}
	}
	$pre_content .= compose_sums_record($e{_ps_files});

	my $entry = { 'release' => $release, 'component' => undef };
	call_hooks('Release', $entry, $pre_content, [qw(sign orig)]);
	call_hooks('InRelease', $entry, $pre_content, [qw(sign inline)]);
	call_hooks('Release.gpg', $entry, $pre_content, [qw(sign detached)]);
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
	my $status_line = $options{'status-line'} // "$want ok installed";

	return compose_status_record($package_name, $status_line, $version_string);
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
	my ($command, %options) = @_;

	my $args = '';
	if ($options{'disable-package-indicators'}//1) {
		$args .= ' -o cupt::console::actions-preview::package-indicators::manually-installed=no';
	}

	return `echo 'q' | $command $args -s 2>&1`;
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

my $offer_version_regex = qr/(?:{\w})? \[.*? -> (.*?)\]/;

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

sub get_keyring_path {
	my $kind = shift;
	my $dir = get_test_dir() . '/gpg';
	my %map = (
		'good-1' => "$dir/mock1k.gpg",
		'good-2' => "$dir/mock4k.gpg",
		'expired' => "$dir/expired.gpg",
		'revoke-for-good-1' => "$dir/mock1k-revoke.gpg",
		'secrets' => "$dir/secrets.asc",
	);
	return $map{$kind};
}

sub get_good_signer {
	my ($keyring, $options) = @_;
	$options //= '';
	return sub {
		my ($variant, undef, undef, $input) = @_;

		my $sign_command = ($variant eq 'inline' ? '--clearsign' : '--detach-sign');
		my $secrets = get_keyring_path('secrets');

		# Signing a file F using a key K - how hard could it be? Involves
		# temporary homes, agents, daemons, session management, key imports.
		#
		# - http://stackoverflow.com/a/39848044
		# - https://lists.debian.org/debian-devel/2016/10/msg00267.html
		my $op_dir = File::Temp->newdir('gnupghome-XXXXXX');
		my $agent_prefix = "GNUPGHOME=$op_dir gpg-agent --daemon";
		my $import_part = "gpg --import $secrets";
		my $sign_part = "gpg --no-default-keyring --keyring $keyring --output - --armor $options $sign_command";

		run3("$agent_prefix sh -c '$import_part && $sign_part'", \$input, \my $stdout, \my $stderr);
		die $stderr if $?;

		return $stdout;
	};
}

1;

