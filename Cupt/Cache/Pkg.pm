package Cupt::Cache::Pkg;

use strict;
use warnings;

use Exporter qw(import);

our @EXPORT = qw(&compare_versions);

use Cupt::Core;

sub new {
	my ($class) = @_;
	my $self = []; # only unparsed versions
	return bless $self => $class;
}

# adds unparsed entry to package
sub add_entry {
	my $self = shift;
	push @$self, \@_;
}

# returns reference to versions array
sub versions {
	my ($self) = @_;

	my @result;
	# parsing of versions of this package was delayed, we parse them now (on-demand)
	eval {
		foreach (@$self) {
			my $parsed_version;
			eval {
				my $version_class = shift @$_;
				$parsed_version = $version_class->new($_);
				unshift @$_, $version_class;
			};
			if (mycatch()) {
				myerr("error while parsing new version entry");
				myredie();
			}
			$self->_merge_version($parsed_version, \@result);
		}
	};
	if (mycatch()) {
		myerr("error while parsing package info");
		myredie();
	};

	return \@result;
}

=head2 find_version

member function, returns reference to Cupt::Cache::{Binary,Source}Version
(depending on the value of the object), which has specific version string

Parameters:

I<version_string> - version string to search

=cut

sub find_version ($$) {
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

			if ($found_version->is_local() or $found_version->is_hashes_equal($parsed_version)) {
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

