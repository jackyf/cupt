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
use Cupt::Cache::Package;
use Cupt::Cache::Relation qw(stringify_relation_expressions);
use Cupt::LValueFields qw(2 _is_installed _upgrade_all_flag _actions
		_strict_satisfy_relation_expressions _strict_unsatisfy_relation_expressions);

my $_dummy_package_name = 'dummy-package-name';

sub new {
	my $class = shift;
	my $self = bless [] => $class;
	$self->SUPER::new(@_);

	$self->_is_installed = {};
	$self->_actions = {};
	$self->_strict_satisfy_relation_expressions = [];
	$self->_strict_unsatisfy_relation_expressions = [];
	$self->_upgrade_all_flag = 0;

	return $self;
}

sub import_installed_versions ($$) {
	my ($self, $ref_versions) = @_;
	foreach my $version (@$ref_versions) {
		$self->_is_installed->{$version->package_name} = 1;
	}
	return;
}

sub install_version ($$) {
	my ($self, $version) = @_;
	my $package_name = $version->package_name;
	$self->_actions->{$package_name}->{'action'} = 'install';
	$self->_actions->{$package_name}->{'version_string'} = $version->version_string;
	return;
}

sub satisfy_relation_expression ($$) {
	my ($self, $relation_expression) = @_;
	push @{$self->_strict_satisfy_relation_expressions}, $relation_expression;
	return;
}

sub unsatisfy_relation_expression ($$) {
	my ($self, $relation_expression) = @_;
	push @{$self->_strict_unsatisfy_relation_expressions}, $relation_expression;
	return;
}

sub remove_package ($$) {
	my ($self, $package_name) = @_;
	$self->_actions->{$package_name}->{'action'} = 'remove';
	return;
}

sub upgrade ($) {
	my ($self) = @_;
	$self->_upgrade_all_flag = 1;
	return;
}

sub resolve ($$) {
	my ($self, $sub_accept) = @_;

	eval {
		my $external_command = $self->config->get_string('cupt::resolver::external-command');
		defined $external_command or
				myinternaldie('undefined external command');

		local $SIG{PIPE} = sub { mydie('external command unexpectedly closed the pipe'); };

		open2(\*READ, \*WRITE, $external_command) or
				mydie("unable to create bidirectional pipe for external command '%s': %s", $external_command, $!);

		$self->_write_dudf_info(\*WRITE);
		close(WRITE) or
				mydie('unable to close pipe write channel: %s', $!);

		my $resolve_result = $self->_read_dudf_result(\*READ);
		close(READ) or
				mydie('unable to close pipe read channel', $!);

		my $user_answer = $sub_accept->($resolve_result);
		if (defined $user_answer && $user_answer) {
			return 1;
		} else {
			return 0;
		}
	};
	if (mycatch()) {
		myerr('external resolver error');
		myredie();
	}
	return;
}

