use TestCupt;
use Test::More tests => 14*3;

use strict;
use warnings;

eval get_inc_code('../common');

my $cupt = TestCupt::setup();

sub test_group {
	my ($template_option) = @_;

	my $value = '112';
	for my $subst (qw(aux arbit POIUUUq531)) {
		(my $option = $template_option) =~ s/\Q*\E/$subst/g;

		my $uppercased_option = uc($option);
		my $output = stdall("$cupt config-dump -o $uppercased_option=$value");

		test_option($output, lc($option), $value);
	}
}

test_group('acquiRE::*::*::proxy');
test_group('acquiRE::*::PROxy::*');
test_group('acquiRE::*::PROxy');
test_group('acquiRE::*::*::dl-limit');
test_group('acquiRE::*::DL-limit::*');
test_group('acquiRE::*::DL-limit');
test_group('acquiRE::*::*::timeout');
test_group('acquiRE::*::TIMeout::*');
test_group('acquiRE::*::TIMeout');
test_group('dpkg::TOOLS::OPtions::*');
test_group('dpkg::TOOLS::OPtions::*::*');
test_group('cupt::DOWNLOADEr::protocols::*::priority');
test_group('cupt::DOWNLOADEr::protocols::*::methods');
test_group('cupt::DOWNLOADEr::protocols::*::methods::*::priority');

