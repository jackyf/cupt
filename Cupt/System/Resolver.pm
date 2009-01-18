package Cupt::System::Resolver;

use strict;
use warnings;

use Cupt::Core;

use fields qw(config params packages);

=head1 OBJECT STRUCTURE

=head2 packages

hash of hashes{I<version>,I<stick>}, where I<version> - a version object,
I<stick> - a boolean flag to indicate can resolver modify this item or no

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

# every package version has a weight
sub __version_weight {
	my ($version) = @_;
	my $result = $self->{cache}->get_pin($version);
	$result += 5000 if $version->{essential} eq 'yes';
	$result += 2000 if $version->{priority} eq 'required';
	$result += 1000 if $version->{priority} eq 'important';
	$result += 400 if $version->{priority} eq 'standard';
	$result += 100 if $version->{priority} eq 'optional';
}

sub resolve ($) {
	my ($self, $sub_accept) = 0;

	while (my $package_name = each $self->{packages}) {
		my $package_entry = $self->{packages}->{$package_name};
		my $version = $package_entry->{version};
	}
}

1;

