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
package Cupt::Cache::Package;

=head1 NAME

Cupt::Cache::Package - store versions of binary or source deb package

=cut

use strict;
use warnings;

use Exporter qw(import);

our @EXPORT = qw(&compare_versions);

use Cupt::LValueFields qw(_unparsed_versions _parsed_versions
		_binary_architecture _allow_reinstall);

use Cupt::Core;

=head1 FLAGS

=head2 o_memoize

This flag determines whether it worth cacheing parsed versions.  Off by
default. If it's on, it stores references, so don't modify results of these
functions, use them in read-only mode. If it's on, these functions are not
thread-safe.

=cut

our $o_memoize = 0;

=head1 METHODS

=head2 new

returns a new Cupt::Cache::Package object. Usually shouldn't be called by hand.

=cut

sub new {
	my ($class, $binary_architecture, $allow_reinstall) = @_;
	my $self = bless [] => $class;
	$self->[_unparsed_versions_offset()] = [];
	$self->[_binary_architecture_offset()] = $binary_architecture;
	$self->[_allow_reinstall_offset()] = $allow_reinstall;
	return $self;
}

=head2 add_entry

method, adds unparsed entry to package. Usually should't be called by hand.

=cut

sub add_entry {
	my $self = shift;
	push @{$self->[_unparsed_versions_offset()]}, \@_;
	return;
}

=head2 get_versions

method, returns reference to array of versions
(L<Cupt::Cache::BinaryVersion|Cupt::Cache::BinaryVersion> or
L<Cupt::Cache::SourceVersion|Cupt::Cache::SourceVersion>) that this package
contains

=cut

sub get_versions {
	my ($self) = @_;

	if (not defined $self->[_parsed_versions_offset()]) {
		my @result;
		# parsing of versions is delayed, we parse them now (on-demand)
		eval {
			my @new_unparsed_versions;
			foreach my $ref_params (@{$self->[_unparsed_versions_offset()]}) {
				my $parsed_version;
				eval {
					my $version_class = shift @$ref_params;
					$parsed_version = $version_class->new($ref_params);
					if (ref $parsed_version eq 'Cupt::Cache::BinaryVersion' and
						$parsed_version->is_installed() and
						$self->[_allow_reinstall_offset()])
					{
						$parsed_version->version_string .= '~installed';
					}
					unshift @$ref_params, $version_class;
				};
				if (mycatch()) {
					mywarn("error while parsing new version entry for package '%s'", shift @$ref_params);
				} else {
					$self->_merge_version($parsed_version, \@result);
					unless ($o_memoize) {
						push @new_unparsed_versions, $ref_params;
					}
				}
			}
			if (not scalar @result) {
				mywarn('no valid versions available, discarding the package');
			}
			unless ($o_memoize) {
				$self->[_unparsed_versions_offset()] = \@new_unparsed_versions;
			}
		};
		if (mycatch()) {
			myerr('error while parsing package info');
			myredie();
		};

		if ($o_memoize) {
			$self->[_parsed_versions_offset()] = \@result;
			undef $self->[_unparsed_versions_offset()];
			undef $self->[_binary_architecture_offset()];
		} else {
			return \@result;
		}
	}
	return $self->[_parsed_versions_offset()];
}

=head2 get_specific_version

method, returns reference to
L<Cupt::Cache::BinaryVersion|Cupt::Cache::BinaryVersion> or
L<Cupt::Cache::SourceVersion|Cupt::Cache::SourceVersion> (depending on the
value of the object), which has specific version string, or undef if such
version isn't found

Parameters:

I<version_string> - version string to search

=cut

sub get_specific_version ($$) {
	my ($self, $lookup_version_string) = @_;

	foreach my $version (@{$self->get_versions()})
	{
		return $version if ($version->version_string eq $lookup_version_string);
	}
	return undef;
}

=head2 compare_versions

free subroutine, compares two versions by version number

Parameters:

I<first_version> - reference to L<Cupt::Cache::BinaryVersion|Cupt::Cache::BinaryVersion> or
L<Cupt::Cache::SourceVersion|Cupt::Cache::SourceVersion>

I<second_verson> - reference to L<Cupt::Cache::BinaryVersion|Cupt::Cache::BinaryVersion> or
L<Cupt::Cache::SourceVersion|Cupt::Cache::SourceVersion>

Returns:

the same as L<Cupt::Core::compare_version_strings|Cupt::Core/compare_version_strings>

=cut

sub compare_versions ($$) {
	return Cupt::Core::compare_version_strings($_[0]->version_string, $_[1]->version_string);
}

=head2 get_installed_version

method, returns reference to L<Cupt::Cache::BinaryVersion|Cupt::Cache::BinaryVersion> which is
installed in the system; if package is not installed, returns undef

=cut

sub get_installed_version ($) {
	my ($self) = @_;

	foreach my $version (@{$self->get_versions()})
	{
		return $version if ($version->is_installed());
	}
	return undef;
}

sub _merge_version {
	my ($self, $parsed_version, $ref_result) = @_;

	if (defined $self->[_binary_architecture_offset()] and ref $parsed_version eq 'Cupt::Cache::BinaryVersion') {
		if (not $parsed_version->is_installed()) {
			if ($parsed_version->architecture ne 'all' and
				$parsed_version->architecture ne $self->[_binary_architecture_offset()])
			{
				# no need to keep it
				return;
			}
		}
	}

	# some sanity checks
	if ($parsed_version->version_string !~ m/^\d/) {
		# the main part doesn't starts with a number, violating Debian Policy
		mywarn("the upstream part of version string '%s' should start with a number",
				$parsed_version->version_string);
	}
	if ($parsed_version->version_string =~ m/_/) {
		# underscores aren't allowed by Debian Policy
		mywarn("the version string '%s' shouldn't contain underscores",
				$parsed_version->version_string);
	}

	# merging
	eval {
		my $found_version;
		foreach my $version (@$ref_result)
		{
			if ($version->version_string eq $parsed_version->version_string) {
				$found_version = $version;
				last;
			}
		}
		if (not defined $found_version) {
			# no such version before, just add it
			push @$ref_result, $parsed_version;
		} else {
			# there is such version string

			if ((ref $found_version eq 'Cupt::Cache::BinaryVersion' and $found_version->is_installed())
				or $found_version->is_hashes_equal($parsed_version))
			{
				# 1)
				# this is installed version
				# as dpkg now doesn't provide hash sums, let's assume that
				# local version is the same that available from archive
				# 2)
				# ok, this is the same version;

				# so, adding new "available_as" info
				push @{$found_version->available_as}, $parsed_version->available_as->[0];

				if (ref $found_version eq 'Cupt::Cache::BinaryVersion' and $found_version->is_installed()) {
					# merge hashsums that are not available from installed
					# packages' info
					$found_version->md5sum = $parsed_version->md5sum;
					$found_version->sha1sum = $parsed_version->sha1sum;
					$found_version->sha256sum = $parsed_version->sha256sum;
				}
			} else {
				# err, no, this is different version :(
				my $info = sprintf __("package name: '%s', version string: '%s', origin: '%s'"),
						$parsed_version->package_name,
						$parsed_version->version_string,
						$parsed_version->available_as->[0]->{release}->{base_uri};
				mywarn('throwing away duplicating version with different hash sums: %s', $info);
			}
		}
	};
	if (mycatch()) {
		myerr("error while merging version '%s' for package '%s'",
				$parsed_version->version_string, $parsed_version->package_name);
		myredie();
	};
	return;
}

1;

