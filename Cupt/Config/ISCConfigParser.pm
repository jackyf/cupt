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
package Cupt::Config::ISCConfigParser;

=head1 NAME

Cupt::Config::ISCConfigParser - parser for bind-like configuration files

=cut

# This package is inspired from Matt Dainly's BIND::Config::Parser

use strict;
use warnings;

use Parse::RecDescent;

use Cupt::Core;

my $grammar = q{

	<autotree>

	program:
		  <skip: qr{\s*
		            (?:(?://|\#)[^\n]*\n\s*|/\*(?:[^*]+|\*(?!/))*\*/\s*)*
		           }x> statement(s?) eofile { $item[2] }

	statement:
		  simple | nested | list

	simple:
		  name value ';'

	nested:
		  name '{' statement(s?) '}' ';'

	list:
		  name '{' (value ';')(s?) '}' ';'

	name:
		  /([\w\/-]+::)*([\w\/-]+)/

	value:
		  /".*"/

	eofile:
		  /^\Z/
};

sub new {
	my $class = shift;

	my $self = {
		'_regular_handler' => undef,
		'_list_handler' => undef,
	};

	$self->{'_parser'} = new Parse::RecDescent($grammar)
		or myinternaldie("bad grammar");

	bless $self, $class;
	return $self;
}

sub parse_file {
	my ($self, $file) = @_;

	open(my $file_handle, $file) or mydie("unable to open file '%s': %s", $file, $!);
	my $text = join("", <$file_handle>);
	close($file_handle) or mydie("unable to close file '%s': %s", $file, $!);

	defined( my $tree = $self->{'_parser'}->program($text) )
		or mydie("bad config in file '%s'", $file);

	$self->_recurse($tree, "");
}

sub set_regular_handler {
	my $self = shift;
	$self->{'_regular_handler'} = shift;
}

sub set_list_handler
{
	my $self = shift;
	$self->{'_list_handler'} = shift;
}

sub _recurse {
	my $self = shift;
	my $tree = shift;
	my $name_prefix = shift;

	foreach my $node (@{$tree}) {
		if (exists $node->{'simple'}) {
			my $item = $node->{'simple'};
			$self->{'_regular_handler'}->( $name_prefix . $item->{'name'}->{'__VALUE__'}, $item->{'value'}->{'__VALUE__'} );
		} elsif (exists $node->{'list'}) {
			my $item = $node->{'list'};
			my $name = $item->{'name'}->{'__VALUE__'};
			while ((my $key, my $value) = each %$item) {
				if (ref($value) eq 'ARRAY') {
					# list items here
					foreach my $listitem (@$value) {
						$self->{'_list_handler'}->( $name_prefix . $name, $listitem->{'value'}->{'__VALUE__'} );
					}
					last; # should be only one array of list items
				}
			}
		} else {
			if (exists $node->{'nested'}) {
				my $item = $node->{'nested'};
				$name_prefix .= $item->{'name'}->{'__VALUE__'} . '::';
				$self->_recurse($item->{'statement(s?)'}, $name_prefix);
			}
		}
	}
}

1;

