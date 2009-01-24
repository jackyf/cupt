package Cupt::System::Resolver;

use 5.10.0;
use strict;
use warnings;

use Cupt::Core;
use Cupt::Cache::Relation qw(stringify_relation_or_group);

=head1 FIELDS

=head2 config

stores reference to config (Cupt::Config)

=head2 cache

stores reference to cache (Cupt::Cache)

=head2 packages

hash { I<package_name> => {S<< 'version' => I<version> >>, S<< 'stick' => I<stick> >>} }

where:

I<package_name> - name of binary package

I<version> - reference to Cupt::Cache::BinaryVersion, can
be undefined if package has to be removed

I<stick> - a boolean flag to
indicate can resolver modify this item or not

=head2 pending_relations

array of relations which are to be satisfied by final resolver, used for
filling depends, recommends (optionally), suggests (optionally) of requested
packages, or for satisfying some requested relations

=cut

use fields qw(config cache params packages pending_relations);

=head1 METHODS

=head2 new

creates new resolver

Parameters: 

I<config> - reference to Cupt::Config

I<cache> - reference to Cupt::Cache

=cut

sub new {
	my $class = shift;
	my $self = fields::new($class);

	# common apt config
	$self->{config} = shift;

	$self->{cache} = shift;

	# resolver params
	$self->{params} = (
		@_
	);

	$self->{pending_relations} = [];

	return $self;
}

=head2 import_versions

member function, imports already installed versions, usually used in pair with
C<&Cupt::System::State::export_installed_versions>

Parameters: 

I<ref_versions> - reference to array of Cupt::Cache::BinaryVersion

=cut

sub import_versions ($$) {
	my ($self, $ref_versions) = @_;

	foreach my $version (@$ref_versions) {
		# just moving versions to packages, don't try install or remove some dependencies
		$self->{packages}->{$version->{package_name}}->{version} = $version;
	}
}

sub _schedule_new_version_relations ($$) {
	my ($self, $version) = @_;

	if (defined($version->{depends})) {
		# ok, unconditionally adding depends
		foreach (@{$version->{depends}}) {
			$self->satisfy_relation($_);
		}
	}
	if ($self->{config}->var('apt::install-recommends') && defined($version->{recommends})) {
		# ok, so adding recommends
		foreach (@{$version->{recommends}}) {
			$self->satisfy_relation($_);
		}
	}
	if ($self->{config}->var('apt::install-suggests') && defined($version->{suggests})) {
		# ok, so adding suggests
		foreach (@{$version->{suggests}}) {
			$self->satisfy_relation($_);
		}
	}
}

=head2 install_version

member function, installs a new version with requested depends

Parameters:

I<version> - reference to Cupt::Cache::BinaryVersion

=cut

sub install_version ($$) {
	my ($self, $version) = @_;
	$self->{packages}->{$version->{package_name}}->{version} = $version;

	$self->_schedule_new_version_relations($version);
}

=head2 satisfy_relation

member function, installs all needed versions to satisfy relation or relation group

Parameters:

I<relation_expression> - reference to Cupt::Cache::Relation, or relation OR
group (see documentation for Cupt::Cache::Relation for the info about OR
groups)

=cut

sub satisfy_relation ($$) {
	my ($self, $relation_expression) = @_;

	my $ref_satisfying_versions = $self->{cache}->get_satisfying_versions($relation_expression);
	if (!__is_version_array_intersects_with_packages($ref_satisfying_versions, $self->{packages})) {
		# if relation is not satisfied
		if ($self->{config}->var('debug::resolver')) {
			my $message = "auto-installing relation '";
			$message .= stringify_relation_or_group($relation_expression);
			$message .= "'";
			mydebug($message);
		}
		push @{$self->{pending_relations}}, $relation_expression;
	}
}

=head2 remove_package

member function, removes a package

Parameters:

I<package_name> - string, name of package to remove

=cut

sub remove_package ($$) {
	my ($self, $package_name) = @_;
	delete $self->{packages}->{$package_name};
}

# every package version has a weight
sub _version_weight ($$) {
	my ($self, $version) = @_;
	my $result = $self->{cache}->get_pin($version);
	$result += 5000 if defined($version->{essential});
	$result += 2000 if $version->{priority} eq 'required';
	$result += 1000 if $version->{priority} eq 'important';
	$result += 400 if $version->{priority} eq 'standard';
	$result += 100 if $version->{priority} eq 'optional';
}

