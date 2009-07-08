#***************************************************************************
#*   Copyright (C) 2008-2009 by Eugene V. Lyubimkin                        *
#*                                                                         *
#*   This program is free software; you can redistribute it and/or modify  *
#*   it under the terms of the GNU General Public License                  *
#*   (version 3 or above) as published by the Free Software Foundation.    *
#*                                                                         *
#*   This program is distributed in the hope that it will be useful,       *
#*   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
#*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
#*   GNU General Public License for more details.                          *
#*                                                                         *
#*   You should have received a copy of the GNU GPL                        *
#*   along with this program; if not, write to the                         *
#*   Free Software Foundation, Inc.,                                       *
#*   51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA               *
#*                                                                         *
#*   This program is free software; you can redistribute it and/or modify  *
#*   it under the terms of the Artistic License, which comes with Perl     *
#***************************************************************************
package Cupt::Cache;

=head1 NAME

Cupt::Cache - store info about available packages

=cut

use 5.10.0;
use strict;
use warnings;

use Digest;
use Fcntl qw(:seek :DEFAULT);

use Memoize;
memoize('verify_signature');

use Cupt::Core;
use Cupt::Cache::Package;
use Cupt::Cache::BinaryVersion;
use Cupt::Cache::SourceVersion;
use Cupt::System::State;

=begin internal

=head2 can_provide

{ I<virtual_package> => [ I<package_name>... ] }

For each I<virtual_package> this field contains the list of I<package_name>s
that B<can> provide given I<virtual_package>. Depending of package versions,
some versions of the some of <package_name>s may provide and may not provide
given I<virtual_package>. This field exists solely for
I<get_satisfying_versions> subroutine for rapid lookup.

=end internal

=cut

use fields qw(_source_packages _binary_packages _config _pin_settings _system_state
		_can_provide _extended_info _index_entries _release_data);

=head1 FLAGS

=head2 o_memoize

This flag determines whether it worth trade space for time in time-consuming
functions. On by default. By now, it affects
L</get_satisfying_versions> and L</get_sorted_pinned_versions>
methods. If it's on, it stores references, so B<don't> modify results of these
functions, use them in read-only mode. It it's on, these functions are not
thread-safe.

=cut

our $o_memoize = 1;

=head1 METHODS

=head2 new

creates a new Cupt::Cache object

Parameters:

I<config> - reference to L<Cupt::Config|Cupt::Config>

Next params are treated as hash-style param list:

'-source': read Sources

'-binary': read Packages

'-installed': read dpkg status file

Example:

  my $cache = new Cupt::Cache($config, '-source' => 0, '-binary' => 1);

=cut

sub new {
	my $class = shift;
	my $self = fields::new($class);

	$self->{_config} = shift;
	$self->{_pin_settings} = [];
	$self->{_source_packages} = {};
	$self->{_binary_packages} = {};
	$self->{_release_data}->{source} = [];
	$self->{_release_data}->{binary} = [];

	do { # ugly hack to copy trusted keyring from APT whenever possible
		my $cupt_keyring_file = $self->{_config}->var('gpgv::trustedkeyring');
		my $apt_keyring_file = '/etc/apt/trusted.gpg';
		# ignore all errors, let install do its best
		qx#install -m644 $apt_keyring_file $cupt_keyring_file >/dev/null 2>/dev/null#;
	};

	eval {
		$self->_parse_sources_lists();
	};
	if (mycatch()) {
		myerr("error while parsing sources list");
		myredie();
	}
	my $ref_index_entries = $self->get_index_entries();

	# determining which parts of cache we wish to build
	my %build_config = (
		'-source' => 1,
		'-binary' => 1,
		'-installed' => 1,
		@_ # applying passed parameters
	);

	if ($build_config{'-installed'}) {
		# read system settings
		$self->{_system_state} = new Cupt::System::State($self->{_config}, $self);
	}

	my @index_files;
	foreach my $ref_index_entry (@$ref_index_entries) {
		my $index_file_to_parse = $self->get_path_of_index_list($ref_index_entry);
		my $source_type = $ref_index_entry->{'type'};
		# don't parse unneeded indexes
		if (($source_type eq 'deb' && $build_config{'-binary'}) ||
			($source_type eq 'deb-src' && $build_config{'-source'}))
		{
			eval {
				my $ref_release_info = $self->_get_release_info($self->get_path_of_release_list($ref_index_entry));
				$ref_release_info->{component} = $ref_index_entry->{'component'};
				$ref_release_info->{base_uri} = $ref_index_entry->{'uri'};
				if ($source_type eq 'deb') {
					push @{$self->{_release_data}->{binary}}, $ref_release_info;
				} else {
					push @{$self->{_release_data}->{source}}, $ref_release_info;
				}

				$self->_process_index_file($index_file_to_parse, $source_type, $ref_release_info);
				push @index_files, $index_file_to_parse;
			};
			if (mycatch()) {
				mywarn("skipped index file '%s'", $index_file_to_parse);
			}
		}
	}

	$self->_process_provides_in_index_files(@index_files);

	# reading pin settings
	my $pin_settings_file = $self->_path_of_preferences();
	$self->_parse_preferences($pin_settings_file) if -r $pin_settings_file;

	# reading list of automatically installed packages
	my $extended_states_file = $self->_path_of_extended_states();
	$self->_parse_extended_states($extended_states_file) if -r $extended_states_file;

	return $self;
}

