use TestCupt;
use Test::More tests => 9;

use strict;
use warnings;

eval get_inc_code('common');

sub setup_aux_between_ess_1 {
	my ($ess_is_essential, $additional_installed) = @_;

	my $ess_field = ($ess_is_essential ? "Essential: yes\n" : '');

	return setup(
		'dpkg_status' =>
			entail(compose_installed_record('ess', 0) . $ess_field . "Depends: aux\n") .
			entail(compose_installed_record('aux', 2)) .
			$additional_installed ,
		'packages' =>
			entail(compose_package_record('ess', 1) . $ess_field . "Breaks: aux\n"),
	);
}

my $cupt = setup_aux_between_ess_1(0, '');
test_dpkg_sequence($cupt, 'install ess # ess is normal package',
		['--remove', [], ['ess']],
		['--remove', [], ['aux']],
		['--install', [], ['<ess 1>']]);

$cupt = setup_aux_between_ess_1(1, '');
test_dpkg_sequence($cupt, 'install ess # ess is essential package',
		['--remove', ['--force-depends'], ['aux']],
		['--unpack', ['--force-depends'], ['<ess 1>']],
		['--configure', [], ['ess']]);
test_dpkg_sequence($cupt, 'remove ess # removing an essential package',
		['--remove', ['--force-remove-essential'], ['ess']]);

$cupt = setup_aux_between_ess_1(0, entail(compose_installed_record('highest', 3) . "Essential: yes\nDepends: ess\n"));
test_dpkg_sequence($cupt, 'install ess # ess is a regular dependency of essential',
		['--remove', ['--force-depends'], ['ess']],
		['--remove', ['--force-depends'], ['aux']],
		['--install', ['--force-depends'], ['<ess 1>']]);

$cupt = setup_aux_between_ess_1(0, entail(compose_installed_record('highest', 3) . "Essential: yes\nPre-Depends: ess\n"));
test_dpkg_sequence($cupt, 'install ess # ess is a pre-dependency of essential',
		['--remove', ['--force-depends'], ['aux']],
		['--install', ['--force-depends'], ['<ess 1>']]);


sub setup_aux_between_ess_2 {
	my ($additional_installed) = @_;

	return setup(
		'dpkg_status' =>
			entail(compose_installed_record('ess', 0)) .
			$additional_installed ,
		'packages' =>
			entail(compose_package_record('aux', 2) . "Breaks: ess (<< 1)\n") .
			entail(compose_package_record('ess', 1) . "Depends: aux\n") ,
	);
}

$cupt = setup_aux_between_ess_2('');
test_dpkg_sequence($cupt, 'install ess # aux breaks old ess',
		['--unpack', [], ['<ess 1>']],
		['--install', [], ['<aux 2>']],
		['--configure', [], ['ess']]);

$cupt = setup_aux_between_ess_2(
		entail(compose_installed_record('higher', 3) . "Depends: ess\n"));
test_dpkg_sequence($cupt, 'install ess # aux breaks old ess, ess is a dependency of normal',
		['--unpack', ['--force-depends'], ['<ess 1>']],
		['--install', ['--force-depends'], ['<aux 2>']],
		['--configure', ['--force-depends'], ['ess']]);

$cupt = setup_aux_between_ess_2(
		entail(compose_installed_record('taivas', 3) . "Essential: yes\nPre-Depends: ess\n"));
test_dpkg_sequence($cupt, 'install ess # aux breaks old ess, ess is a pre-dependency of essential',
		['--install', ['--force-breaks'], ['<aux 2>']],
		['--install', ['--force-breaks'], ['<ess 1>']]);

$cupt = setup_aux_between_ess_2(
		entail(compose_installed_record('taivas', 3) . "Essential: yes\nPre-Depends: middle1\n") .
		entail(compose_installed_record('middle1', 4) . "Depends: middle2\n") .
		entail(compose_installed_record('middle2', 5) . "Pre-Depends: middle3\n") .
		entail(compose_installed_record('middle3', 6) . "Depends: ess\n"));
test_dpkg_sequence($cupt, 'install ess # aux breaks old ess, ess is down dependency chain of (pre-dependency of essential)',
		['--install', ['--force-breaks'], ['<aux 2>']],
		['--install', ['--force-breaks'], ['<ess 1>']]);

