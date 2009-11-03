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
package Cupt::Cache::ArchitecturedRelation;

=head1 NAME

Cupt::Cache::ArchitecturedRelation - store info about the relation with architecture specifier

=cut

use 5.10.0;
use strict;
use warnings;

use Exporter qw(import);
use base qw(Cupt::Cache::Relation);

use Cupt::LValueFields qw(3 architectures);

use Cupt::Core;

our @EXPORT_OK = qw(&parse_architectured_relation_line &parse_architectured_relation_expression
		&unarchitecture_relation_expressions);

=head1 METHODS

=head2 new

creates new Cupt::Cache::ArchitecturedRelation object

Parameters:

I<relation_string> - bare relation string (examples: C<nlkt [amd64]>, C<nlkt (E<gt>= 0.3.1)>

=cut

sub new {
	my ($class, $unparsed) = @_;
	my @architectures;
	if ($unparsed =~ m{
		\[ # opening square brace, arch info starter
			( # catch block start
				(?:
					\w | - | \s | , | ! # allowed letters
				)
				+ # multiple times
			) # catch block end
		\] # closing square brace, arch info finisher
		\s* # possible spaces
		$
		}x
	)
	{
		@architectures = split(/ /, $1);
		# cleaning square braces info
		$unparsed =~ s/\s*\[.*//;
	}
	my $self = Cupt::Cache::Relation->new($unparsed);
	bless $self => $class;
	$self->architectures = \@architectures;

	return $self;
}

=head2 stringify

method, returns canonical stringified form of the relation

=cut

sub stringify {
	my ($self) = @_;
	my $result = $self->SUPER::stringify();
	if (scalar @{$self->architectures}) {
		$result .= ' [' . join(' ', @{$self->architectures}) . ']';
	}
	return $result;
}

=head1 FREE SUBROUTINES

=head2 unarchitecture_relation_expressions

free subroutine, converts array of architectured relation expressions to array
of regular relation expressions

Parameters:

I<ref_architectured_relation_expressions> - input array

I<current_architecture> - string, current architecture

=cut

sub unarchitecture_relation_expressions ($$) {
	my ($ref_architectured_relation_expressions, $current_architecture) = @_;

	my $sub_is_appropriate_relation = sub {
		my ($architectured_relation) = @_;
		my @architectures = @{$architectured_relation->architectures};
		return 1 if not scalar @architectures; # no architectures defined

		if ($architectures[0] =~ m/^!/) {
			# negative architecture specifications, see Debian Policy §7.1
			foreach my $architecture (@architectures) {
				$architecture =~ s/^!//;
				if ($current_architecture eq $architecture) {
					# not our case
					return 0;
				}
			}
			return 1;
		} else {
			# positive architecture specifications, see Debian Policy §7.1
			foreach my $architecture (@architectures) {
				if ($current_architecture eq $architecture) {
					# our case
					return 1;
				}
			}
			return 0;
		}
	};

	my @result;

	foreach my $archirectured_relation_expression (@$ref_architectured_relation_expressions) {
		my @group;
		if (ref $archirectured_relation_expression eq 'ARRAY') { # OR relation group
			@group = grep { $sub_is_appropriate_relation->($_) } @$archirectured_relation_expression;
		} else {
			@group = $sub_is_appropriate_relation->($archirectured_relation_expression) ?
					($archirectured_relation_expression) : ();
		}
		bless $_ => 'Cupt::Cache::Relation' for @group;

		if (scalar @group) {
			# repacking group
			if (scalar @group > 1) {
				# if there are some relations in the group
				push @result, \@group;
			} else {
				push @result, $group[0];
			}
		}
	}

	return \@result;
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
		return Cupt::Cache::ArchitecturedRelation->new($architectured_relations[0]);
	} else {
		# 'OR' group of relations
		return [ map { Cupt::Cache::ArchitecturedRelation->new($_) } @architectured_relations ];
	}
}

=head2 parse_architectured_relation_line

free subroutine, parses line of architectured relation expressions, builds array of
L</Relation expression>s and returns reference to it

=cut

sub parse_architectured_relation_line {
	my ($relation_line) = @_;
	my @result;
	while ($relation_line =~ m/(.+?)(?:,\s*|$)/g) {
		push @result, parse_architectured_relation_expression($1);
	}
	return \@result;
}

1;

