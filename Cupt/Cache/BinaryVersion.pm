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

=head2 task

task which the package belongs to, string, can be undef

=head2 tags

tags list, string, can be undef

=head2 others

hash entry that contains other fields found in the version entry in form { $name => $value }.

=cut

use Cupt::LValueFields qw(available_as package_name priority section installed_size 
		maintainer architecture source_package_name version_string source_version_string 
		essential depends recommends suggests conflicts breaks enhances provides 
		replaces pre_depends size md5sum sha1sum sha256sum short_description 
		long_description task tags others);

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

	$self->[available_as_offset()] = [];
	$self->[essential_offset()] = 0;
	$self->[pre_depends_offset()] = [];
	$self->[depends_offset()] = [];
	$self->[recommends_offset()] = [];
	$self->[suggests_offset()] = [];
	$self->[enhances_offset()] = [];
	$self->[conflicts_offset()] = [];
	$self->[replaces_offset()] = [];
	$self->[breaks_offset()] = [];
	$self->[provides_offset()] = [];
	$self->[others_offset()] = {};

	# parsing fields
	my ($package_name, $fh, $offset, $ref_release_info, $translation_fh, $translation_offset) = @$ref_arg;

	$self->[available_as_offset()]->[0]->{release} = $ref_release_info;
	$self->[package_name_offset()] = $package_name;

	do { # actual parsing
		# go to starting byte of the entry
		seek($fh, $offset, 0) or
				mydie('unable to seek on Packages file: %s', $!);

		local $_;
		do {
			local $/ = "\n\n";
			# read all version entry entirely
			$_ = <$fh>;
		};

		s/^Version: (.*)$//m and do { $self->[version_string_offset()] = $1 };
		s/^Essential: (.*)$//m and do { $self->[essential_offset()] = $1 };
		s/^Priority: (.*)$//m and do { $self->[priority_offset()] = $1 };
		s/^Size: (.*)$//m and do { $self->[size_offset()] = $1 };
		s/^Installed-Size: (.*)$//m and do { $self->[installed_size_offset()] = $1 * 1024 };
		s/^Architecture: (.*)$//m and do { $self->[architecture_offset()] = $1 };
		s/^Filename: (.*)$//m and do { $self->[available_as_offset()]->[0]->{filename} = $1 };
		s/^MD5sum: (.*)$//m and do { $self->[md5sum_offset()] = $1 };
		s/^SHA1: (.*)$//m and do { $self->[sha1sum_offset()] = $1 };
		s/^SHA256: (.*)$//m and do { $self->[sha256sum_offset()] = $1 };
		s/^Task: (.*)$//m and do { $self->[task_offset()] = $1 };
		s/^Source: (.*)$//m and do {
			$self->[source_package_name_offset()] = $1;
			if ($self->[source_package_name_offset()] =~ s/ \((.*)\)$//) {
				# there is a source version, most probably
				# indicating that it was some binary-only rebuild, and
				# the source version is different with binary one
				$self->[source_version_string_offset()] = $1;
				$self->[source_version_string_offset()] =~ m/^$version_string_regex$/ or
						mydie("bad source version '%s'", $1);
			}
		};

		unless ($o_no_parse_relations) {
			s/^Pre-Depends: (.*)$//m and do { $self->[pre_depends_offset()] = parse_relation_line($1) };
			s/^Depends: (.*)$//m and do { $self->[depends_offset()] = parse_relation_line($1) };
			s/^Recommends: (.*)$//m and do { $self->[recommends_offset()] = parse_relation_line($1) };
			s/^Suggests: (.*)$//m and do { $self->[suggests_offset()] = parse_relation_line($1) };
			s/^Conflicts: (.*)$//m and do { $self->[conflicts_offset()] = parse_relation_line($1) };
			s/^Breaks: (.*)$//m and do { $self->[breaks_offset()] = parse_relation_line($1) };
			s/^Replaces: (.*)$//m and do { $self->[replaces_offset()] = parse_relation_line($1) };
			s/^Enhances: (.*)$//m and do { $self->[enhances_offset()] = parse_relation_line($1) };
			s/^Provides: (.*)$//m and do { @{$self->[provides_offset()]} = split(/\s*,\s*/, $1) };
		}

		unless ($o_no_parse_info_onlys) {
			s/^Section: (.*)$//m and do { $self->[section_offset()] = $1 };
			s/^Maintainer: (.*)$//m and do { $self->[maintainer_offset()] = $1 };
			s/^Description: (.*)$(?:\n)((?:^(?: |\t).*$(?:\n))*)//m and do {
				$self->[short_description_offset()] = $1;
				$self->[long_description_offset()] = $2;
			};
			s/^Tag: (.*)$//m and do { $self->[tags_offset()] = $1 };
			while (s/^([A-Za-z-]+): (.*)$//m) {
				next if $1 eq 'Package' || $1 eq 'Status';
				$self->others->{$1} = $2;
			}
		}

		$self->[source_version_string_offset()] //= $self->[version_string_offset()];
		$self->[source_package_name_offset()] //= $self->[package_name_offset()];

		# read localized descriptions if available
		if (defined $translation_fh) {
			seek($translation_fh, $translation_offset, 0) or
					mydie('unable to seek on Translations file: %s', $!);
			$self->[long_description_offset()] = '';
			while ((my $line = <$translation_fh>) ne "\n") {
				next if $line =~ m/^Description-md5:/;
				if ($line =~ m/^Description/) {
					# it's localized short description
					# delete the field name
					chomp($line);
					$line =~ s/.*?: //;
					$self->[short_description_offset()] = $line;
				} else {
					# it's localized long description
					$self->[long_description_offset()] .= $line;
				}
			}
		}
	};

	defined $self->[version_string_offset()] or mydie("version string isn't defined");
	defined $self->[architecture_offset()] or mydie("architecture isn't defined");
	# checking hash sums
	if (!$self->is_installed() && !are_hash_sums_present($self->export_hash_sums())) {
		mydie('no hash sums specified');
	}

	$self->[priority_offset()] //= 'extra';

	return $self;
}

sub export_hash_sums {
	my ($self) = @_;

	return {
		'md5sum' => $self->[md5sum_offset()],
		'sha1sum' => $self->[sha1sum_offset()],
		'sha256sum' => $self->[sha256sum_offset()],
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
	return ($self->[available_as_offset()]->[0]->{release}->{base_uri} eq '');
}

1;

