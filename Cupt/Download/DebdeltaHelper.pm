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

package Cupt::Download::DebdeltaHelper;

use strict;
use warnings;

use URI::Escape;

use Cupt::Core;
use Cupt::Cache;
use Cupt::Cache::BinaryVersion;
use Cupt::LValueFields qw(_sources);

sub new {
	my $class = shift;
	my $self = bless [] => $class;
	$self->_sources = {};

	if (-e '/usr/bin/debpatch') {
		# fill debdelta sources only if patches is available
		my $sources_file = '/etc/debdelta/sources.conf';
		if (-r $sources_file) {
			eval {
				$self->_parse_sources($sources_file);
			};
			if (mycatch()) {
				mywarn("failed to parse debdelta configuration file '%s'", $sources_file);
			}
		}
	}

	return $self;
}

sub uris {
	my ($self, $version, $cache) = @_;

	my @result;

	my $package_name = $version->package_name;
	my $installed_version_string = $cache->get_system_state()->
			get_installed_version_string($package_name);

	my $sub_mangle_version_string = sub {
		# I hate http uris, hadn't I told this before, hm...
		my $result = $_[0];
		my $colon_pattern = uri_escape('%3a');
		$result =~ s/:/$colon_pattern/g;
		return $result;
	};

	if (defined $installed_version_string) {
		SOURCE:
		foreach my $ref_source (values %{$self->_sources}) {
			foreach my $key (keys %$ref_source) {
				next if $key eq 'delta_uri';
				my $found = 0;
				foreach my $release_info (map { $_->{release} } @{$version->available_as}) {
					if ($release_info->{$key} eq $ref_source->{$key}) {
						$found = 1;
						last;
					}
				}

				if (not $found) {
					next SOURCE;
				}
			}

			# suitable

			my $delta_uri = $ref_source->{'delta_uri'};

			my $base_uri = "debdelta:$delta_uri";

			# not very reliable :(
			my $appendage = $version->available_as->[0]->{filename};
			$appendage =~ s{(.*/).*}{$1};
			$appendage .= join('_', $package_name,
					$sub_mangle_version_string->($installed_version_string),
					$sub_mangle_version_string->($version->version_string),
					$version->architecture);
			$appendage .= '.debdelta';

			push @result, {
				'base_uri' => $base_uri,
				'download_uri' => "$base_uri/$appendage"
			};
		}
	}

	return @result;
}

sub _parse_sources {
	my ($self, $file) = @_;

	open(my $fd, '<', $file) or
			mydie("unable to open file '%s'", $file);

	my $current_section;

	# we are parsing entries like that:
	#
	# [main debian sources]
	# Origin=Debian
	# Label=Debian
	# delta_uri=http://www.bononia.it/debian-deltas
	#

	while (<$fd>) {
		# skip empty lines and lines with comments
		if (m/^\s*(#|$)/) {
			undef $current_section;
			next;
		}

		if (m/^\[/) {
			# new section
			if (defined $current_section) {
				mydie("new section before closing previous one in file '%s', line %u", $file, $.);
			}
			($current_section) = m/^\[(.*)\]$/;
			if (not defined $current_section) {
				mydie("unable to parse section name in file '%s', line %u", $file, $.);
			}
		} else {
			my ($key, $value) = m/^(.*?)=(.*)$/ or
					mydie("unable to parse key-value pair in file '%s', line %u", $file, $.);

			if ($key eq 'Origin') {
				$key = 'vendor';
			} elsif ($key eq 'Label') {
				$key = 'label';
			} elsif ($key eq 'Archive') {
				$key = 'archive';
			} elsif ($key eq 'delta_uri') {
				# fine :)
			} else {
				mydie("unknown key to parse section name in file '%s', line %u", $file, $.);
			}
			$self->_sources->{$current_section}->{$key} = $value;
		}
	}

	close($fd) or
			mydie("unable to close file '%s'", $file);

	return;
}

1;

