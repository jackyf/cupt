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

## no critic (RequireInterpolationOfMetachars)
## no critic (ProhibitImplicitNewlines)
my $grammar = q{

	<autotree>

	program:
		  <skip: qr{\s*
		            (?:(?://|\#\s)[^\n]*\n\s*|/\*(?:[^*]+|\*(?!/))*\*/\s*)*
		           }x> statement(s?) eofile { $item[2] }

	statement:
		  simple | nested | list | clear

	simple:
		  name value ';'

	nested:
		  name '{' statement(s?) '}' ';'

	list:
		  name '{' (value ';')(s?) '}' ';'

	clear:
	      '#clear' name ';'

	name:
		  /([\w\/.-]+::)*([\w\/.-]+)/

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

	$self->{'_parser'} = Parse::RecDescent->new($grammar)
		or myinternaldie('bad grammar');

	bless $self, $class;
	return $self;
}

sub parse_file {
	my ($self, $file) = @_;

	open(my $file_handle, $file) or mydie("unable to open file '%s': %s", $file, $!);
	my $text;
	do {
		local $/ = undef;
		$text = <$file_handle>;
	};
	close($file_handle) or mydie("unable to close file '%s': %s", $file, $!);

	defined( my $ref_tree = $self->{'_parser'}->program($text) )
		or mydie("bad config in file '%s'", $file);

	$self->_recurse($ref_tree, '');
	return;
}

sub set_regular_handler {
	my ($self, $value) = @_;
	$self->{'_regular_handler'} = $value;
	return;
}

sub set_list_handler {
	my ($self, $value) = @_;
	$self->{'_list_handler'} = $value;
	return;
}

sub set_clear_handler {
	my ($self, $value) = @_;
	$self->{'_clear_handler'} = $value;
	return;
}

sub __filter_value {
	my ($value) = @_;

	(my $result = $value) =~ s'\\"'"'g;
	return $result;
}

sub _recurse {
	my ($self, $ref_tree, $name_prefix) = @_;

	foreach my $ref_node (@{$ref_tree}) {
		if (exists $ref_node->{'simple'}) {
			my $ref_item = $ref_node->{'simple'};
			my $key = $name_prefix . $ref_item->{'name'}->{'__VALUE__'};
			my $value = __filter_value($ref_item->{'value'}->{'__VALUE__'});
			$self->{'_regular_handler'}->($key, $value);
		} elsif (exists $ref_node->{'list'}) {
			my $ref_item = $ref_node->{'list'};
			my $name = $ref_item->{'name'}->{'__VALUE__'};
			my $key = $name_prefix . $name;
			foreach my $ref_value (values %$ref_item) {
				if (ref $ref_value eq 'ARRAY') {
					# list items here
					foreach my $ref_list_item (@$ref_value) {
						my $value = __filter_value($ref_list_item->{'value'}->{'__VALUE__'});
						$self->{'_list_handler'}->($key, $value);
					}
					last; # should be only one array of list items
				}
			}
		} elsif (exists $ref_node->{'nested'}) {
			my $ref_item = $ref_node->{'nested'};
			my $new_name_prefix = $name_prefix . $ref_item->{'name'}->{'__VALUE__'} . '::';
			$self->_recurse($ref_item->{'statement(s?)'}, $new_name_prefix);
		} elsif (exists $ref_node->{'clear'}) {
			my $ref_item = $ref_node->{'clear'};
			my $name = $ref_item->{'name'}->{'__VALUE__'};
			$self->{'_clear_handler'}->($name_prefix . $name);
		}
	}
	return;
}

1;

