package Cupt::Cache::Pkg;

use strict;
use warnings;

use Exporter qw(import);

our @EXPORT = qw(&compare_versions);

use Cupt::Core;

=head1 METHODS

=head2 new

returns a new Cupt::Cache::Pkg object. Usually shouldn't be created by hand.

=cut

sub new {
	my ($class) = @_;
	my $self = []; # only unparsed versions
	return bless $self => $class;
}

=head2 add_entry

method, adds unparsed entry to package. Usually should't be called by hand.

=cut

sub add_entry {
	my $self = shift;
	push @$self, \@_;
}

=head2 versions

method, returns reference to array of versions (Cupt::Cache::BinaryVersion or
Cupt::Cache::SourceVersion) that this package contains

=cut

sub versions {
	my ($self) = @_;

	my @result;
	# parsing of versions of this package was delayed, we parse them now (on-demand)
	eval {
		my @errored_indexes;
		foreach my $idx (0..$#{$self}) {
			my $ref_params = $self->[$idx];
			my $parsed_version;
			eval {
				my $version_class = shift @$ref_params;
				$parsed_version = $version_class->new($ref_params);
				unshift @$ref_params, $version_class;
			};
			# FIXME: error, not warning after science-mathematics has been fixed in the Debian archive
			# and remove this fat index stuff for removing broken entries
			if (mycatch()) {
				# delete broken entry at all...
				push @errored_indexes, $idx;
				mywarn("error while parsing new version entry");
			} else {
				$self->_merge_version($parsed_version, \@result);
			}
		}
		splice @$self, $_, 1 foreach @errored_indexes;
	};
	if (mycatch()) {
		myerr("error while parsing package info");
		myredie();
	};

	return \@result;
}

=head2 get_specific_version

method, returns reference to Cupt::Cache::{Binary,Source}Version
(depending on the value of the object), which has specific version string

Parameters:

I<version_string> - version string to search

=cut

sub get_specific_version ($$) {
	my ($self, $lookup_version_string) = @_;

	foreach my $version (@{$self->versions()})
	{
		return $version if ($version->{version_string} eq $lookup_version_string);
	}
	return undef;
}

sub compare_versions ($$) {
	return Cupt::Core::compare_version_strings($_[0]->{version_string}, $_[1]->{version_string});
}

=head2 get_installed_version

method, returns reference to Cupt::Cache::BinaryVersion which is
installed in the system; if package is not installed, returns undef

=cut

sub get_installed_version ($) {
	my ($self) = @_;

	foreach my $version (@{$self->versions()})
	{
		return $version if ($version->is_installed());
	}
	return undef;
}

sub _merge_version {
	my ($self, $parsed_version, $ref_result) = @_;

	# merging
	eval {
		my $found_version;
		foreach my $version (@$ref_result)
		{
			if ($version->{version_string} eq $parsed_version->{version_string}) {
				$found_version = $version;
				last;
			}
		}
		if (!defined($found_version)) {
			# no such version before, just add it
			push @$ref_result, $parsed_version;
		} else {
			# there is such version string

			if ($found_version->is_installed() or $found_version->is_hashes_equal($parsed_version)) {
				# 1)
				# this is locally installed version
				# as dpkg now doesn't provide hash sums, let's assume that
				# local version is the same that available from archive
				# 2)
				# ok, this is the same version;

				# so, adding new "avail_as" info
				push @{$found_version->{avail_as}}, $parsed_version->{avail_as}->[0];
			} else {
				# err, no, this is different package :(
				# just skip it for now
			}
		}
	};
	if (mycatch()) {
		myerr("error while merging version '%s'", $parsed_version->{version_string});
		myredie();
	};
}

1;

