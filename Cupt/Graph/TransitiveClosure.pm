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

package Cupt::Graph::TransitiveClosure;

use strict;
use warnings;

use Cupt::Graph;

use Cupt::LValueFields qw(_graph);

sub new {
	my ($class, $graph) = @_;
	my $self = bless [] => $class;
	$self->_graph = $graph;
	return $self;
}

sub is_reachable {
	my ($self, $from_vertex, $to_vertex) = @_;

	my @current_vertices = $from_vertex;

	my %seen_vertices;

	while (my $current_vertex = shift @current_vertices) {
		if ($current_vertex eq $to_vertex) {
			return 1;
		}
		if (not $seen_vertices{$current_vertex}++) {
			push @current_vertices, $self->_graph->successors($from_vertex);
		}
	}
	return 0;
}

sub path_vertices {
	my ($self, $from_vertex, $to_vertex) = @_;

	my @current_vertices_and_paths = ([ $from_vertex, [ $from_vertex ] ]);

	my %seen_vertices;

	while (my ($current_vertex, $ref_current_path) = @{shift @current_vertices_and_paths}) {
		if ($current_vertex eq $to_vertex) {
			return (@$ref_current_path, $to_vertex);
		}
		if (not $seen_vertices{$current_vertex}++) {
			foreach my $successor_vertex ($self->_graph->successors($from_vertex)) {
				my @new_path = (@$ref_current_path, $successor_vertex);
				push @current_vertices_and_paths, [ $successor_vertex, \@new_path ];
			}
		}
	}
	return undef;
}

1;