sub __is_version_array_intersects_with_packages ($$) {
	my ($ref_versions, $ref_packages) = @_;

	foreach my $version (@$ref_versions) {
		exists $ref_packages->{$version->{package_name}} or next;

		my $installed_version = $ref_packages->{$version->{package_name}}->{version};
		defined $installed_version or next;
		
		return 1 if $version->{version} eq $installed_version->{version};
	}
	return 0;
}

sub _recursive_resolve ($$$) {
	my ($self, $sub_accept, $recurse_level) = @_;

	# for debug purposes
	return 1 if $recurse_level > 7;

	my $sub_mydebug_wrapper = sub {
		mydebug('  ' x $recurse_level . "@_");
	};

	# debugging subroutine
	my $sub_debug_version_change = sub {
		my ($package_name, $supposed_version, $original_version) = @_;

		my $old_version_string = defined($original_version) ? $original_version->{version} : '<not installed>';
		my $new_version_string = defined($supposed_version) ? $supposed_version->{version} : '<not installed>';
		my $message = "package '$package_name': trying '$old_version_string' -> '$new_version_string'";
		$sub_mydebug_wrapper->($message);
	};
	
	# [ package_name, version ]
	my @possible_actions;
	my $check_failed = 0;


	MAIN_LOOP:
	my $package_name;
	foreach (keys %{$self->{packages}}) {
		my $package_name = $_;
		my $package_entry = $self->{packages}->{$package_name};
		my $version = $package_entry->{version};

		# checking that all 'Depends' are satisfied
		if (defined($version->{depends})) {
			foreach (@{$version->{depends}}) {
				# check if relation is already satisfied
				my $ref_satisfying_versions = $self->{cache}->get_satisfying_versions($_);
				if (__is_version_array_intersects_with_packages($ref_satisfying_versions, $self->{packages})) {
					# good, nothing to do
				} else {
					# for resolving we can do:

					# install one of versions package needs
					foreach my $satisfying_version (@$ref_satisfying_versions) {
						if (!exists $self->{packages}->{$satisfying_version->{package_name}}->{stick}) {
							push @possible_actions, [ $satisfying_version->{package_name}, $satisfying_version ];
						}
					}

					if (!exists $package_entry->{stick}) {
						# change version of the package
						my $other_package = $self->{cache}->get_binary_package($package_name);
						foreach my $other_version (@{$other_package->versions()}) {
							# don't try existing version
							next if $other_version->{version} eq $version->{version};

							push @possible_actions, [ $package_name, $other_version ];
						}

						# remove the package
						push @possible_actions, [ $package_name, undef ];
					}

					if ($self->{config}->var('debug::resolver')) {
						my $stringified_relation = stringify_relation_or_group($_);
						$sub_mydebug_wrapper->("problem: package '$package_name': " . 
								"unsatisfied depends '$stringified_relation'");
					}
					$check_failed = 1;
					last;
				}
			}
		}
		last if $check_failed;

		# checking that all 'Conflicts' are not satisfied
		if (defined($version->{conflicts})) {
			foreach (@{$version->{conflicts}}) {
				# check if relation is accidentally satisfied
				my $ref_satisfying_versions = $self->{cache}->get_satisfying_versions($_);

				if (!__is_version_array_intersects_with_packages($ref_satisfying_versions, $self->{packages})) {
					# good, nothing to do
				} else {
					# so, this can conflict... check it deeper on the fly
					foreach my $satisfying_version (@$ref_satisfying_versions) {
						my $other_package_name = $satisfying_version->{package_name};

						# package can't conflict with itself
						$other_package_name ne $package_name or next;

						# is the package installed?
						exists $self->{packages}->{$other_package_name} or next;

						my $other_package_entry = $self->{packages}->{$other_package_name};

						# does the stick exists?
						!exists $other_package_entry->{stick} or next;
						# does the package have an installed version?
						defined($other_package_entry->{version}) or next;
						# is this our version?
						$other_package_entry->{version}->{version} eq $satisfying_version->{version} or next;

						$check_failed = 1;
						# yes... so change it
						my $other_package = $self->{cache}->get_binary_package($package_name);
						foreach my $other_version (@{$other_package->versions()}) {
							# don't try existing version
							next if $other_version->{version} eq $satisfying_version->{version};

							push @possible_actions, [ $other_package_name, $other_version ];
						}

						# or remove it
						push @possible_actions, [ $other_package_name, undef ];
					}

					if ($check_failed) {
						if (!exists $package_entry->{stick}) {
							# change version of the package
							my $package = $self->{cache}->get_binary_package($package_name);
							foreach my $other_version (@{$package->versions()}) {
								# don't try existing version
								next if $other_version->{version} eq $version->{version};

								push @possible_actions, [ $package_name, $other_version ];
							}
							
							# remove the package
							push @possible_actions, [ $package_name, undef ];
						}

						if ($self->{config}->var('debug::resolver')) {
							my $stringified_relation = stringify_relation_or_group($_);
							$sub_mydebug_wrapper->("problem: package '$package_name': " . 
									"satisfied conflicts '$stringified_relation'");
						}
						last;
					}
				}
			}
		}
		last if $check_failed;
	}

	if ($check_failed) {
		# some depends are not satisfied, try to fix them

		# firstly rank all solutions
		foreach (@possible_actions) {
			my $package_name = $_->[0];
			my $supposed_version = $_->[1];
			my $original_version = exists $self->{packages}->{$package_name} ?
					$self->{packages}->{$package_name}->{version} : undef;

			my $supposed_version_weight = defined($supposed_version) ? $self->_version_weight($supposed_version) : 0;
			my $original_version_weight = defined($original_version) ? $self->_version_weight($original_version) : 0;

			# 3rd field in the structure will be "profit" of the change
			push @$_, $supposed_version_weight - $original_version_weight;
		}

		# sort them by "rank"
		@possible_actions = sort { $b->[2] <=> $a->[2] } @possible_actions;

		# apply by one
		foreach (@possible_actions) {
			my $package_name = $_->[0];
			my $supposed_version = $_->[1];
			my $ref_package_entry = $self->{packages}->{$package_name};
			my $original_version = $ref_package_entry->{version};

			if ($self->{config}->var('debug::resolver')) {
				$sub_debug_version_change->($package_name, $supposed_version, $original_version);
			}

			# set stick for change for the time on underlying solutions
			$ref_package_entry->{stick} = 1;
			$ref_package_entry->{version} = $supposed_version;

			if ($self->_recursive_resolve($sub_accept, $recurse_level+1)) {
				# some underlying solution has been accepted, moving up
				return 1;
			}

			# otherwise remove it and try next...
			$ref_package_entry->{version} = $original_version;
			delete $ref_package_entry->{stick};
		}
		return 0;
	} else {
		# suggest found solution
		if ($sub_accept->(map { defined($_->{version}) ? $_->{version} : () } $self->{packages})) {
			# yeah, this is end of our tortures
			return 1;
		} else {
			# caller hasn't accepted this solution, well, go next...
			return 0;
		}
	}
}

=head2 resolve

member function, finds a solution for requested actions

Parameters:

I<sub_accept> - reference to subroutine which have return true if solution is
accepted, and false otherwise

Returns:

true if some solution was found and accepted, false otherwise

=cut

sub resolve ($$) {
	my ($self, $sub_accept) = @_;

	# unwinding relations
	while (scalar @{$self->{pending_relations}}) {
		my $relation_expression = shift @{$self->{pending_relations}};
		my $ref_satisfying_versions = $self->{cache}->get_satisfying_versions($relation_expression);
		
		# if we have no candidates, skip the relation
		scalar @$ref_satisfying_versions or next;

		# installing most preferrable version

		my $version_to_install = $ref_satisfying_versions->[0];
		$self->install_version($version_to_install);
		# note that install_version can add some pending relations

		if ($self->{config}->var('debug::resolver')) {
			mydebug("selected package '%s', version '%s' for relation expression '%s'",
					$version_to_install->{package_name},
					$version_to_install->{version},
					stringify_relation_or_group($relation_expression)
			);
		}
	}

	# at this stage we have all extraneous dependencies installed, now we should check inter-depends
	return $self->_recursive_resolve($sub_accept, 0);
}

1;

