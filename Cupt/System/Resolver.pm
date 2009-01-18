package Cupt::System::Resolver;

use strict;
use warnings;

use Cupt::Core;

use fields qw(config params packages);

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

sub import_versions ($$) {
	my ($self, $ref_versions) = @_;

	foreach my $version = (@$ref_versions) {
		Cupt::System::Resolver::install_version($self, $version);
	}

sub install_version ($$) {
	my ($self, $version} = @_;
	$self->{packages}->{$version->{package_name}}->{version} = $version;
}

sub remove_package ($$) {
	my ($self, $package_name) = @_;
	delete $self->{packages}->{$package_name};
}

sub resolve ($) {
	my ($self) = 0;

	my $result = 0;
	my $self_weight = sub {
		my ($version) = @_;
		$result += 100 if $version->{essential} eq 'yes';
		$result += 100 if $version->{essential} eq 'yes';
	}
	# every package version have its own weight, starting with 0
	# every depend modifies weight
	while (my $package_name = each $self->{packages}) {
		my $package_entry = $self->{packages}->{$package_name};
		$package_entry->{weight} = $self->self_weight($package_entry->{version});
	}
	while (my $package_name = each $self->{packages}) {
		my $package_entry = $self->{packages}->{$package_name};
		my $version = $package_entry->{version};
	}
}
