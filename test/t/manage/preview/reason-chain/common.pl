use strict;
use warnings FATAL => 'all';

sub get_reason_chain {
	my ($cupt, $package_to_install, $package_down_chain) = @_;

	my $input = "rc\n$package_down_chain\n";
	my $output = `echo '$input' 2>&1 | $cupt -s install $package_to_install 2>&1`;

	my ($result) = ($output =~ m/to show reason chain.*?\n((?:$package_down_chain).+?)\n\n/s);

	if (not defined $result) {
		return "Reason chain extraction failed, full output:\n" . $output;
	}

	return $result;
}

1;

