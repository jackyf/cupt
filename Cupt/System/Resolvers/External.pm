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
use Cupt::Cache;
use Cupt::Cache::Pkg;
use Cupt::Cache::Relation qw(stringify_relation_expressions);

use fields qw(_is_installed _upgrade_all_flag _actions _strict_relation_expressions);

sub new {
	my $class = shift;
	my $self = fields::new($class);
	$self->SUPER::new(@_);

	$self->{_is_installed} = {};
	$self->{_actions} = {};
	$self->{_strict_relation_expressions} = [];
	$self->{_upgrade_all_flag} = 0;

	return $self;
}

sub import_installed_versions ($$) {
	my ($self, $ref_versions) = @_;
	foreach my $version (@$ref_versions) {
		$self->{_is_installed}->{$version->{package_name}} = 1;
	}
}

sub install_version ($$) {
	my ($self, $version) = @_;
	my $package_name = $version->{package_name};
	my $action = exists $self->{_is_installed}->{$package_name} ? 'upgrade' : 'install';
	$self->{_actions}->{$package_name} = $action;
}

sub satisfy_relation_expression ($$) {
	my ($self, $relation_expression) = @_;
	push @{$self->{_strict_relation_expressions}}, $relation_expression;
}

sub remove_package ($$) {
	my ($self, $package_name) = @_;
	$self->{_actions}->{$package_name} = 'remove';
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
				mydie("unable to close pipe read channel");
		close(WRITE);
	};
	if (mycatch()) {
		myerr("external resolver error");
		myredie();
	}
}

sub _write_cudf_info ($$) {
	my ($self, $fh) = @_;

	# writing package info
	foreach my $package (values %{$self->cache->get_binary_packages()}) {
		foreach my $version (@{$package->versions()}) {
			my $package_name = $version->{package_name};
			say $fh "Package: " . $package_name;
			say $fh "Version: " . $version->{version_string};

			do { # print strict dependencies
				my @depends_relation_expressions;

				push @depends_relation_expressions, @{$version->{pre_depends}};
				push @depends_relation_expressions, @{$version->{depends}};

				if (scalar @depends_relation_expressions) {
					print $fh "Depends: ";
					say $fh stringify_relation_expressions(\@depends_relation_expressions);
				}
			};

			do { # print conflicting packages
				print $fh "Conflicts: ";

				# cannot install the same package multiple times
				my @conflicts_relation_expressions = (new Cupt::Cache::Relation($package_name));

				push @conflicts_relation_expressions, @{$version->{conflicts}};
				push @conflicts_relation_expressions, @{$version->{breaks}};

				say $fh stringify_relation_expressions(\@conflicts_relation_expressions);
			};

			do { # print provides
				my $ref_provides_package_names = $version->{provides};
				if (scalar @$ref_provides_package_names) {
					say $fh "Provides: " . join(", ", @$ref_provides_package_names);
				}
			};

			if ($version->is_installed()) {
				say $fh "Installed: true";
				if ($self->config->var('cupt::resolver::no-remove') &&
						not $self->cache->is_automatically_installed($package_name))
				{
					say $fh "Keep: package";
				}
			}

			# end of entry
			say $fh "";
		}
	}

	# writing problems
	say $fh "Problem: source: Debian/DUDF";

	if ($self->{_upgrade_all_flag}) {
		say $fh "Upgrade: " . join(" ", keys %{$self->{_is_installed}});
	}

	do { # packages that are to be removed
		my @package_names_to_remove;
		foreach my $package_name (keys %{$self->{_actions}}) {
			if ($self->{_actions}->{$package_name} eq 'remove') {
				push @package_names_to_remove, $package_name;
			}
		}

		if (scalar @package_names_to_remove) {
			say $fh "Remove: " . join(", ", @package_names_to_remove);
		}
	};
}

1;

