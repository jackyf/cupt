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

=encoding utf8

=head1 NAME

Cupt::Cache::BinaryVersion - store info about specific version of deb binary package

=cut

use 5.10.0;
use warnings;
use strict;

use List::MoreUtils qw(any);

=head1 FIELDS

=head2 available_as

  {
    'release' => $release_info
    'filename' => download URI appendage
  }

See L<Release info in Cupt::Cache|Cupt::Cache/Release info>.

=head2 package_name

name of the package, string, defined in Debian Policy, §3.1

=head2 priority

priority, string, defined in Debian Policy, §5.6.6

=head2 section

section, string, defined in Debian Policy, §5.6.5, can be undef

=head2 installed_size

size of unpacked archive in bytes

=head2 maintainer

maintainer of the package, string

=head2 architecture

binary architecture of the packge, string, defined in Debian Policy, §5.6.8

=head2 source_package_name

name of the corresponding source package, string, the same rules as for
L<package_name|/package_name>.

=head2 version_string

version string, defined in Debian Policy, §5.6.12

=head2 source_version_string

version string of the corresponding source package, the same rules as for
L<version_string|/version_string>

=head2 essential

is the version essential, boolean, defined in Debian Policy, §5.6.9

=head2 depends

[ L<relation_expression|Cupt::Cache::Relation/Relation expression>... ]

Debian Policy, §7.2, 'Depends'

=head2 recommends

[ L<relation_expression|Cupt::Cache::Relation/Relation expression>... ]

Debian Policy, §7.2, 'Recommends'

=head2 suggests

[ L<relation_expression|Cupt::Cache::Relation/Relation expression>... ]

Debian Policy, §7.2, 'Suggests'

=head2 conflicts

[ L<relation_expression|Cupt::Cache::Relation/Relation expression>... ]

Debian Policy, §7.2, 'Conflicts'

=head2 breaks

[ L<relation_expression|Cupt::Cache::Relation/Relation expression>... ]

Debian Policy, §7.2, 'Breaks'

=head2 enhances

[ L<relation_expression|Cupt::Cache::Relation/Relation expression>... ]

Debian Policy, §7.2, 'Enhances'

=head2 provides

[ L<package_name|/package_name>... ]

Debian Policy, §7.2, 'Provides'

=head2 replaces

[ L<relation_expression|Cupt::Cache::Relation/Relation expression>... ]

Debian Policy, §7.2, 'Replaces'

=head2 pre_depends

[ L<relation_expression|Cupt::Cache::Relation/Relation expression>... ]

Debian Policy, §7.2, 'Pre-Depends'

=head2 size

size of binary archive in bytes

=head2 md5sum

MD5 hash sum of the binary archive, can be undef

=head2 sha1sum

SHA1 hash sum of the binary archive, can be undef

=head2 sha256sum

SHA256 hash sum of the binary archive, can be undef

=head2 short_description

short description of the version, string, can be undef

=head2 long_description

long description of the version, multi-line string, can be undef

=head2 homepage

program home page URI, string, can be undef

=head2 task

task which the package belongs to, string, can be undef

=head2 tags

tags list, string, can be undef

=cut

use Cupt::LValueFields qw(available_as package_name priority section installed_size 
		maintainer architecture source_package_name version_string source_version_string 
		essential depends recommends suggests conflicts breaks enhances provides 
		replaces pre_depends size md5sum sha1sum sha256sum short_description 
		long_description homepage task tags);

use Cupt::Core;
use Cupt::Cache::Relation qw(parse_relation_line);

=head1 FLAGS

=head2 o_no_parse_relations

Option to don't parse dependency relation between packages, can speed-up
parsing the version if this info isn't needed. Off by default.

=cut

our $o_no_parse_relations = 0;

=head2 o_no_parse_info_onlys

