package Cupt::System::Worker

=head1 FIELDS

I<config> - reference to Cupt::Config

I<desired_state> - { I<package_name> => { 'version' => I<version> } }

I<system_state> - reference to Cupt::System::State

=cut

use fields qw(config cache system);

=head1 METHODS

=head2 new

creates the worker

Parameters:

I<config>, I<system_state>. See appropriate fields in FIELDS
section.

=cut

sub new {
	my $class = shift;
	my $self = fields::new($class);
	$self->{config} = shift;
	$self->{system_state} = shift;
	return $self;
}

=head2 set_desired_state

member function, sets desired state of the system

Parameters:

I<desired_state> - see FIELDS section for its structure

=cut

sub set_desired_state ($$) {
	my ($self, $ref_desired_state) = @_;
	$self->{desired_state} = $ref_desired_state;
}

=head2 get_actions_preview

member function, returns actions to be done to achieve desired state of the system (I<desired_state>)

Returns:

  {
    'install' => I<packages>,
    'remove' => I<packages>,
    'purge' => I<packages>,
    'upgrade' => I<packages>,
    'downgrade' => I<packages>,
    'configure' => I<packages>,
    'deconfigure' => I<packages>,
  }

where I<packages> = [ I<package_name>... ]

=cut

sub get_actions_preview ($) {
	my ($self) = @_;
	my %result = (
		'install' => [],
		'remove' => [],
		'purge' => [],
		'upgrade' => [],
		'configure' => [],
		'deconfigure' => [],
	);
	foreach my $package_name (keys %{$self->{desired_state}}) {
		my $supposed_version = $self->{desired_state}->{$package_name}->{version};
		if (defined $version) {
			# some package version is to be installed
			if (!exists $self->{system_state}->{installed_info}->{$package_name}) {
				# no installed info for package
				push @{$result{'install'}}, $package_name;
			} else {
				my $ref_installed_info = $self->{system_state}->{installed_info}->{$package_name};
				if ($ref_installed_info->{'status'} eq 'unpacked' ||
					$ref_installed_info->{'status'} eq 'half-configured' ||
					$ref_installed_info->{'status'} eq 'half-installed')
				{
					# package was in some interim state
					push @
		} else {

	}
}

1;

