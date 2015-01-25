use TestCupt;
use Test::More tests => 4;

use strict;
use warnings;

my $cupt;
eval get_inc_code('common');

sub setup_aux_between_ess {
	my ($ess_is_essential, $additional_installed) = @_;

	my $ess_field = ($ess_is_essential ? "Essential: yes\n" : '');

	$cupt = setup_for_worker(
		'dpkg_status' =>
			entail(compose_installed_record('ess', 0) . $ess_field . "Depends: aux\n") .
			entail(compose_installed_record('aux', 2)) .
			$additional_installed ,
		'packages' =>
			entail(compose_package_record('ess', 1) . $ess_field . "Breaks: aux\n"),
	);
}

setup_aux_between_ess(0, '');
test_dpkg_sequence('install ess # ess is normal package',
		['--remove', [], ['ess']],
		['--remove', [], ['aux']],
		['--install', [], ['<ess 1>']]);

setup_aux_between_ess(1, '');
test_dpkg_sequence('install ess # ess is essential package',
		['--remove', ['--force-depends'], ['aux']],
		['--unpack', ['--force-depends'], ['<ess 1>']],
		['--configure', [], ['ess']]);

setup_aux_between_ess(0, entail(compose_installed_record('highest', 3) . "Essential: yes\nDepends: ess\n"));
test_dpkg_sequence('install ess # ess is a regular dependency of essential',
		['--remove', ['--force-depends'], ['ess']],
		['--remove', ['--force-depends'], ['aux']],
		['--install', ['--force-depends'], ['<ess 1>']]);

setup_aux_between_ess(0, entail(compose_installed_record('highest', 3) . "Essential: yes\nPre-Depends: ess\n"));
test_dpkg_sequence('install ess # ess is a pre-dependency of essential',
		['--remove', ['--force-depends'], ['aux']],
		['--install', ['--force-depends'], ['<ess 1>']]);