=head2 get_binary_packages

method, returns all binary packages as hash reference in form { $package_name
=> I<pkg> }, where I<pkg> is reference to L<Cupt::Cache::Package|Cupt::Cache::Package>

=cut

sub get_binary_packages ($) {
	my ($self) = @_;

	return $self->{_binary_packages};
}

=head2 get_system_state

method, returns reference to L<Cupt::System::State|Cupt::System::State>

=cut

sub get_system_state ($) {
	my ($self) = @_;

	return $self->{_system_state};
}

=head2 get_extended_info

method, returns info about extended package statuses in format:

  {
    'automatically_installed' => { I<package_name> => 1 },
  }

=cut

sub get_extended_info ($) {
	my ($self) = @_;

	return $self->{_extended_info};
}

=head2 is_automatically_installed

method, returns boolean value - is the package automatically installed
or not

Parameters:

I<package_name> - package name

=cut

sub is_automatically_installed ($$) {
	my ($self, $package_name) = @_;

	my $ref_auto_installed = $self->{_extended_info}->{'automatically_installed'};
	return (exists $ref_auto_installed->{$package_name} &&
			$ref_auto_installed->{$package_name});
}

=head2 get_original_apt_pin

method, returns pin value for the supplied version as described in apt_preferences(5)

Parameters:

I<version> - reference to L<Cupt::Cache::BinaryVersion|Cupt::Cache::BinaryVersion>

=cut

sub get_original_apt_pin {
	my ($self, $version) = @_;
	my $result;

	my $update_pin = sub ($) {
		if (not defined $result) {
			$result = $_[0];
		} elsif ($result < $_[0]) {
			$result = $_[0];
		}
	};

	my @available_as = @{$version->{avail_as}};

	# release-dependent settings
	my $default_release = $self->{_config}->var("apt::default-release");
	foreach (@available_as) {
		if (defined $default_release) {
			if ($_->{release}->{archive} eq $default_release ||
				$_->{release}->{codename} eq $default_release)
			{
				$update_pin->(990);
				last; # no sense to search further, this is maximum
			}
		}
		if ($_->{release}->{archive} eq 'experimental') {
			$update_pin->(1);
		} elsif ($_->{release}->{archive} eq 'installed') {
			$update_pin->(100);
		} else {
			$update_pin->(500);
		}
	}

	# looking in pin settings
	PIN:
	foreach my $ref_pin (@{$self->{_pin_settings}}) {
		if (exists $ref_pin->{'package_name'}) {
			my $value = $ref_pin->{'package_name'};
			$version->{package_name} =~ m/$value/ or next PIN;
		}
		if (exists $ref_pin->{'source_package_name'}) {
			$version->isa('Cupt::Cache::BinaryVersion') or next PIN;
			my $value = $ref_pin->{'source_package_name'};
			$version->{source_package_name} =~ m/$value/ or next PIN;
		}
		if (exists $ref_pin->{'version'}) {
			my $value = $ref_pin->{'version'};
			$version->{version_string} =~ m/$value/ or next PIN;
		}
		if (exists $ref_pin->{'base_uri'}) {
			my $value = $ref_pin->{'base_uri'};

			my $found = 0;
			foreach (@available_as) {
				if ($_->{release}->{base_uri} =~ m/$value/) {
					$found = 1;
					last;
				}
			}
			$found or next PIN;
		}
		if (exists $ref_pin->{'release'}) {
			my @keys = keys %{$ref_pin->{'release'}};
			foreach my $key (@keys) {
				my $value = $ref_pin->{'release'}->{$key};
				my $found = 0;
				foreach (@available_as) {
					defined $_->{release}->{$key} or
							myinternaldie("unexistent key '%s' in the release entry", $key);
					if ($_->{release}->{$key} =~ m/$value/) {
						$found = 1;
						last;
					}
				}
				$found or next PIN;
			}
		}

		# yeah, all conditions satisfied here, and we can set less pin too here
		$result = $ref_pin->{'value'};
	}

	return $result;
}

=head2 get_pin

method, returns Cupt pin value for the supplied version

Parameters:

I<version> - reference to L<Cupt::Cache::BinaryVersion|Cupt::Cache::BinaryVersion>

=cut

sub get_pin ($$) {
	my ($self, $version) = @_;
	my $result = $self->get_original_apt_pin($version);

	# discourage downgrading 
	# downgradings will usually have pin <= 0
	my $package_name = $version->{package_name};
	if (defined $self->{_system_state}) { # for example, for source versions will return false...
		my $installed_version_string = $self->{_system_state}->get_installed_version_string($package_name);
		if (defined $installed_version_string
			&& Cupt::Core::compare_version_strings($installed_version_string, $version->{version_string}) > 0)
		{
			$result -= 2000;
		}
	}

	$result += 1 if $version->is_signed();

	return $result;
}

=head2 get_binary_package

method, returns reference to appropriate L<Cupt::Cache::Package|Cupt::Cache::Package> for package name.
Returns undef if there is no such package in cache.

