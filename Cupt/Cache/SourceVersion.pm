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
package Cupt::Cache::SourceVersion;

=head1 NAME

Cupt::Cache::SourceVersion - store info about specific version of deb source package

=cut

use 5.10.0;
use warnings;
use strict;

use Cupt::Core;
use Cupt::Cache::ArchitecturedRelation qw(parse_architectured_relation_line);

=head1 FLAGS

=head2 o_no_parse_relations

Option to don't parse dependency relation between packages, can speed-up
parsing the version if this info isn't needed. Off by default.

=cut

our $o_no_parse_relations = 0;

=head2 o_no_parse_info_onlys

Option to don't parse 'Maintainer', 'Section', can
speed-up parsing the version if this info isn't needed. Off by default.

=cut

our $o_no_parse_info_onlys = 0;

=head1 METHODS

=head2 new

creates an Cupt::Cache::SourceVersion

Parameters:

I<initializer_argument> - [ $package_name, I<fh>, I<offset>, I<ref_release_info> ]

where

I<fh> - file handle to opened file that contains version entry

I<offset> - offset in bytes to locate version entry in I<fh>, may include 'Package:' line or not

I<ref_release_info> - reference to L<release info|Cupt::Cache/Release info>

=cut

sub new {
	my ($class, $ref_arg) = @_;
	my $self = {
		avail_as => [],
		# should contain array of hashes
		#	release => {
		#		...
		#	},
		#	directory

		package_name => undef,
		binary_package_names => [],

		architecture => undef,
		priority => undef,
		section => undef,
		standards_version => undef,
		maintainer => undef,
		version_string => undef,
		build_depends => [],
		tarball => {
			filename => undef,
			size => undef,
			md5sum => undef,
			sha1sum => undef,
			sha256sum => undef,
		},
		diff => {
			filename => undef,
			size => undef,
			md5sum => undef,
			sha1sum => undef,
			sha256sum => undef,
		},
		dsc => {
			filename => undef,
			size => undef,
			md5sum => undef,
		},
	};
	# parsing fields
	my ($package_name, $fh, $offset, $ref_release_info) = @$ref_arg;

	$self->{avail_as}->[0]->{release} = $ref_release_info;
	$self->{package_name} = $package_name;

	my $field_name = undef;
	eval {
		my $line;
		my $current_hash_sum_name;
		# go to starting byte of the entry
		seek $fh, $offset, 0;

		# we have already opened file handle and offset for reading the entry
		while (($line = <$fh>) ne "\n") {
			chomp($line);
			if ($line =~ m/^ /) {
				defined $current_hash_sum_name or
						mydie("line '%s' without previous hash sum declaration", $line);
				my ($hash_sum, $size, $name) = ($line =~ m/^ ([[:xdigit:]]+) +(\d+) +(.*)$/) or
						mydie("malformed line '%s'", $line);
				local $_ = $name;
				my $part = m/.dsc$/ ? 'dsc' : (m/.diff.gz$/ ? 'diff' : 'tarball');
				$self->{$part}->{'filename'} = $name;
				$self->{$part}->{'size'} = $size;
				$self->{$part}->{$current_hash_sum_name} = $hash_sum;
			} else {
				if ($line =~ m/^Files:/) {
					$current_hash_sum_name = 'md5sum';
				} elsif ($line =~ m/^Checksums-Sha1:/) {
					$current_hash_sum_name = 'sha1sum';
				} elsif ($line =~ m/^Checksums-Sha256:/) {
					$current_hash_sum_name = 'sha256sum';
				} else {
					undef $current_hash_sum_name;

					(($field_name, my $field_value) = ($line =~ m/^((?:\w|-)+?): (.*)/)) # '$' implied in regexp
						or mydie("cannot parse line '%s'", $line);

					given ($field_name) {
						when ('Build-Depends') {
							$self->{build_depends} = parse_architectured_relation_line($field_value) unless $o_no_parse_relations;
						}
						when ('Priority') { $self->{priority} = $field_value }
						when ('Section') { $self->{section} = $field_value unless $o_no_parse_info_onlys }
						when ('Maintainer') { $self->{maintainer} = $field_value unless $o_no_parse_info_onlys }
						when ('Architecture') { $self->{architecture} = $field_value }
						when ('Version') { $self->{version_string} = $field_value }
						when ('Directory') { $self->{avail_as}->[0]->{directory} = $field_value }
					}
					undef $field_name;
				}
			}
		}
	};
	if (mycatch()) {
		if (defined($field_name)) {
			myerr("error while parsing field '%s'", $field_name);
		}
		myredie();
	}
	bless $self => $class;

	# checking a presence of version string
	defined $self->{version_string} or mydie("version string isn't defined");
	# checking hash sums
	defined $self->{tarball}->{md5sum} or mydie("MD5 hash sum of tarball isn't defined");
	defined $self->{tarball}->{sha1sum} or mydie("SHA1 hash sum of tarball isn't defined");
	defined $self->{tarball}->{sha256sum} or mydie("SHA256 hash sum of tarball isn't defined");
	defined $self->{diff}->{md5sum} or mydie("MD5 hash sum of diff isn't defined");
	defined $self->{diff}->{sha1sum} or mydie("SHA1 hash sum of diff isn't defined");
	defined $self->{diff}->{sha256sum} or mydie("SHA256 hash sum of diff isn't defined");

	return $self;
}

sub is_hashes_equal {
	my ($self, $other) = @_;
	return ($self->{diff}->{md5sum} eq $other->{diff}->{md5sum} &&
			$self->{diff}->{sha1sum} eq $other->{diff}->{sha1sum} &&
			$self->{diff}->{sha256sum} eq $other->{diff}->{sha256sum} &&
			$self->{tarball}->{md5sum} eq $other->{tarball}->{md5sum} &&
			$self->{tarball}->{sha1sum} eq $other->{tarball}->{sha1sum} &&
			$self->{tarball}->{sha256sum} eq $other->{tarball}->{sha256sum});
}

=head1 METHODS

=head2 uris

method, returns available URIs to download the .deb file.

Returns:

  {
    'tarball' => [ I<URI entry>... ],
    'diff' => [ I<URI entry>... ],
    'dsc' => [ I<URI entry>... ],
  }

where:

I<URI entry> - { 'download_uri' => I<download_uri>, 'base_uri' => I<base_uri>, 'appendage' => I<appendage> }

I<download_uri> - full URI to download

I<base_uri> - base URI (as specified in sources.list)

I<appendage> - string to append to base URI to compute I<download_uri>,
contains 'Filename' property of package entries.

=cut

sub uris {
	my $self = shift;
	my %result;
	foreach (@{$self->{avail_as}}) {
		my $base_uri = $_->{release}->{base_uri};
		if ($base_uri ne "") {
			# real download path
			my $new_uri = ( $base_uri . '/' . $_->{'directory'} );
			foreach my $part ('tarball', 'diff', 'dsc') {
				my $download_uri = $new_uri . '/' . $self->{$part}->{filename};
				push @{$result{$part}}, {
					'download_uri' => $download_uri,
					'base_uri' => $base_uri,
					'appendage' => $_->{'directory'} . '/' . $self->{$part}->{filename},
				} unless grep { $_->{'download_uri'} eq $download_uri } @{$result{$part}};
			}
		}
	}
	return \%result;
}

=head2 is_signed

method, returns whether this version has signed source or not

=cut

sub is_signed ($$) {
	my ($self) = @_;

	foreach (@{$self->{avail_as}}) {
		if ($_->{release}->{signed}) {
			return 1;
		}
	}

	return 0;
}

1;

