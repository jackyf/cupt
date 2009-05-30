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
package Cupt::Cache::BinaryVersion;

=head1 NAME

Cupt::Cache::BinaryVersion - store info about specific version of deb binary package

=cut

use 5.10.0;
use warnings;
use strict;

use Cupt::Core;
use Cupt::Cache::Relation qw(parse_relation_line);

=head1 FLAGS

=head2 o_no_parse_relations

Option to don't parse dependency relation between packages, can speed-up
parsing the version if this info isn't needed. Off by default.

=cut

our $o_no_parse_relations = 0;

=head2 o_no_parse_info_onlys

Option to don't parse 'Maintainer', 'Description', 'Tag', 'Homepage', can
speed-up parsing the version if this info isn't needed. Off by default.

=cut

our $o_no_parse_info_onlys = 0;

=head1 METHODS

=head2 new

creates an Cupt::Cache::BinaryVersion

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
		#	filename

		package_name => undef,

		priority => undef,
		section => undef,
		installed_size => undef,
		maintainer => undef,
		architecture => undef,
		source_name => undef,
		version_string => undef,
		source_version_string => undef,
		essential => undef,
		depends => [],
		recommends => [],
		suggests => [],
		conflicts => [],
		breaks => [],
		enhances => [],
		provides => [],
		replaces => [],
		pre_depends => [],
		size => undef,
		md5sum => undef,
		sha1sum => undef,
		sha256sum => undef,
		short_description => undef,
		long_description => undef,
		homepage => undef,
		task => undef,
		tags => undef,
	};
	# parsing fields
	my ($package_name, $fh, $offset, $ref_release_info) = @$ref_arg;

	$self->{avail_as}->[0]->{release} = $ref_release_info;
	$self->{package_name} = $package_name;
	$self->{source_name} = $package_name; # may be redefined in appropriate tag

	my $field_name = undef;
	eval {
		# next boolean variable determines whether we are in long description
		my $in_long_description = 0;

		my $line;
		# go to starting byte of the entry
		seek $fh, $offset, 0;

		# we have already opened file handle and offset for reading the entry
		while (($line = <$fh>) ne "\n") {
			# skip all fields that haven't a value on the same line and aren't a part of multi-line fields
			next if $line =~ m/^\S.*:\n$/;

			if (($line =~ m/^ / or $line =~ m/^\t/)) {
				if ($in_long_description) {
					# TODO: remove this bogus '\t' after libobject-declare-perl is fixed
					# part of long description
					$self->{long_description} .= $line unless $o_no_parse_info_onlys;
				}
			} else {
				$in_long_description = 0;
				chomp($line);
				(($field_name, my $field_value) = ($line =~ m/^((?:\w|-)+?): (.*)/)) # '$' implied in regexp
					or mydie("cannot parse line '%s'", $line);

				given ($field_name) {
					# mandatory fields
					when ('Priority') { $self->{priority} = $field_value }
					when ('Section') { $self->{section} = $field_value unless $o_no_parse_info_onlys }
					when ('Installed-Size') { $self->{installed_size} = $field_value }
					when ('Maintainer') { $self->{maintainer} = $field_value unless $o_no_parse_info_onlys }
					when ('Architecture') { $self->{architecture} = $field_value }
					when ('Version') { $self->{version_string} = $field_value }
					when ('Filename') { $self->{avail_as}->[0]->{filename} = $field_value }
					when ('Size') { $self->{size} = $field_value }
					when ('MD5sum') { $self->{md5sum} = $field_value }
					when ('SHA1') { $self->{sha1sum} = $field_value }
					when ('SHA256') { $self->{sha256sum} = $field_value }
					when ('Description') {
						if (!$o_no_parse_info_onlys) {
							$self->{short_description} = $field_value;
							$in_long_description = 1;
						}
					}
					# often fields
					when ('Depends') {
						$self->{depends} = parse_relation_line($field_value) unless $o_no_parse_relations;
					}
					when ('Tag') { $self->{tags} = $field_value unless $o_no_parse_info_onlys }
					when ('Source') {
						$self->{source_name} = $field_value;
						if ($self->{source_name} =~ s/ \((.*)\)$//) {
							# there is a source version, most probably
							# indicating that it was some binary-only rebuild, and
							# the source version is different with binary one
							$self->{source_version_string} = $1;
							$self->{source_version_string} =~ m/^$version_string_regex$/ or
									mydie("bad source version '%s'", $1);
						}
					}
					when ('Homepage') { $self->{homepage} = $field_value unless $o_no_parse_info_onlys }
					when ('Recommends') {
						$self->{recommends} = parse_relation_line($field_value) unless $o_no_parse_relations;
					}
					when ('Suggests') {
						$self->{suggests} = parse_relation_line($field_value) unless $o_no_parse_relations;
					}
					when ('Conflicts') {
						$self->{conflicts} = parse_relation_line($field_value) unless $o_no_parse_relations;
					}
					when ('Replaces') {
						$self->{replaces} = parse_relation_line($field_value) unless $o_no_parse_relations;
					}
					when ('Provides') {
						$self->{provides} = [ split /\s*,\s*/, $field_value ] unless $o_no_parse_relations;
					}
					# rare fields
					when ('Pre-Depends') {
						$self->{pre_depends} = parse_relation_line($field_value) unless $o_no_parse_relations;
					}
					when ('Task') { $self->{homepage} = $field_value }
					when ('Enhances') {
						$self->{enhances} = parse_relation_line($field_value) unless $o_no_parse_relations;
					}
					when ('Essential') { $self->{essential} = $field_value }
					when ('Breaks') {
						$self->{breaks} = parse_relation_line($field_value) unless $o_no_parse_relations;
					}
				}
				undef $field_name;
			}
		}

		$self->{source_version_string} //= $self->{version_string};
	};
	if (mycatch()) {
		if (defined($field_name)) {
			myerr("error while parsing field '%s'", $field_name);
		}
		myredie();
	}
	return bless $self => $class;
}