Option to don't parse 'Maintainer', 'Description', 'Tag', 'Homepage',
'Section', can speed-up parsing the version if this info isn't needed. Off by
default.

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
	my $self = (bless [] => $class);

	# initialization

	$self->[_available_as_offset()] = [];
	$self->[_essential_offset()] = 0;
	$self->[_pre_depends_offset()] = [];
	$self->[_depends_offset()] = [];
	$self->[_recommends_offset()] = [];
	$self->[_suggests_offset()] = [];
	$self->[_enhances_offset()] = [];
	$self->[_conflicts_offset()] = [];
	$self->[_replaces_offset()] = [];
	$self->[_breaks_offset()] = [];
	$self->[_provides_offset()] = [];

	# parsing fields
	my ($package_name, $fh, $offset, $ref_release_info, $translation_fh, $translation_offset) = @$ref_arg;

	$self->[_available_as_offset()]->[0]->{release} = $ref_release_info;
	$self->[_package_name_offset()] = $package_name;

	my $field_name = undef;
	eval {
		# next boolean variable determines whether we are in long description
		my $in_long_description = 0;

		my $line;
		# go to starting byte of the entry
		seek($fh, $offset, 0) or
				mydie('unable to seek on Packages file: %s', $!);

		# we have already opened file handle and offset for reading the entry
		while (($line = <$fh>) ne "\n") {
			# skip all fields that haven't a value on the same line and aren't a part of multi-line fields
			next if $line =~ m/^\S.*:\n$/;

			if (($line =~ m/^ / or $line =~ m/^\t/)) {
				if ($in_long_description) {
					# TODO: remove this bogus '\t' after libobject-declare-perl is fixed
					# part of long description
					$self->[_long_description_offset()] .= $line unless $o_no_parse_info_onlys;
				}
			} else {
				$in_long_description = 0;
				chomp($line);
				(($field_name, my $field_value) = ($line =~ m/^((?:\w|-)+?): (.*)/)) # '$' implied in regexp
					or mydie("cannot parse line '%s'", $line);

				given ($field_name) {
					# mandatory fields
					when ('Priority') { $self->[_priority_offset()] = $field_value }
					when ('Section') { $self->[_section_offset()] = $field_value unless $o_no_parse_info_onlys }
					when ('Installed-Size') { $self->[_installed_size_offset()] = $field_value * 1024 }
					when ('Maintainer') { $self->[_maintainer_offset()] = $field_value unless $o_no_parse_info_onlys }
					when ('Architecture') { $self->[_architecture_offset()] = $field_value }
					when ('Version') { $self->[_version_string_offset()] = $field_value }
					when ('Filename') { $self->[_available_as_offset()]->[0]->{filename} = $field_value }
					when ('Size') { $self->[_size_offset()] = $field_value }
					when ('MD5sum') { $self->[_md5sum_offset()] = $field_value }
					when ('SHA1') { $self->[_sha1sum_offset()] = $field_value }
					when ('SHA256') { $self->[_sha256sum_offset()] = $field_value }
					when ('Description') {
						if (!$o_no_parse_info_onlys) {
							$self->[_short_description_offset()] = $field_value;
							$in_long_description = 1;
						}
					}
					# often fields
					when ('Depends') {
						$self->[_depends_offset()] = parse_relation_line($field_value) unless $o_no_parse_relations;
					}
					when ('Tag') { $self->[_tags_offset()] = $field_value unless $o_no_parse_info_onlys }
					when ('Source') {
						$self->[_source_package_name_offset()] = $field_value;
						if ($self->[_source_package_name_offset()] =~ s/ \((.*)\)$//) {
							# there is a source version, most probably
							# indicating that it was some binary-only rebuild, and
							# the source version is different with binary one
							$self->[_source_version_string_offset()] = $1;
							$self->[_source_version_string_offset()] =~ m/^$version_string_regex$/ or
									mydie("bad source version '%s'", $1);
						}
					}
					when ('Homepage') { $self->[_homepage_offset()] = $field_value unless $o_no_parse_info_onlys }
					when ('Recommends') {
						$self->[_recommends_offset()] = parse_relation_line($field_value) unless $o_no_parse_relations;
					}
					when ('Suggests') {
						$self->[_suggests_offset()] = parse_relation_line($field_value) unless $o_no_parse_relations;
					}
					when ('Conflicts') {
						$self->[_conflicts_offset()] = parse_relation_line($field_value) unless $o_no_parse_relations;
					}
					when ('Replaces') {
						$self->[_replaces_offset()] = parse_relation_line($field_value) unless $o_no_parse_relations;
					}
					when ('Provides') {
						$self->[_provides_offset()] = [ split /\s*,\s*/, $field_value ] unless $o_no_parse_relations;
					}
					# rare fields
					when ('Pre-Depends') {
						$self->[_pre_depends_offset()] = parse_relation_line($field_value) unless $o_no_parse_relations;
					}
					when ('Task') { $self->[_task_offset()] = $field_value }
					when ('Enhances') {
						$self->[_enhances_offset()] = parse_relation_line($field_value) unless $o_no_parse_relations;
					}
					when ('Essential') { $self->[_essential_offset()] = $field_value }
					when ('Breaks') {
						$self->[_breaks_offset()] = parse_relation_line($field_value) unless $o_no_parse_relations;
					}
				}
				undef $field_name;
			}
		}

		$self->[_source_version_string_offset()] //= $self->[_version_string_offset()];
		$self->[_source_package_name_offset()] //= $self->[_package_name_offset()];

		# read localized descriptions if available
		if (defined $translation_fh) {
			seek($translation_fh, $translation_offset, 0) or
					mydie('unable to seek on Translations file: %s', $!);
			$self->[_long_description_offset()] = '';
			while (($line = <$translation_fh>) ne "\n") {
				next if $line =~ m/^Description-md5:/;
				if ($line =~ m/^Description/) {
					# it's localized short description
					# delete the field name
					chomp($line);
					$line =~ s/.*?: //;
					$self->[_short_description_offset()] = $line;
				} else {
					# it's localized long description
					$self->[_long_description_offset()] .= $line;
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

	# checking a presence of version string
	defined $self->[_version_string_offset()] or mydie("version string isn't defined");
	# checking hash sums
	if (!$self->is_installed() && !are_hash_sums_present($self->export_hash_sums())) {
		mydie('no hash sums specified');
	}

	$self->[_priority_offset()] //= 'extra';

	return $self;
}

sub export_hash_sums {
	my ($self) = @_;

	return {
		'md5sum' => $self->[_md5sum_offset()],
		'sha1sum' => $self->[_sha1sum_offset()],
		'sha256sum' => $self->[_sha256sum_offset()],
	};
}

sub is_hashes_equal {
	my ($self, $other) = @_;
	return compare_hash_sums($self->export_hash_sums(), $other->export_hash_sums());
}

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
    foreach (@{$self->available_as}) {
		my $base_uri = $_->{release}->{base_uri};
		if ($base_uri ne '') {
			# real download path
			my $new_uri = ( $base_uri . '/' . $_->{'filename'} );

			push @result, {
				'download_uri' => $new_uri,
				'base_uri' => $base_uri,
				'appendage' => $_->{'filename'},
			} unless any { $_->{'download_uri'} eq $new_uri } @result;
		}
	}
	return @result;
}

=head2 is_signed

method, returns whether this version has signed source or not

=cut

sub is_signed ($$) {
	my ($self) = @_;

	foreach (@{$self->available_as}) {
		if ($_->{release}->{signed}) {
			return 1;
		}
	}

	return 0;
}

=head2 is_installed

method, returns whether this version is installed in the system or not

=cut

sub is_installed {
	(my $self) = @_;
	return ($self->[_available_as_offset()]->[0]->{release}->{base_uri} eq '');
}

1;

