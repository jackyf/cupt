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

=encoding utf8

=head1 NAME

Cupt::Cache::SourceVersion - store info about specific version of deb source package

=cut

use 5.10.0;
use warnings;
use strict;

=head1 FIELDS

=head2 avail_as

  {
    'release' => $release_info
    'directory' => download URI appendage (directory part)
  }

See L<Release info in Cupt::Cache|Cupt::Cache/Release info>.

=head2 package_name

name of the package, string, defined in Debian Policy, §3.1

=head2 binary_package_names

reference to array of binary package names (strings) that build from this package

=head2 architecture

source architecture of the packge, string, defined in Debian Policy, §5.6.8

=head2 priority

priority, string, defined in Debian Policy, §5.6.6

=head2 section

section, string, defined in Debian Policy, §5.6.5, can be undef

=head2 standards_version

version of the last Debian Policy the package conforms to

=head2 maintainer

maintainer of the package, string

=head2 uploaders

package uploaders list, string, defined in Debian Policy, §5.6.3

=head2 version_string

version string, defined in Debian Policy, §5.6.12

=head2 build_depends

[ L<relation_expression|Cupt::Cache::Relation/Relation expression>... ]

Debian Policy, §7.1, 'Build-Depends'

Note that relation expression in this in all other fields of this class is
build upon
L<Cupt::Cache::ArchitecturedRelation|Cupt::Cache::ArchitecturedRelation> which is subclass of
L<Cupt::Cache::Relation|Cupt::Cache::Relation>.

=head2 build_depends_indep

[ L<relation_expression|Cupt::Cache::Relation/Relation expression>... ]

Debian Policy, §7.1, 'Build-Depends-Indep'

=head2 build_conflicts

[ L<relation_expression|Cupt::Cache::Relation/Relation expression>... ]

Debian Policy, §7.1, 'Build-Conflicts'

=head2 build_conflicts_indep

[ L<relation_expression|Cupt::Cache::Relation/Relation expression>... ]

Debian Policy, §7.1, 'Build-Conflicts-Indep'

=head2 tarball

  {
    'filename' => download URI appendage (file name ("leaf") part)
    'size' => size
    'md5sum' => MD5 hash sum of the file, can be undef
    'sha1sum' => SHA1 hash sum of the file, can be undef
    'sha256sum' => SHA256 hash sum of the file, can be undef
  }

=head2 diff

same as L<tarball|/tarball>, can be whole undef in case of Debian native package

=head2 dsc

same as L<tarball|/tarball>

=cut

use Cupt::LValueFields qw(avail_as package_name binary_package_names architecture
		priority section standards_version maintainer uploaders version_string
		build_depends build_depends_indep build_conflicts build_conflicts_indep 
		tarball diff dsc);

use Cupt::Core;
use Cupt::Cache::ArchitecturedRelation qw(parse_architectured_relation_line);

=head1 FLAGS

=head2 o_no_parse_relations

Option to don't parse dependency relation between packages, can speed-up
parsing the version if this info isn't needed. Off by default.

=cut

our $o_no_parse_relations = 0;

=head2 o_no_parse_info_onlys

Option to don't parse 'Maintainer', 'Uploaders', 'Section', can
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
	my $self = (bless [] => $class);
	$self->avail_as = [];
	$self->build_depends = [];
	$self->build_depends_indep = [];
	$self->build_conflicts = [];
	$self->build_conflicts_indep = [];
	$self->binary_package_names = [];
	$self->tarball = {
		filename => undef,
		size => undef,
		md5sum => undef,
		sha1sum => undef,
		sha256sum => undef,
	};
	$self->diff = {
		filename => undef,
		size => undef,
		md5sum => undef,
		sha1sum => undef,
		sha256sum => undef,
	};
	$self->dsc = {
		filename => undef,
		size => undef,
		md5sum => undef,
	};
	# parsing fields
	my ($package_name, $fh, $offset, $ref_release_info) = @$ref_arg;

	$self->avail_as->[0]->{release} = $ref_release_info;
	$self->package_name = $package_name;

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
				$self->$part->{'filename'} = $name;
				$self->$part->{'size'} = $size;
				$self->$part->{$current_hash_sum_name} = $hash_sum;
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
							$self->build_depends = parse_architectured_relation_line($field_value) unless $o_no_parse_relations;
						}
						when ('Build-Depends-Indep') {
							$self->build_depends_indep = parse_architectured_relation_line($field_value) unless $o_no_parse_relations;
						}
						when ('Binary') { @{$self->binary_package_names} = split(/, /, $field_value) }
						when ('Priority') { $self->priority = $field_value }
						when ('Section') { $self->section = $field_value unless $o_no_parse_info_onlys }
						when ('Maintainer') { $self->maintainer = $field_value unless $o_no_parse_info_onlys }
						when ('Uploaders') { $self->uploaders = $field_value unless $o_no_parse_info_onlys }
						when ('Architecture') { $self->architecture = $field_value }
						when ('Version') { $self->version_string = $field_value }
						when ('Directory') { $self->avail_as->[0]->{directory} = $field_value }
						when ('Build-Conflicts') {
							$self->build_conflicts = parse_architectured_relation_line($field_value) unless $o_no_parse_relations;
						}
						when ('Build-Conflicts-Indep') {
							$self->build_conflicts_indep = parse_architectured_relation_line($field_value) unless $o_no_parse_relations;
						}
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
	defined $self->version_string or mydie("version string isn't defined");

	# native Debian source packages don't contain Debian diff
	if (Cupt::Core::is_version_string_native($self->version_string)) {
		undef $self->diff;
	}

	# checking hash sums
	if (!are_hash_sums_present($self->tarball)) {
		mydie("no hash sums specified for tarball");
	}
	if (defined $self->diff && !are_hash_sums_present($self->diff)) {
		mydie("no hash sums specified for diff");
	}
	if (!are_hash_sums_present($self->dsc)) {
		mydie("no hash sums specified for dsc");
	}

	return $self;
}

sub is_hashes_equal {
	my ($self, $other) = @_;
	compare_hash_sums($self->tarball, $other->tarball) or return 0;
	if (defined $self->diff) {
		compare_hash_sums($self->diff, $other->diff) or return 0;
	}
	return compare_hash_sums($self->dsc, $other->dsc);
}

=head1 METHODS

=head2 uris

method, returns available URIs to download the .deb file.

Returns:

  {
    'tarball' => [ I<URI entry>... ],
    (optionally) 'diff' => [ I<URI entry>... ],
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
	foreach (@{$self->avail_as}) {
		my $base_uri = $_->{release}->{base_uri};
		if ($base_uri ne "") {
			# real download path
			my $new_uri = ( $base_uri . '/' . $_->{'directory'} );
			foreach my $part ('tarball', 'diff', 'dsc') {
				next if $part eq 'diff' and not defined $self->diff;
				my $download_uri = $new_uri . '/' . $self->$part->{filename};
				push @{$result{$part}}, {
					'download_uri' => $download_uri,
					'base_uri' => $base_uri,
					'appendage' => $_->{'directory'} . '/' . $self->$part->{filename},
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

	foreach (@{$self->avail_as}) {
		if ($_->{release}->{signed}) {
			return 1;
		}
	}

	return 0;
}

1;