sub _write_dudf_info ($$) {
	my ($self, $fh) = @_;

	my $sub_strip_circle_braces = sub {
		$_[0] =~ tr/()//d;
		return $_[0];
	};

	# writing package info
	foreach my $package_name ($self->cache->get_binary_package_names()) {
		my $package = $self->cache->get_binary_package($package_name);
		foreach my $version (@{$package->get_versions()}) {
			say { $fh } 'Package: ' . $package_name;
			say { $fh } 'Version: ' . $version->version_string;
			say { $fh } 'Pin-Priority: ' . $self->cache->get_original_apt_pin($version);

			do { # print strict dependencies
				my @depends_relation_expressions;

				push @depends_relation_expressions, @{$version->pre_depends};
				push @depends_relation_expressions, @{$version->depends};

				if (scalar @depends_relation_expressions) {
					print { $fh } 'Depends: ';
					say { $fh } $sub_strip_circle_braces->(stringify_relation_expressions(\@depends_relation_expressions));
				}
			};

			do { # print conflicting packages
				my @conflicts_relation_expressions;

				push @conflicts_relation_expressions, @{$version->conflicts};
				push @conflicts_relation_expressions, @{$version->breaks};

				if (scalar @conflicts_relation_expressions) {
					print { $fh } 'Conflicts: ';
					say { $fh } $sub_strip_circle_braces->(stringify_relation_expressions(\@conflicts_relation_expressions));
				}
			};

			do { # print provides
				my $ref_provides_package_names = $version->provides;
				if (scalar @$ref_provides_package_names) {
					say { $fh } 'Provides: ' . join(', ', @$ref_provides_package_names);
				}
			};

			if ($version->is_installed()) {
				say { $fh } 'Installed: true';
				if ($self->config->get_bool('cupt::resolver::no-remove') and
						not $self->cache->is_automatically_installed($package_name))
				{
					say { $fh } 'Keep: package';
				}
			}

			# end of entry
			say { $fh } '';
		}
	}
	if (scalar @{$self->_strict_satisfy_relation_expressions} ||
		scalar @{$self->_strict_unsatisfy_relation_expressions})
	{
		# writing dummy package entry
		say { $fh } "Package: $_dummy_package_name";
		say { $fh } 'Version: 1';
		if (scalar @{$self->_strict_satisfy_relation_expressions}) {
			print { $fh } 'Depends: ';
			say $fh $sub_strip_circle_braces->(stringify_relation_expressions(
					$self->_strict_satisfy_relation_expressions));
		}
		if (scalar @{$self->_strict_unsatisfy_relation_expressions}) {
			print { $fh } 'Conflicts: ';
			say { $fh } $sub_strip_circle_braces->(stringify_relation_expressions(
					$self->_strict_unsatisfy_relation_expressions));
		}
		say { $fh } '';
	}

	# writing problems
	say { $fh } 'Problem: source: Debian/DUDF';

	if ($self->_upgrade_all_flag) {
		say { $fh } 'Upgrade: ' . join(' ', keys %{$self->_is_installed});
	}

	my @package_names_to_remove;
	my @strings_to_install;
	foreach my $package_name (keys %{$self->_actions}) {
		my $package_entry = $self->_actions->{$package_name};
		if ($package_entry->{'action'} eq 'remove') {
			push @package_names_to_remove, $package_name;
		} elsif ($package_entry->{'action'} eq 'install') {
			push @strings_to_install, "$package_name = $package_entry->{'version_string'}";
		}
	}

	if (scalar @{$self->_strict_satisfy_relation_expressions} ||
		scalar @{$self->_strict_unsatisfy_relation_expressions})
	{
		push @strings_to_install, $_dummy_package_name;
	}

	if (scalar @package_names_to_remove) {
		say { $fh } 'Remove: ' . join(', ', @package_names_to_remove);
	}
	if (scalar @strings_to_install) {
		say { $fh } 'Install: ' . join(', ', @strings_to_install);
	}

	# at last!
	say { $fh } '';
	return;
}

sub _read_dudf_result ($$) {
	my ($self, $fh) = @_;

	my %result;
	foreach my $installed_package_name (keys %{$self->_is_installed}) {
		$result{$installed_package_name}->{'version'} = undef;
		$result{$installed_package_name}->{'manually_installed'} = 0;
		$result{$installed_package_name}->{'reasons'} = [];
	}

	do {
		my $previous_delimiter = $/;
		local $/ = "\n\n"; # set by-entry mode
		while (my $entry = <$fh>) {
			local $/ = $previous_delimiter;
			my ($package_name, $version_string) = ($entry =~ m/^package: (.*?)\n^version: (.*?)$/m) or
					mydie("wrong resolve result package entry '%s'", $entry);
			$package_name =~ m/^$package_name_regex$/ or
					mydie("wrong package name '%s'", $package_name);
			$version_string =~ m/^$version_string_regex$/ or
					mydie("wrong version string '%s'", $version_string);
			my $package = $self->cache->get_binary_package($package_name) or
					mydie("wrong resolve result package name '%s'", $package_name);
			my $version = $package->get_specific_version($version_string) or
					mydie("wrong resolve result version string '%s' for package '%s'", $version_string, $package_name);

			$result{$package_name}->{'version'} = $version;
			$result{$package_name}->{'manually_installed'} = (exists $self->_actions->{$package_name});
			$result{$package_name}->{'reasons'} = []; # no defined yet
		}
	};

	return \%result;
}

1;

