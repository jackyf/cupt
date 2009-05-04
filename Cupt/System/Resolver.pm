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
package Cupt::System::Resolver;

=head1 NAME

Cupt::System::Resolver - base class for Cupt resolvers

=cut

use 5.10.0;
use strict;
use warnings;

use Cupt::Core;

use fields qw(_config _cache);

=head1 METHODS

=head2 new

creates new Cupt::System::Resolver object

Parameters: 

I<config> - reference to L<Cupt::Config|Cupt::Config>

I<cache> - reference to L<Cupt::Cache|Cupt::Cache>

=cut

sub new {
	my $class = shift;
	my $self = fields::new($class);

	$self->{_config} = shift;
	$self->{_cache} = shift;

	return $self;
}

=head2 cache

returns reference to L<Cupt::Cache|Cupt::Cache>

=cut

sub cache {
	my ($self) = @_;
	return $self->{_cache};
}

=head2 config

returns reference to L<Cupt::Config|Cupt::Config>

=cut

sub config {
	my ($self) = @_;
	return $self->{_config};
}

=head2 import_installed_versions

method, imports already installed versions, usually used in pair with
L<&Cupt::System::State::export_installed_versions|Cupt::System::State/export_installed_versions>

Should be re-implemented by derived classes.

Parameters:

I<ref_versions> - reference to array of L<Cupt::Cache::BinaryVersion|Cupt::Cache::BinaryVersion>

=cut

sub import_installed_versions ($$) {
	my ($self, $ref_versions) = @_;
	# stub
}

=head2 install_version

method, installs a new version with requested dependencies

Should be re-implemented by derived classes.

Parameters:

I<version> - reference to L<Cupt::Cache::BinaryVersion|Cupt::Cache::BinaryVersion>

=cut

sub install_version ($$) {
	my ($self, $version) = @_;
	# stub
}

=head2 satisfy_relation

method, installs all needed versions to satisfy L<relation expression|Cupt::Cache::Relation/Relation expression>

Should be re-implemented by derived classes.

Parameters:

I<relation_expression> - see L<Cupt::Cache::Relation/Relation expression>

=cut

sub satisfy_relation_expression ($$) {
	my ($self, $relation_expression) = @_;
	# stub
}

=head2 remove_package

method, removes a package

Should be re-implemented by derived classes.

Parameters:

I<package_name> - string, name of package to remove

=cut

sub remove_package ($$) {
	my ($self, $package_name) = @_;
	# stub
}

=head2 upgrade

method, schedule upgrade of as much packages in system as possible

Should be re-implemented by derived classes.

=cut

sub upgrade ($) {
	my ($self) = @_;
	# stub
}

=head2 resolve

method, finds a solution for requested actions

Should be re-implemented by derived classes.

Parameters:

I<sub_accept> - reference to subroutine which has to return true if solution is
accepted, false if solution is rejected, undef if user abandoned further searches

=cut

sub resolve ($$) {
	my ($self, $sub_accept) = @_;
	# stub
}

1;