sub is_hashes_equal {
	my $self = shift;
	my $other = shift;
	return ($self->{md5sum} eq $other->{md5sum} &&
			$self->{sha1sum} eq $other->{sha1sum} &&
			$self->{sha256sum} eq $other->{sha256sum});
}

=head1 METHODS

=head2 uris

method, returs available URIs to download the .deb file.

Returns:

array of I<URI entry>s.

where:

I<URI entry> - { 'download_uri' => I<download_uri>, 'base_uri' => I<base_uri>, 'appendage' => I<appendage> }

I<download_uri> - full URI to download

I<base_uri> - base URI (as specified in sources.list)

I<appendage> - string to append to base URI to compute I<download_uri>,
contains 'Filename' property of package entries.

=cut

sub uris {
	my $self = shift;
	my @result;
    foreach (@{$self->{avail_as}}) {
		my $base_uri = $_->{release}->{base_uri};
		if ($base_uri ne "") {
			# real download path
			my $new_uri = ( $base_uri . '/' . $_->{'filename'} );

			push @result, {
				'download_uri' => $new_uri,
				'base_uri' => $base_uri,
				'appendage' => $_->{'filename'},
			} unless grep { $_->{'download_uri'} eq $new_uri } @result;
		}
	}
	return @result;
}

=head2 is_signed

method, returns whether this version has signed source or not

=cut

sub is_signed ($$) {
	my ($self) = @_;

	my $has_signed_source = 0;
	foreach (@{$self->{avail_as}}) {
		if ($_->{release}->{signed}) {
			$has_signed_source = 1;
			last;
		}
	}

	return $has_signed_source;
}

=head2 is_installed

method, returns whether this version is installed in the system or not

=cut

sub is_installed {
	(my $self) = @_;
	return ($self->{avail_as}->[0]->{release}->{base_uri} eq "");
}

1;

