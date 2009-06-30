#***************************************************************************
#*   Copyright (C) 2009 by Eugene V. Lyubimkin                             *
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
package Cupt::Cache::ArchirecturedRelation;

=head1 NAME

Cupt::Cache::ArchirecturedRelation - store info about the relation with architecture specifier

=cut

use 5.10.0;
use strict;
use warnings;

use base qw(Cupt::Cache::Relation);

use constant {
	REL_ARCHITECTURES => 3,
};

use Exporter qw(import);

use Cupt::Core;

our @EXPORT_OK = qw(&parse_architectured_relation_line &parse_architectured_relation_expression);

=head1 METHODS

=head2 new

creates new Cupt::Cache::ArchitecturedRelation object

Parameters:

I<relation_string> - bare relation string (examples: C<nlkt [amd64]>, C<nlkt (E<gt>= 0.3.1)>

=cut

sub new {
	my ($class, $unparsed) = @_;
	my @architectures;
	if ($unparsed =~ m/
		\[ # opening square brace, arch info starter
			( # catch block start
				(?:
					\w | \s | , | ! # allowed letters
				)
				+ # multiple times
			) # catch block end
		\] # closing square brace, arch info finisher
		\s* # possible spaces
		$
		/x
	)
	{
		@architectures = split(/\s*,\s*/, $1);
		# cleaning square braces info
		$unparsed =~ s/\[.*//;
	}
	my $self;
	bless $self => $class;
	$self->SUPER::new($unparsed);
	$self->[REL_ARCHITECTURES] = \@architectures;

	return $self;
}

=head2 architectures

method, architectures field accessor and mutator

=cut

sub architectures ($) {
	return $_[0]->[REL_ARCHITECTURES];
}

=head2 stringify

method, returns canonical stringified form of the relation

=cut

sub stringify {
	my ($self) = @_;
	my $result = $self->SUPER::stringify();
	if (scalar @{$self->[REL_ARCHITECTURES]}) {
		$result .= '[' . join(', ', @{$self->[REL_ARCHITECTURES]}) . ']';
	}
	return $result;
}

=head2 parse_architectured_relation_expression

free subroutine, parses architectured relation expression in string form,
builds L</Relation expression> and returns it

=cut

sub parse_architectured_relation_expression ($) {
	my ($architectured_relation_expression_string) = @_;

	# looking for OR groups
	my @architectured_relations = split / ?\| ?/, $architectured_relation_expression_string;
	if (scalar @architectured_relations == 1) {
		# ordinary relation
		return new Cupt::Cache::ArchitecturedRelation($architectured_relations[0]);
	} else {
		# 'OR' group of relations
		return [ map { new Cupt::Cache::ArchitecturedRelation($_) } @architectured_relations ];
	}
}

=head2 parse_architectured_relation_line

free subroutine, parses line of architectured relation expressions, builds array of
L</Relation expression>s and returns reference to it

=cut

sub parse_architectured_relation_line {
	my @result;
	while ($_[0] =~ m/(.+?)(?:,\s*|$)/g) {
		push @result, parse_architectured_relation_expression($1);
	}
	return \@result;
}

1;