Parameters:

I<package_name> - package name to find

=cut

sub get_binary_package {
	my ($self, $package_name) = @_;
	# will transparently return undef if there is no such package
	return $self->{_binary_packages}->{$package_name};
};

=head2 get_source_package

method, returns reference to appropriate L<Cupt::Cache::Package|Cupt::Cache::Package> for package name.
Returns undef if there is no such package in cache.

Parameters:

I<package_name> - package name to find

=cut

sub get_source_package {
	my ($self, $package_name) = @_;
	# will transparently return undef if there is no such package
	return $self->{_source_packages}->{$package_name};
};

=head2 get_sorted_pinned_versions

method to get sorted by "candidatness" versions in descending order

Parameters:

I<package> - reference to L<Cupt::Cache::Package|Cupt::Cache::Package>

Returns: [ { 'version' => I<version>, 'pin' => I<pin> }... ]

where:

I<version> - reference to L<Cupt::Cache::BinaryVersion|Cupt::Cache::BinaryVersion>

I<pin> - pin value

=cut

sub get_sorted_pinned_versions {
	my ($self, $package) = @_;

	my @result;
	state %cache;

	# caching results
	if ($o_memoize) {
		my $key = join(",", $self, $package);
		if (exists $cache{$key}) {
			return $cache{$key};
		} else {
			$cache{$key} = \@result;
			# the @result itself will be filled by under lines of code so at
			# next run moment cache will contain the correct result
		}
	}

	foreach my $version (@{$package->get_versions()}) {
		push @result, { 'version' => $version, 'pin' => $self->get_pin($version) };
	}

	do {
		use sort 'stable';
		# sort in descending order, first key is pin, second is version string
		@result = sort {
			$b->{'pin'} <=> $a->{'pin'} or 
			compare_versions($b->{'version'}, $a->{'version'})
		} @result;
	};

	return \@result;
}

=head2 get_policy_version

method, returns reference to L<Cupt::Cache::BinaryVersion|Cupt::Cache::BinaryVersion>, this is the version
of I<package>, which to be installed by cupt policy

Parameters:

I<package> - reference to L<Cupt::Cache::Package|Cupt::Cache::Package>, package to select versions from

=cut

sub get_policy_version {
	my ($self, $package) = @_;

	# selecting by policy
	# we assume that every existent package have at least one version
	# this is how we add versions in 'Cupt::Cache::&_process_index_file'

	# so, just return version with maximum "candidatness"
	return $self->get_sorted_pinned_versions($package)->[0]->{'version'};
}

sub _get_satisfying_versions_for_one_relation {
	my ($self, $relation) = @_;
	my $package_name = $relation->package_name;

	my @result;
	state %cache;

	# caching results
	if ($o_memoize) {
		my $key = join(",",
				$self,
				$package_name,
				$relation->relation_string // "",
				$relation->version_string // ""
		);
		if (exists $cache{$key}) {
			return @{$cache{$key}};
		} else {
			$cache{$key} = \@result;
			# the @result itself will be filled by under lines of code so at
			# next run moment cache will contain the correct result
		}
	}

	my $package = $self->get_binary_package($package_name);

	if (defined $package) {
		# if such binary package exists
		my $ref_sorted_versions = $self->get_sorted_pinned_versions($package);
		foreach (@$ref_sorted_versions) {
			my $version = $_->{'version'};
			push @result, $version if $relation->satisfied_by($version->{version_string});
		}
	}

	# virtual package can only be considered if no relation sign is specified
	if (not defined $relation->relation_string && exists $self->{_can_provide}->{$package_name}) {
		# looking for reverse-provides
		foreach (@{$self->{_can_provide}->{$package_name}}) {
			my $reverse_provide_package = $self->get_binary_package($_);
			defined $reverse_provide_package or next;
			foreach (@{$self->get_sorted_pinned_versions($reverse_provide_package)}) {
				my $version = $_->{'version'};
				foreach my $provides_package_name (@{$version->{provides}}) {
					if ($provides_package_name eq $package_name) {
						# ok, this particular version does provide this virtual package
						push @result, $version;
					}
				}
			}
		}
	}

	return @result;
}

=head2 get_satisfying_versions

method, returns reference to array of L<Cupt::Cache::BinaryVersion|Cupt::Cache::BinaryVersion>
that satisfy relation, if no version can satisfy the relation, returns an
empty array

Parameters:

I<relation_expression> - see L<Relation expression in Cupt::Cache::Relation|Cupt::Cache::Relation/Relation expression>

=cut

sub get_satisfying_versions ($$) {
	my ($self, $relation_expression) = @_;

	if (ref $relation_expression ne 'ARRAY') {
		# relation expression is just one relation
		return [ $self->_get_satisfying_versions_for_one_relation($relation_expression) ];
	} else {
		# othersise it's OR group of expressions
		my @result = map { $self->_get_satisfying_versions_for_one_relation($_) } @$relation_expression;
		# get rid of duplicates
		my %seen;
		@result = grep { !$seen{ $_->{package_name}, $_->{version_string} } ++ } @result;
		return \@result;
	}
}

