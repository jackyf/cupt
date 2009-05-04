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
package Cupt::System::Resolvers::External;

=head1 NAME

Cupt::System::Resolvers::External - external dependency resolvers wrapper for Cupt

=cut

use 5.10.0;
use strict;
use warnings;

use base qw(Cupt::System::Resolver);

use IPC::Open2;

use Cupt::Core;
use Cupt::Cache::Relation;

use fields qw(_is_installed _actions _strict_relation_expressions);

sub new {
	my $class = shift;
	my $self = fields::new($class);
	$self->SUPER::new(@_);

	$self->{_installed_package_names} = {};
	$self->{_actions} = {};
	$self->{_strict_relation_expressions} = [];
	$self->{_upgrade_all_flag} = 0;

	return $self;
}

sub import_installed_versions ($$) {
	my ($self, $ref_versions);
	foreach my $version (@$ref_versions) {
		$self->{_is_installed}->{$version->{package_name}} = 1;
	}
}

sub install_version ($$) {
	my ($self, $version) = @_;
	my $package_name = $version->{package_name};
	my $action = exists $self->{_is_installed}->{$package_name} ? 'upgrade' : 'install';
	$self->{_actions}->{$package_name}->{$action};
}

sub satisfy_relation_expression ($$) {
	my ($self, $relation_expression) = @_;
	push @{$self->{_strict_relation_expressions}}, $relation_expression;
}

sub remove_package ($$) {
	my ($self, $package_name) = @_;
	$self->{_actions}->{$package_name}->{'remove'};
}

sub upgrade ($) {
	my ($self) = @_;
	$self->{_upgrade_all_flag} = 1;
}

sub resolve ($$) {
	my ($self, $sub_accept) = @_;
	
	eval { 
		my $external_command = $self->config->var('cupt::resolver::external-command');
		defined $external_command or
				myinternaldie("undefined external command");

		open2(\*READ, \*WRITE, $external_command) or
				mydie("unable to create bidirectional pipe for external command '%s'", $external_command);

		$self->_write_cudf_info(\*WRITE);

		close(READ) or
				mydie("unable to close pipe read channel" ;
		close(WRITE);
	};
	if (mycatch()) {
		myerr("external resolver error");
		myredie();
	}
}

sub _write_cudf_info ($$) {
	my ($self, $fh) = @_;
}

1;

