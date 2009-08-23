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
package Cupt::Cache::Relation;

=head1 NAME

Cupt::Cache::Relation - store and combine information about package interrelations

=cut

use 5.10.0;
use strict;
use warnings;

use Cupt::LValueFields qw(package_name relation_string version_string);

use Exporter qw(import);

use Cupt::Core;

our @EXPORT_OK = qw(&parse_relation_line &stringify_relation_expressions
		&stringify_relation_expression &parse_relation_expression);

=head1 METHODS

=head2 new

creates new Cupt::Cache::Relation object

Parameters:

I<relation_string> - bare relation string (examples: C<nlkt>, C<nlkt (E<gt>= 0.3.1)>

=cut

sub new {
	my ($class, $unparsed) = @_;
	my $self = [ undef, undef, undef ];

	bless $self => $class;

	if ($unparsed =~ m/^($package_name_regex)/g) {
		# package name is here
		$self->package_name = $1;
	} else {
		# no package name, badly
		mydie("failed to parse package name in relation '%s'", $unparsed);
	}

	if ($unparsed =~ m{
		\G # start at end of previous regex
		\s* # possible spaces
		\( # open relation brace
			(
				>=|=|<=|<<|>>|<|> # relation
			)
			\s* # possible spaces
			(
				$version_string_regex# version
			)
		\) # close relation brace
		$
		}xgc)
	{
		# versioned info is here, assigning
		($self->relation_string, $self->version_string) = ($1, $2);
	} else {
		# no valid versioned info, maybe empty?
		($unparsed =~ m/\G\s*$/g) # empty versioned info, this is also acceptable
			or mydie("failed to parse versioned info in relation '%s'", $unparsed); # what else can we do?..
	}

	return $self;
}

=head2 stringify

method, returns canonical stringified form of the relation

=cut

sub stringify {
	my ($self) = @_;
	my $result = $self->package_name;
	if (defined $self->relation_string) {
		# there is versioned info
		$result .= join('', ' (', $self->relation_string, ' ', $self->version_string, ')');
	}
	return $result;
}

=head2 stringify_relation_expression

free subroutine, returns canonical stringified form of the L</Relation expression>

Parameters:

I<relation_expression> - L</Relation expression> to stringify

=cut

sub stringify_relation_expression ($) {
	my $arg = $_[0];
	if (ref $arg ne 'ARRAY' ) {
		# it's ordinary relation object
		return $arg->stringify();
	} else {
		# it have be an 'OR' group of relations
		return join(' | ', map { $_->stringify() } @$arg);
	}
}

=head2 stringify_relation_expressions

free subroutine, returns canonical stringified form of the L</Relation expression>'s as a line

Parameters:

I<relation_expressions> - [ L</Relation expression> ... ]

=cut

sub stringify_relation_expressions {
	my @relation_strings;
	foreach my $object (@{$_[0]}) {
		push @relation_strings, stringify_relation_expression($object);
	}
	return join(', ', @relation_strings);
}

=head2 satisfied_by

method, returns whether is this relation satisfied with the supplied version of the relation's package

Parameters:

I<version_string> - version string to check

=cut

sub satisfied_by ($$) {
	my ($self, $version_string) = @_;
	if (defined $self->relation_string) {
		# relation is defined, checking
		my $comparison_result = Cupt::Core::compare_version_strings($version_string, $self->version_string);
		given($self->relation_string) {
			when('>=') { return ($comparison_result >= 0) }
			when('<') { return ($comparison_result < 0) }
			when('<<') { return ($comparison_result < 0) }
			when('<=') { return ($comparison_result <= 0) }
			when('=') { return ($comparison_result == 0) }
			when('>') { return ($comparison_result > 0) }
			when('>>') { return ($comparison_result > 0) }
		}
	}
	# no versioned info, so return true
	return 1;
}

=head2 parse_relation_expression

free subroutine, parses relation expression in string form, builds L</Relation expression> and returns it

=cut

sub parse_relation_expression ($) {
	my ($relation_expression_string) = @_;

	# looking for OR groups
	my @relations = split / ?\| ?/, $relation_expression_string;
	if (scalar @relations == 1) {
		# ordinary relation
		return new Cupt::Cache::Relation($relations[0]);
	} else {
		# 'OR' group of relations
		return [ map { new Cupt::Cache::Relation($_) } @relations ];
	}
}

=head2 parse_relation_line

free subroutine, parses line of relation expressions, builds array of
L</Relation expression>s and returns reference to it

=cut

sub parse_relation_line {
	# my $relation_line = $_[0] 
	# or myinternaldie("relation line is not defined");

	my @result;
	while ($_[0] =~ m/(.+?)(?:,\s*|$)/g) {
		push @result, parse_relation_expression($1);
	}
	return \@result;
}

=head1 Relation expression

Can be in two forms:

=over

=item *

I<relation>, which stands for single relation

=item *

[ I<relation> ... ], which stands for relation OR group, where only one of relations need to be satisfied

=back

where

I<relation> - Cupt::Cache::Relation object

=head1 SEE ALSO

Debian Policy 7.1

=cut

1;