our %_empty_release_info = (
	'version' => undef,
	'description' => undef,
	'signed' => 0,
	'vendor' => undef,
	'label' => undef,
	'archive' => undef,
	'codename' => undef,
	'date' => undef,
	'valid-until' => undef,
	'architectures' => [],
	'base_uri' => undef,
);

sub _get_release_info {
	my ($self, $file) = @_;

	my %release_info = %_empty_release_info;

	open(RELEASE, '<', $file) or mydie("unable to open release file '%s'", $file);
	my $field_name = undef;
	eval {
		while (<RELEASE>) {
			(($field_name, my $field_value) = ($_ =~ m/^((?:\w|-)+?): (.*)/)) # '$' implied in regexp
				or last;

			given ($field_name) {
				when ('Origin') { $release_info{vendor} = $field_value }
				when ('Label') { $release_info{label} = $field_value }
				when ('Suite') { $release_info{archive} = $field_value }
				when ('Codename') { $release_info{codename} = $field_value }
				when ('Date') { $release_info{date} = $field_value }
				when ('Valid-Until') { $release_info{valid_until} = $field_value }
				when ('Architectures') { $release_info{architectures} = [ split / /, $field_value ] }
				when ('Description') {
					$release_info{description} = $field_value;
					if ($field_value =~ m/([0-9][0-9a-z._-]*)/) {
						$release_info{version} = $1;
					} else {
						$release_info{version} = '';
					}
				}
			}

			undef $field_name;
		}
	};
	if (mycatch()) {
		myerr("error parsing release file '%s', line %u", $file, $.);
		myredie();
	}
	if (!defined($release_info{description})) {
		mywarn("no description specified in release file '%s'", $file);
	}
	if (!defined($release_info{vendor})) {
		mydie("no vendor specified in release file '%s'", $file);
	}
	if (!defined($release_info{archive})) {
		mydie("no archive specified in release file '%s'", $file);
	}

	$release_info{label} //= '';
	$release_info{codename} //= '-';

	close(RELEASE) or mydie("unable to close release file '%s'", $file);

	$release_info{signed} = verify_signature($self->{_config}, $file);

	return \%release_info;
}

sub _parse_sources_lists {
	my ($self) = @_;
	my $root_prefix = $self->{_config}->var('dir');
	my $etc_dir = $self->{_config}->var('dir::etc');

	my $parts_dir = $self->{_config}->var('dir::etc::sourceparts');
	my @source_files = glob("$root_prefix$etc_dir/$parts_dir/*.list");

	my $main_file = $self->{_config}->var('dir::etc::sourcelist');
	my $main_file_path = "$root_prefix$etc_dir/$main_file";
	push @source_files, $main_file_path if -r $main_file_path;

	@{$self->{_index_entries}} = ();
	foreach (@source_files) {
		push @{$self->{_index_entries}}, __parse_source_list($_);
	}
}

=head2 get_index_entries

method, returns reference to list of L</index_entry>'s

=cut

sub get_index_entries {
	my ($self) = @_;

	return $self->{_index_entries};
}

