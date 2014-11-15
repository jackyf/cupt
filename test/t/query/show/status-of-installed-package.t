use TestCupt;
use Test::More tests => 21;

use strict;
use warnings;

sub test {
	my ($desired, $flag, $state, $error) = @_;
	
	my $status_line = "Status: $desired $flag $state";
	my $inst_record = "Package: pp\n$status_line\nVersion: 1\nArchitecture: all\n";
	my $cupt = TestCupt::setup('dpkg_status' => $inst_record);

	my $output = stdall("$cupt show pp");

	my $line_to_search;
	if (length($error)) {
		$line_to_search = "E: malformed '$error' status indicator (for the package 'pp')";
	} else {
		$line_to_search = "Status: $state" . ($desired eq 'hold' ? ' (on hold)' : '');
	}

	my $comment = "status line: '$desired $flag $state', error expected: '$error'";
	like($output, qr/^\Q$line_to_search\E$/m, $comment);
}

test('install', 'ok', 'installed' => '');
test('install', 'ok', 'half-installed' => '');
test('install', 'ok', 'unpacked' => '');
test('install', 'ok', 'half-configured' => '');
test('install', 'ok', 'triggers-awaited' => '');
test('install', 'ok', 'triggers-pending' => '');

test('install', 'ok', 'triggers' => 'status');
test('install', 'ok', 'triggers-ok' => 'status');
test('install', 'ok', 'install' => 'status');
test('install', 'ok', 'half' => 'status');
test('install', 'ok', '#*^^$' => 'status');


test('deinstall', 'ok', 'installed' => '');
test('hold', 'ok', 'installed' => '');
test('purge', 'ok', 'installed' => '');

test('installed', 'ok', 'installed' => 'desired');
test('ok', 'ok', 'installed' => 'desired');
test('@#@$&*((n', 'ok', 'installed' => 'desired');


test('install', 'reinstreq', 'installed' => '');
test('purge', 'reinstreq', 'half-configured' => '');

test('install', 'notok', 'installed' => 'error');
test('install', '000:=', 'installed' => 'error');

