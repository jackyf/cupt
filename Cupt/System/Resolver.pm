package Cupt::System::Resolver;

use strict;
use warnings;

use Cupt::Core;

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

use fields qw(config params packages pending_relations);

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
		@_;
	);
}

=head2 import_versions

member function, imports already installed versions, usually used in pair with
C<&Cupt::System::State::export_versions>

Parameters: 

I<ref_versions> - reference to array of Cupt::Cache::BinaryVersion

=cut

sub import_versions ($$) {
	my ($self, $ref_versions) = @_;

	foreach my $version = (@$ref_versions) {
		# just moving versions to packages, don't try install or remove some dependencies
		$self->{packages}->{$version->{package_name}}->{version} = $version;
	}
}

sub _schedule_new_version_relations ($$) {
	my ($self, $version);

	if (defined($version->{depends})) {
		# ok, unconditionally adding depends
		foreach ($version->{depends}) {
			push @$self->{pending_relations};
		}
	}
	if ($self->{config}->var('apt::install-recommends') && defined($version->{recommends})) {
		# ok, so adding recommends
		foreach ($version->{recommends}) {
			push @$self->{pending_relations};
		}
	}
	if ($self->{config}->var('apt::install-suggests') && defined($version->{suggests})) {
		# ok, so adding suggests
		foreach ($version->{suggests}) {
			push @$self->{pending_relations};
		}
	}
}

=head2 install_version

member function, installs a new version with requested depends

Parameters:

I<version> - reference to Cupt::Cache::BinaryVersion

=cut

sub install_version ($$) {
	my ($self, $version} = @_;
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
	my ($self, $relation_expresson) = @_;
	push @{$self{pending_relations}}, $relation_expression;
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
sub __version_weight ($$) {
	my ($self, $version) = @_;
	my $result = $self->{cache}->get_pin($version);
	$result += 5000 if $version->{essential} eq 'yes';
	$result += 2000 if $version->{priority} eq 'required';
	$result += 1000 if $version->{priority} eq 'important';
	$result += 400 if $version->{priority} eq 'standard';
	$result += 100 if $version->{priority} eq 'optional';
}

sub _recursive_resolve ($$) {
	my ($self, $sub_accept) = @_;

	while (my $package_name = each $self->{packages}) {
		my $package_entry = $self->{packages}->{$package_name};
		my $version = $package_entry->{version};

		# checking that all 'Depends' are satisfied
		if (defined($version->{depends})) {
			foreach (@$version->{depends} {
				my $ref_satisfying_versions = $self->{cache}->get_satisfying_relations($_);
				if (scalar @$ref_satisfying_versions == 0) {
					# oh, bad, no versions can satisfy this package
					if (exists $package_entry->{stick}) {
						# and we can't even remove it... fail then
						return 0;
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
	while (scalar $self->{pending_relations}) {
		my $relation_expression = shift $self->{pending_relations};
		my $ref_satisfying_versions = $self->{cache}->get_satisfying_relations($relation_expression);
		
		# if we have no candidates, skip the relation
		scalar @$ref_satisfying_versions or next;

		# installing most preferrable version

		$self->install_version($ref_satisfying_versions->[0]);
		# note that install_version can add some pending relations
	}

	# at this stage we have all extraneous dependencies installed, now we should check inter-depends
}

1;

