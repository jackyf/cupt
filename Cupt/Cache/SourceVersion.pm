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

use List::MoreUtils qw(any);

=head1 FIELDS

=head2 available_as

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

source architecture of the package, string, defined in Debian Policy, §5.6.8

=head2 priority

priority, string, defined in Debian Policy, §5.6.6

=head2 section

section, string, defined in Debian Policy, §5.6.5, can be undef

=head2 standards_version

version of the last Debian Policy the package conforms to

=head2 maintainer

maintainer of the package, string

=head2 uploaders

package uploaders list, reference to array of strings, defined in Debian Policy, §5.6.3

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

=head2 others

hash entry that contains other fields found in the version entry in form { $name => $value }.

=cut

use Cupt::LValueFields qw(available_as package_name binary_package_names architecture
		priority section standards_version maintainer uploaders version_string
		build_depends build_depends_indep build_conflicts build_conflicts_indep 
		tarball diff dsc others);

use Cupt::Core;
use Cupt::Cache::ArchitecturedRelation qw(parse_architectured_relation_line);

=head1 FLAGS

=head2 o_no_parse_relations

Option to don't parse dependency relation between packages, can speed-up
parsing the version if this info isn't needed. Off by default.

=cut

our $o_no_parse_relations = 0;

=head2 o_no_parse_info_onlys

Option to don't parse 'Maintainer', 'Uploaders', 'Section' and unknown fields
(those which are put to L<others|/others>), can speed-up parsing the version if
this info isn't needed. Off by default.

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
	$self->available_as = [];
	$self->build_depends = [];
	$self->build_depends_indep = [];
	$self->build_conflicts = [];
	$self->build_conflicts_indep = [];
	$self->binary_package_names = [];
	$self->uploaders = [];
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
	$self->others = {};

	# parsing fields
	my ($package_name, $fh, $offset, $ref_release_info) = @$ref_arg;

	$self->available_as->[0]->{release} = $ref_release_info;
	$self->package_name = $package_name;

	do { # actual parsing
		# go to starting byte of the entry
		seek($fh, $offset, 0) or
				mydie('unable to seek on Sources file: %s', $!);

		local $_ = undef;
		do {
			local $/ = "\n\n";
			$_ = <$fh>;
		};

		while (s/^(Files|Checksums-Sha1|Checksums-Sha256): *$(?:\n)((?:^ .*$(?:\n))*)//m) {
			my $hash_sum_name = ($1 eq 'Files' ? 'md5sum' : ($1 eq 'Checksums-Sha1' ? 'sha1sum' : 'sha256sum'));
			foreach my $line (split(m/\n/, $2)) {
				my ($hash_sum, $size, $name) = ($line =~ m/^ ([[:xdigit:]]+) +(\d+) +(.*)$/) or
						mydie("malformed line '%s'", $line);
				local $_ = $name;
				my $part = m/.dsc$/ ? 'dsc' : ((m/.diff.gz$/ or m/.debian.tar.\w+/) ? 'diff' : 'tarball');
				$self->$part->{'filename'} = $name;
				$self->$part->{'size'} = $size;
				$self->$part->{$hash_sum_name} = $hash_sum;
			}
		}

		s/^Priority: (.*)$//m and do { $self->priority = $1 };
		s/^Architecture: (.*)$//m and do { $self->architecture = $1 };
		s/^Version: (.*)$//m and do { $self->version_string = $1 };
		s/^Binary: (.*\n(?:^ .*$(?:\n))*)//m and do {
			(my $full_line = $1) =~ s/\n//g; # delete linebreaks
			@{$self->binary_package_names} = split(/,\s*/, $full_line);
		};
		s/^Directory: (.*)$//m and do { $self->available_as->[0]->{directory} = $1 };

		unless ($o_no_parse_relations) {
			s/^Build-Depends: (.*)$//m and do {
				$self->build_depends = parse_architectured_relation_line($1);
			};
			s/^Build-Depends-Indep: (.*)$//m and do {
				$self->build_depends_indep = parse_architectured_relation_line($1);
			};
			s/^Build-Conflicts: (.*)$//m and do {
				$self->build_conflicts = parse_architectured_relation_line($1);
			};
			s/^Build-Conflicts-Indep: (.*)$//m and do {
				$self->build_conflicts_indep = parse_architectured_relation_line($1);
			};
		}

		unless ($o_no_parse_info_onlys) {
			s/^Section: (.*)$//m and do { $self->section = $1 };
			s/^Maintainer: (.*)$//m and do { $self->maintainer = $1 };
			s/^Uploaders: (.*)$//m and do { $self->uploaders = [ split(/\s*,\s*/, $1) ] };
			while (s/^([A-Za-z-]+): (.*)$//m) {
				$self->others->{$1} = $2;
			}
		}
	};

	# checking a presence of version string
	defined $self->version_string or mydie("version string isn't defined");

	# native Debian source packages don't contain Debian diff
	if (Cupt::Core::is_version_string_native($self->version_string)) {
		undef $self->diff;
	}

	# checking hash sums
	if (!are_hash_sums_present($self->tarball)) {
		mydie('no hash sums specified for tarball');
	}
	if (defined $self->diff && !are_hash_sums_present($self->diff)) {
		undef $self->diff;
		mywarn("source package '%s', version '%s': no diff entries but a dashed version, assuming it's a native package",
				$self->package_name, $self->version_string);
	}
	if (!are_hash_sums_present($self->dsc)) {
		mydie('no hash sums specified for dsc');
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
	foreach (@{$self->available_as}) {
		my $base_uri = $_->{release}->{base_uri};
		if ($base_uri ne '') {
			# real download path
			my $new_uri = ( $base_uri . '/' . $_->{'directory'} );
			foreach my $part ('tarball', 'diff', 'dsc') {
				next if $part eq 'diff' and not defined $self->diff;
				my $download_uri = $new_uri . '/' . $self->$part->{filename};
				push @{$result{$part}}, {
					'download_uri' => $download_uri,
					'base_uri' => $base_uri,
					'appendage' => $_->{'directory'} . '/' . $self->$part->{filename},
				} unless any { $_->{'download_uri'} eq $download_uri } @{$result{$part}};
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

	foreach (@{$self->available_as}) {
		if ($_->{release}->{signed}) {
			return 1;
		}
	}

	return 0;
}

1;

