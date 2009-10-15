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

package Cupt::Graph;

use strict;
use warnings;

use List::MoreUtils qw(any all);

use Cupt::LValueFields qw(_vertices _predecessors _successors
		_graph_attributes _edge_attributes);

sub new {
	my $class = shift;
	my $self = bless [] => $class;
	$self->_vertices = {};
	$self->_predecessors = {};
	$self->_successors = {};
	$self->_graph_attributes = {};
	$self->_edge_attributes = {};
	return $self;
}

sub vertices {
	my ($self) = @_;
	return values %{$self->_vertices};
}

sub edges {
	my ($self) = @_;

	my @result;
	foreach my $vertex ($self->vertices()) {
		push @result, map { [ $_, $vertex ] } $self->predecessors($vertex);
	}

	return @result;
}

sub add_vertex {
	my ($self, $vertex) = @_;

	$self->_vertices->{$vertex} = $vertex;
	return;
}

sub delete_vertex {
	my ($self, $vertex) = @_;

	delete $self->_vertices->{$vertex};

	# deleting edges containing vertex
	foreach my $predecessor (@{$self->_predecessors->{$vertex}}) {
		@{$self->_successors->{$predecessor}} = grep { $_ ne $vertex } @{$self->_successors->{$predecessor}};
	}
	delete $self->_predecessors->{$vertex};

	foreach my $successor (@{$self->_successors->{$vertex}}) {
		@{$self->_predecessors->{$successor}} = grep { $_ ne $vertex } @{$self->_predecessors->{$successor}};
	}
	delete $self->_successors->{$vertex};
	return;
}

sub has_edge {
	my ($self, $from_vertex, $to_vertex) = @_;

	return any { $_ eq $from_vertex } @{$self->_predecessors->{$to_vertex}};
}

sub add_edge {
	my ($self, $from_vertex, $to_vertex) = @_;

	if (not $self->has_edge($from_vertex, $to_vertex)) {
		$self->add_vertex($from_vertex);
		$self->add_vertex($to_vertex);
		push @{$self->_predecessors->{$to_vertex}}, $from_vertex;
		push @{$self->_successors->{$from_vertex}}, $to_vertex;
	}
	return;
}

sub delete_edge {
	my ($self, $from_vertex, $to_vertex) = @_;

	$self->_predecessors->{$to_vertex} =
			[ grep { $_ ne $from_vertex } @{$self->_predecessors->{$to_vertex}} ];
	$self->_successors->{$from_vertex} =
			[ grep { $_ ne $to_vertex } @{$self->_successors->{$from_vertex}} ];
	delete $self->_edge_attributes->{$from_vertex,$to_vertex};
	return;
}

sub predecessors {
	my ($self, $vertex) = @_;

	my $ref_predecessors = $self->_predecessors->{$vertex};
	return defined $ref_predecessors ? @$ref_predecessors : ();
}

sub successors {
	my ($self, $vertex) = @_;

	my $ref_successors = $self->_successors->{$vertex};
	return defined $ref_successors ? @$ref_successors : ();
}

sub set_graph_attribute {
	my ($self, $attribute_name, $attribute_value) = @_;

	$self->_graph_attributes->{$attribute_name} = $attribute_value;
	return;
}

sub get_graph_attribute {
	my ($self, $attribute_name) = @_;

	return $self->_graph_attributes->{$attribute_name};
}

sub delete_graph_attributes {
	my ($self) = @_;

	$self->_graph_attributes = {};
	return;
}

sub set_edge_attribute {
	my ($self, $from_vertex, $to_vertex, $attribute_name, $attribute_value) = @_;

	$self->_edge_attributes->{$from_vertex,$to_vertex}->{$attribute_name} = $attribute_value;
	return;
}

sub get_edge_attribute {
	my ($self, $from_vertex, $to_vertex, $attribute_name) = @_;

	return $self->_edge_attributes->{$from_vertex,$to_vertex}->{$attribute_name};
}

sub set_edge_attributes {
	my ($self, $from_vertex, $to_vertex, $ref_attributes) = @_;

	$self->_edge_attributes->{$from_vertex,$to_vertex} = $ref_attributes;
	return;
}

sub get_edge_attributes {
	my ($self, $from_vertex, $to_vertex) = @_;

	return $self->_edge_attributes->{$from_vertex,$to_vertex};
}

sub _dfs_visit {
	my ($self, $vertex, $ref_stages, $ref_finish_times, $ref_time) = @_;

	$ref_stages->{$vertex} = 1;
	++$$ref_time;
	foreach my $to_vertex ($self->successors($vertex)) {
		if ($ref_stages->{$to_vertex} == 0) {
			$self->_dfs_visit($to_vertex, $ref_stages, $ref_finish_times, $ref_time);
		}
	}
	$ref_stages->{$vertex} = 2;
	++$$ref_time;
	push @$ref_finish_times, [ $vertex, $$ref_time ];
	return;
}

sub _dfs {
	my ($self, @vertices) = @_;

	my $mode = (scalar @vertices == 0) ? 'first' : 'second';
	if ($mode eq 'first') {
		# undefined order then
		@vertices = $self->vertices();
	}

	my %stages;
	my @finish_times;

	my @topologically_sorted_components; # for 2nd mode

	foreach my $vertex (@vertices) {
		$stages{$vertex} = 0;
	}
	my $time = 0;

	foreach my $vertex (@vertices) {
		if ($stages{$vertex} == 0) {
			$self->_dfs_visit($vertex, \%stages, \@finish_times, \$time);
			if ($mode eq 'second') {
				push @topologically_sorted_components, [ map { $_->[0] } @finish_times ];
				@finish_times = ();
			}
		}
	}

	if ($mode eq 'first') {
		return @finish_times;
	} else {
		return @topologically_sorted_components;
	}
}

sub topological_sort_of_strongly_connected_components {
	my ($self) = @_;

	my @first_finish_times = $self->_dfs();

	# "transposing" the graph temporarily
	($self->_successors, $self->_predecessors) = ($self->_predecessors, $self->_successors);

	# sort by finish time in descent order
	@first_finish_times = sort { $b->[1] <=> $a->[1] } @first_finish_times;
	my @vertices = map { $_->[0] } @first_finish_times;
	my @result = $self->_dfs(@vertices);

	# "transposing" it to original state
	($self->_successors, $self->_predecessors) = ($self->_predecessors, $self->_successors);

	return @result;
}

1;