sub __parse_source_list {
	my ($file) = @_;
	my @result;
	open(HFILE, '<', $file) or mydie("unable to open file '%s': %s", $file, $!);
	while (<HFILE>) {
		chomp;
		# skip all empty lines and lines with comments
		next if m/^\s*(?:#.*)?$/;

		my %entry;
		($entry{'type'}, $entry{'uri'}, $entry{'distribution'}, my @sections) = split ' ';

		mydie("incorrect source type at file '%s', line %u", $file, $.)
				if ($entry{'type'} ne 'deb' && $entry{'type'} ne 'deb-src');

		if (scalar @sections) {
			# this is normal entry
			map { $entry{'component'} = $_; push @result, { %entry }; } @sections;
		} else {
			# this a candidate for easy entry

			# distribution must end with a slash
			($entry{'distribution'} =~ s{/$}{}) or
					mydie("distribution doesn't end with a slash at file '%s', line %u", $file, $.);

			# ok, so adding single entry
			$entry{'component'} = "";
			push @result, { %entry };
		}
	}
	close(HFILE) or mydie("unable to close file '%s': %s", $file, $!);
	return @result;
}

sub _parse_preferences {
	my ($self, $file) = @_;

	# we are parsing triads like:

	# Package: perl
	# Pin: o=debian,a=unstable
	# Pin-Priority: 800

	# Source: unetbootin
	# Pin: a=experimental
	# Pin-Priority: 1100

	sub glob_to_regex ($) {
		$_[0] =~ s/\*/.*?/g;
		$_[0] =~ s/^/.*?/g;
		$_[0] =~ s/$/.*/g;
	}

	open(PREF, '<', $file) or mydie("unable to open file '%s': %s", $file, $!);
	while (<PREF>) {
		chomp;
		# skip all empty lines and lines with comments
		next if m/^\s*(?:#.*)?$/;
		# skip special explanation lines, they are just comments
		next if m/^Explanation: /;

		# ok, real triad should be here
		my %pin_result;

		do { # processing first line
			m/^(Package|Source): (.*)/ or
					mydie("bad package/source line at file '%s', line %u", $file, $.);

			my $name_type = ($1 eq 'Package' ? 'package_name' : 'source_package_name');
			my $name_value = $2;
			glob_to_regex($name_value);

			$pin_result{$name_type} = $name_value;
		};

		do { # processing second line
			my $pin_line = <PREF>;
			defined($pin_line) or
					mydie("no pin line at file '%s' line %u", $file, $.);

			$pin_line =~ m/^Pin: (\w+?) (.*)/ or
					mydie("bad pin line at file '%s' line %u", $file, $.);

			my $pin_type = $1;
			my $pin_expression = $2;
			given ($pin_type) {
				when ('release') {
					my @conditions = split /,/, $pin_expression;
					scalar @conditions or
							mydie("bad release expression at file '%s' line %u", $file, $.);

					foreach (@conditions) {
						m/^(\w)=(.*)/ or
								mydie("bad condition in release expression at file '%s' line %u", $file, $.);

						my $condition_type = $1;
						my $condition_value = $2;
						given ($condition_type) {
							when ('a') { $pin_result{'release'}->{'archive'} = $condition_value; }
							when ('v') { $pin_result{'release'}->{'version'} = $condition_value; }
							when ('c') { $pin_result{'release'}->{'component'} = $condition_value; }
							when ('n') { $pin_result{'release'}->{'codename'} = $condition_value; }
							when ('o') { $pin_result{'release'}->{'vendor'} = $condition_value; }
							when ('l') { $pin_result{'release'}->{'label'} = $condition_value; }
							default {
								mydie("bad condition type (should be one of 'a', 'v', 'c', 'n', 'o', 'l') " . 
										"in release expression at file '%s' line %u", $file, $.);
							}
						}
					}
				}
				when ('version') {
					glob_to_regex($pin_expression);
					$pin_result{'version'} = $pin_expression;
				}
				when ('origin') { # this is 'base_uri', really...
					$pin_result{'base_uri'} = $pin_expression;
				}
				default {
					mydie("bad pin type (should be one of 'release', 'version', 'origin') " . 
							"at file '%s' line %u", $file, $.);
				}
			}
		};

		do { # processing third line
			my $priority_line = <PREF>;
			defined($priority_line) or
					mydie("no priority line at file '%s' line %u", $file, $.);

			$priority_line =~ m/^Pin-Priority: ([+-]?\d+)/ or
					mydie("bad priority line at file '%s' line %u", $file, $.);

			my $priority = $1;
			$pin_result{'value'} = $priority;
		};

		# adding to storage
		push @{$self->{'_pin_settings'}}, \%pin_result;
	}

	close(PREF) or mydie("unable to close file '%s': %s", $file, $!);
}

sub _parse_extended_states {
	my ($self, $file) = @_;

	# we are parsing duals like:

	# Package: perl
	# Auto-Installed: 1

	eval {
		my $package_name;
		my $value;

		open(STATES, '<', $file) or mydie("unable to open file '%s': %s", $file, $!);
		while (<STATES>) {
			chomp;

			# skipping newlines
			next if $_ eq "";

			do { # processing first line
				m/^Package: (.*)/ or
						mydie("bad package line at file '%s', line %u", $file, $.);

				$package_name = $1;
			};

			do { # processing second line
				my $value_line = <STATES>;
				defined($value_line) or
						mydie("no value line at file '%s' line %u", $file, $.);

				$value_line =~ m/^Auto-Installed: (0|1)/ or
						mydie("bad value line at file '%s' line %u", $file, $.);

				$value = $1;
			};

			if ($value) {
				# adding to storage
				$self->{_extended_info}->{'automatically_installed'}->{$package_name} = $value;
			}
		}

		close(STATES) or mydie("unable to close file '%s': %s", $file, $!);
	};
	if (mycatch()) {
		myerr("error while parsing extended states");
		myredie();
	}
}

sub _process_provides_in_index_files {
	my ($self, @files) = @_;

	eval {
		foreach my $file (@files) {
			open(FILE, '<', $file) or
					mydie("unable to open file '%s': %s", $file, $!);

			my $package_line = '';
			while(<FILE>) {
				next if !m/^Package: / and !m/^Provides: /;
				chomp;
				if (m/^Pa/) {
					$package_line = $_;
					next;
				} else {
					my ($package_name) = ($package_line =~ m/^Package: (.*)/);
					my ($provides_subline) = m/^Provides: (.*)/;
					my @provides = split /\s*,\s*/, $provides_subline;

					foreach (@provides) {
						# if this entry is new one?
						if (!grep { $_ eq $package_name } @{$self->{_can_provide}->{$_}}) {
							push @{$self->{_can_provide}->{$_}}, $package_name ;
						}
					}
				}
			}
			close(FILE) or
					mydie("unable to close file '%s': %s", $file, $!);
		}
	};
	if (mycatch()) {
		myerr("error parsing provides");
		myredie();
	}

}

sub _process_index_file {
	my ($self, $file, $type, $ref_release_info) = @_;

	my $version_class;
	my $ref_packages_storage;
	if ($type eq 'deb') {
		$version_class = 'Cupt::Cache::BinaryVersion';
		$ref_packages_storage = \$self->{_binary_packages};
	} elsif ($type eq 'deb-src') {
		$version_class = 'Cupt::Cache::SourceVersion';
		$ref_packages_storage = \$self->{_source_packages};
	}

	my $fh;
	open($fh, '<', $file) or mydie("unable to open index file '%s': %s", $file, $!);
	open(OFFSETS, "/bin/grep -b '^Package: ' $file |"); 

	eval {
		while (<OFFSETS>) {
			my ($offset, $package_name) = /^(\d+):Package: (.*)/;

			# offset is returned by grep -b, and we skips 'Package: <...>' line additionally
			$offset += length("Package: ") + length($package_name) + 1;

			# check it for correctness
			($package_name =~ m/^$package_name_regex$/)
				or mydie("bad package name '%s'", $package_name);

			# adding new entry (and possible creating new package if absend)
			Cupt::Cache::Package::add_entry($$ref_packages_storage->{$package_name} //= Cupt::Cache::Package->new(),
					$version_class, $package_name, $fh, $offset, $ref_release_info);
		}
	};
	if (mycatch()) {
		myerr("error parsing index file '%s'", $file);
		myredie();
	}

	close(OFFSETS) or $! == 0 or mydie("unable to close grep pipe: %s", $!);
}

sub _path_of_base_uri {
	my ($self, $index_entry) = @_;

	# "http://ftp.ua.debian.org" -> "ftp.ua.debian.org"
	# "file:/home/jackyf" -> "/home/jackyf"
	(my $uri_prefix = $index_entry->{'uri'}) =~ s[^\w+:(?://)?][];

	# stripping last '/' from uri if present
	$uri_prefix =~ s{/$}{};
	
	# "escaping" tilde, following APT practice :(
	$uri_prefix =~ s/~/%7e/g;

	# "ftp.ua.debian.org/debian" -> "ftp.ua.debian.org_debian"
	$uri_prefix =~ tr[/][_];

	my $dirname = join('',
		$self->{_config}->var('dir'),
		$self->{_config}->var('dir::state'),
		'/',
		$self->{_config}->var('dir::state::lists')
	);

	(my $distribution_part = $index_entry->{'distribution'}) =~ tr[/][_];
	my $base_uri_part;
    if ($index_entry->{'component'} eq "") {
		# easy source type
		$base_uri_part = join('_', $uri_prefix, $distribution_part);
	} else {
		# normal source type
		$base_uri_part = join('_', $uri_prefix, 'dists', $distribution_part);
	}

	return join('', $dirname, '/', $base_uri_part);
}

sub _base_download_uri {
	my ($self, $index_entry) = @_;

    if ($index_entry->{'component'} eq "") {
		# easy source type
		return join('/', $index_entry->{'uri'}, $index_entry->{'distribution'});
	} else {
		# normal source type
		return join('/', $index_entry->{'uri'}, 'dists', $index_entry->{'distribution'});
	}
}

sub _index_list_suffix {
	my ($self, $index_entry, $delimiter) = @_;

	my $arch = $self->{_config}->var('apt::architecture');

	if ($index_entry->{'component'} eq "") {
		# easy source type
		return ($index_entry->{'type'} eq 'deb') ? "Packages" : 'Sources';
	} else {
		# normal source type
		return ($index_entry->{'type'} eq 'deb') ?
				"binary-${arch}${delimiter}Packages" : "source${delimiter}Sources";
	}
}

=head2 get_path_of_index_list

method, returns path of Packages/Sources file for I<index_entry>

Parameters:

L</index_entry>

=cut

sub get_path_of_index_list {
	my ($self, $index_entry) = @_;

	my $base_uri = $self->_path_of_base_uri($index_entry);
	my $index_list_suffix = $self->_index_list_suffix($index_entry, '_');

	my $filename = join('_', $base_uri, $index_entry->{'component'}, $index_list_suffix);
	$filename =~ s/__/_/; # if component is empty
	return $filename;
}

=head2 get_download_entries_of_index_list

method, returns the download entries of Packages/Sources file for I<index_entry>

Parameters:

L</index_entry>

path to accompanying Release file

Returns:

[ I<download_entry>... ]

where

I<download_entry> is

  {
    'uri' => {
               'size' => file size
               'md5sum' => MD5 hash sum
               'sha1sum' => SHA1 hash sum
               'sha256sum' => SHA256 hash sum
             }
  }

=cut

sub get_download_entries_of_index_list {
	my ($self, $index_entry, $path_to_release_file) = @_;

	my $base_download_uri = $self->_base_download_uri($index_entry);
	my $index_list_suffix = $self->_index_list_suffix($index_entry, '/');
	my $full_index_list_suffix = join('/', $index_entry->{'component'}, $index_list_suffix);
	$full_index_list_suffix =~ s{^/}{}; # if component is empty

	open(my $release_file_handle, '<', $path_to_release_file) or
			mydie("unable to open file '%s': %s", $path_to_release_file, $!);
	my @release_lines = <$release_file_handle>;
	close($release_file_handle) or
			mydie("unable to close file '%s': %s'", $path_to_release_file);

	my %result;

	my $current_hash_sum_name;
	# now we need to find if this variant is present in the release file
	foreach (@release_lines) {
		if (m/^MD5/) {
			$current_hash_sum_name = 'md5sum';
		} elsif (m/^SHA1/) {
			$current_hash_sum_name = 'sha1sum';
		} elsif (m/^SHA256/) {
			$current_hash_sum_name = 'sha256sum';
		} elsif (m/$full_index_list_suffix/) {
			my $release_line = $_;
			defined $current_hash_sum_name or
					mydie("release line '%s' without previous hash sum declaration at file '%s'",
							$release_line, $path_to_release_file);
			my ($hash_sum, $size, $name) = ($release_line =~ m/^ ([[:xdigit:]]+) +(\d+) +(.*)$/) or
					mydie("malformed release line '%s' at file '%s'", $release_line, $path_to_release_file);
			$name =~ m/^$full_index_list_suffix/ or next;
			# skipping diffs for now...
			$name !~ m/^$full_index_list_suffix.diff/ or next;
			my $uri = join('/', $base_download_uri, $name);
			$result{$uri}->{'size'} = $size;
			$result{$uri}->{$current_hash_sum_name} = $hash_sum;
		}
	}

	# checks
	foreach my $uri (keys %result) {
		my $ref_download_entry = $result{$uri};
		if (not exists $ref_download_entry->{'md5sum'}) {
			mydie("MD5 hash sum isn't defined for URI '%s'", $uri);
		}
		if (not exists $ref_download_entry->{'sha1sum'}) {
			mydie("SHA1 hash sum isn't defined for URI '%s'", $uri);
		}
		if (not exists $ref_download_entry->{'sha256sum'}) {
			mydie("SHA256 hash sum isn't defined for URI '%s'", $uri);
		}
	}

	return \%result;
}

=head2 get_path_of_release_list

method, returns path of Release file for I<index_entry>

Parameters:

L</index_entry>

=cut

sub get_path_of_release_list {
	my ($self, $index_entry) = @_;

	return join('_', $self->_path_of_base_uri($index_entry), 'Release');
}

=head2 get_download_uri_of_release_list

method, returns the remote URI of Release file for I<index_entry>

Parameters:

L</index_entry>

=cut

sub get_download_uri_of_release_list {
	my ($self, $index_entry) = @_;

	return join('/', $self->_base_download_uri($index_entry), 'Release');
}

sub _path_of_preferences {
	my ($self) = @_;

	my $root_prefix = $self->{_config}->var('dir');
	my $etc_dir = $self->{_config}->var('dir::etc');

	my $leaf = $self->{_config}->var('dir::etc::preferences');

	return "$root_prefix$etc_dir/$leaf";
}

sub _path_of_extended_states {
	my ($self) = @_;

	my $root_prefix = $self->{_config}->var('dir');
	my $etc_dir = $self->{_config}->var('dir::state');

	my $leaf = $self->{_config}->var('dir::state::extendedstates');

	return "$root_prefix$etc_dir/$leaf";
}

=head2 get_binary_release_data

method, returns reference to array of available releases of binary packages in
form [ L</release_info> ... ]

=cut

sub get_binary_release_data ($) {
	my ($self) = @_;
	return $self->{_release_data}->{binary};
}

=head2 get_source_release_data

method, returns reference to array of available releases of source packages in
form [ L</release_info> ... ]

=cut

sub get_source_release_data ($) {
	my ($self) = @_;
	return $self->{_release_data}->{source};
}

=head1 FREE SUBROUTINES

=head2 verify_signature

Checks signature of supplied file via GPG.

Parameters:

I<config> - reference to L<Cupt::Config|Cupt::Config>

I<file> - path to file

Returns:

non-zero on success, zero on fail

=cut

sub verify_signature ($$) {
	my ($config, $file) = @_;

	my $debug = $config->var('debug::gpgv');

	mydebug("verifying file '%s'", $file) if $debug;

	my $keyring_file = $config->var('gpgv::trustedkeyring');
	mydebug("keyring file is '%s'", $keyring_file) if $debug;

	my $signature_file = "$file.gpg";
	mydebug("signature file is '%s'", $signature_file) if $debug;
	-r $signature_file or
			do {
				mydebug("unable to read signature file '%s'", $signature_file) if $debug;
				return 0;
			};

	-e $keyring_file or
			do {
				mywarn("keyring file '%s' doesn't exist", $keyring_file);
				return 0;
			};
	-r $keyring_file or
			do {
				mywarn("no read rights on keyring file '%s', please do 'chmod +r %s' with root rights",
						$keyring_file, $keyring_file);
				return 0;
			};

	open(GPG_VERIFY, "gpg --verify --status-fd 1 --no-default-keyring " .
			"--keyring $keyring_file $signature_file $file 2>/dev/null |") or
			mydie("unable to open gpg pipe: %s", $!);
	my $sub_gpg_readline = sub {
		my $result;
		do {
			$result = readline(GPG_VERIFY);
			chomp $result;
			mydebug("fetched '%s' from gpg pipe", $result) if $debug;
		} while (defined $result and (($result =~ m/^\[GNUPG:\] SIG_ID/) or !($result =~ m/^\[GNUPG:\]/)));

		if (!defined $result) {
			return undef;
		} else {
			$result =~ s/^\[GNUPG:\] //;
			return $result;
		}
	};
	my $verify_result;

	my $status_string = $sub_gpg_readline->();
	if (defined $status_string) {
		# first line ought to be validness indicator
		my ($message_type, $message) = ($status_string =~ m/(\w+) (.*)/);
		given ($message_type) {
			when ('GOODSIG') {
				my $further_info = $sub_gpg_readline->();
				defined $further_info or
						mydie("gpg: '%s': unfinished status");

				my ($check_result_type, $check_message) = ($further_info =~ m/(\w+) (.*)/);
				given ($check_result_type) {
					when ('VALIDSIG') {
						# no comments :)
						$verify_result = 1;
					}
					when ('EXPSIG') {
						$verify_result = 0;
						mywarn("gpg: '%s': expired signature: %s", $file, $check_message);
					}
					when ('EXPKEYSIG') {
						$verify_result = 0;
						mywarn("gpg: '%s': expired key: %s", $file, $check_message);
					}
					when ('REVKEYSIG') {
						$verify_result = 0;
						mywarn("gpg: '%s': revoked key: %s", $file, $check_message);
					}
					default {
						mydie("gpg: '%s': unknown error: %s %s", $file, $check_result_type, $check_message);
					}
				}
			}
			when ('BADSIG') {
				mywarn("gpg: '%s': bad signature: %s", $file, $message);
				$verify_result = 0;
			}
			when ('ERRSIG') {
				# gpg was not able to verify signature
				mywarn("gpg: '%s': could not verify signature: %s", $file, $message);
				$verify_result = 0;
			}
			when ('NODATA') {
				# no signature
				mywarn("gpg: '%s': empty signature", $file);
				$verify_result = 0;
			}
			default {
				mydie("gpg: '%s': unknown message received: %s %s", $file, $message_type, $message);
			}
		}
	} else {
		# no info from gpg at all
		mydie("error while verifying signature for file '%s'", $file);
	}

	close(GPG_VERIFY) or $! == 0 or
			mydie("unable to close gpg pipe: %s", $!);

	mydebug("the verify result is %u", $verify_result) if $debug;
	return $verify_result;
}

=head2 verify_hash_sums

verifies MD5, SHA1 and SHA256 hash sums of file

Parameters:

I<hash sums> - { 'md5sum' => $md5sum, 'sha1sum' => $sha1sum', 'sha256sum' => $sha256sum }

I<path> - path to file

Returns: zero on failure, non-zero on success

=cut

sub verify_hash_sums ($$) {
	my ($ref_hash_sums, $path) = @_;

	my @checks = 	(
					[ $ref_hash_sums->{'md5sum'}, 'MD5' ],
					[ $ref_hash_sums->{'sha1sum'}, 'SHA-1' ],
					[ $ref_hash_sums->{'sha256sum'}, 'SHA-256' ],
					);

	open(FILE, '<', $path) or
			mydie("unable to open file '%s': %s", $path, $!);
	binmode(FILE);

	foreach (@checks) {
		my $expected_result = $_->[0];
		my $hash_type = $_->[1];
		my $hasher = Digest->new($hash_type);
		seek(FILE, 0, SEEK_SET);
		$hasher->addfile(*FILE);
		my $computed_sum = $hasher->hexdigest();
		return 0 if ($computed_sum ne $expected_result);
	}

	close(FILE) or
			mydie("unable to close file '%s': %s", $path, $!);

	return 1;
}

=head2 get_path_of_debian_changelog

free subroutine, returns string path of Debian changelog for version when
version is installed, undef otherwise

Parameters:

I<version> - reference to
L<Cupt::Cache::BinaryVersion|Cupt::Cache::BinaryVersion>

=cut

sub get_path_of_debian_changelog ($) {
	my ($version) = @_;

	return undef if not $version->is_installed();

	my $package_name = $version->{package_name};
	my $common_part = "/usr/share/doc/$package_name/";
	if (is_version_string_native($version->{version_string})) {
		return $common_part . 'changelog.gz';
	} else {
		return $common_part . 'changelog.Debian.gz';
	}
}

=head2 get_path_of_debian_copyright

free subroutine, returns string path of Debian copyright for version when
version is installed, undef otherwise

Parameters:

I<version> - reference to
L<Cupt::Cache::BinaryVersion|Cupt::Cache::BinaryVersion>

=cut

sub get_path_of_debian_copyright ($) {
	my ($version) = @_;

	return undef if not $version->is_installed();

	my $package_name = $version->{package_name};
	return "/usr/share/doc/$package_name/copyright";
}

=head1 DATA SPECIFICATION

=head2 release_info

This is a hash:

  {
    'signed' => boolean, whether release signed or not
    'version' => version of released distribution (can be undef)
    'description' => description string
    'vendor' => vendor string
    'label' => label string
    'archive' => archive name string
    'codename' => codename string
    'date' => date of release (can be undef)
    'valid-until' => time string when to forget about this release
    'architectures' => reference to array of available architectures
    'base_uri' => base URI (origin), empty string in case of "installed" distribution
  }

=head2 index_entry

This is a hash:
  
  {
    'type' => { 'deb' | 'deb-src' }
    'uri' => URI string
    'distribution' => distribution path
    'component' => component string
  }

=cut

1;

